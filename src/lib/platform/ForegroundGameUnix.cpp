/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/ForegroundGame.h"
#include "platform/ForegroundGameUnixParser.h"

#include <QDateTime>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

// XCB owns each query's connection and returns errors as values. Unlike Xlib,
// it does not install a process-global error handler that races input handling.
#if defined(WINAPI_XWINDOWS)
#include <poll.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#endif
namespace deskflow {
namespace {

using foregroundGameUnix::WindowInfo;

ForegroundGameState unavailable(const QString &detail)
{
  ForegroundGameState state;
  state.detail = detail;
  return state;
}

// Desktop files are inspected as data, never executed. Cache the catalogue so
// input polling does not walk the filesystem on every sample.
QSet<QString> desktopGames()
{
  static std::mutex mutex;
  const std::lock_guard lock(mutex);
  static QSet<QString> games;
  static QElapsedTimer age;
  if (age.isValid() && age.elapsed() < 60000)
    return games;
  age.start();
  games.clear();
  for (const auto &location : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
    const QString directory = location + "/applications";
    QDirIterator files(directory, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
      const QString name = files.next();
      QFile file(name);
      if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
        continue;
      bool inEntry = false;
      bool game = false;
      QString windowClass;
      for (const auto &line : QString::fromUtf8(file.readAll()).split('\n')) {
        const QString text = line.trimmed();
        if (text.startsWith('['))
          inEntry = text == "[Desktop Entry]";
        if (!inEntry)
          continue;
        if (text.startsWith("Categories="))
          game = text.mid(11).split(';').contains("Game");
        if (text.startsWith("StartupWMClass="))
          windowClass = text.mid(15);
      }
      if (game) {
        QString desktopId = QDir(directory).relativeFilePath(name);
        desktopId.chop(8); // .desktop
        desktopId.replace('/', '-');
        games.insert(desktopId.toCaseFolded());
        if (!windowClass.isEmpty())
          games.insert(windowClass.toCaseFolded());
      }
    }
  }
  return games;
}

ForegroundGameState classify(WindowInfo info, const QStringList &gameApps, bool nativeGame = false)
{
  ForegroundGameState state;
  state.supported = true;
  state.fullscreen = info.fullscreen;
  if (info.pid > 0) {
    const auto executable = QFileInfo(QStringLiteral("/proc/%1/exe").arg(info.pid)).symLinkTarget();
    if (!executable.isEmpty()) {
      info.identifiers.append(executable);
      info.identifiers.append(QFileInfo(executable).fileName());
      nativeGame = nativeGame || isKnownGamePath(executable);
    }
  }
  state.application = info.identifiers.value(0);
  state.game = nativeGame || matchesGameApplication(info.identifiers, gameApps);
  if (!state.game && state.fullscreen) {
    const auto &games = desktopGames();
    for (const auto &identifier : info.identifiers) {
      if (games.contains(identifier.toCaseFolded())) {
        state.game = true;
        break;
      }
      // Steam assigns this class to games, rather than its own launcher UI.
      if (identifier.startsWith("steam_app_")) {
        bool valid = false;
        const auto appId = identifier.mid(10).toULongLong(&valid);
        state.game = valid && appId != 0;
        if (state.game)
          break;
      }
    }
  }
  return state;
}

// Bound all socket operations to one deadline. No external command or shell is
// started. Oversized/partial responses are rejected rather than cached as valid.
QByteArray socketQuery(const QString &path, const QByteArray &request, bool sway)
{
  constexpr int timeoutMs = 15;
  constexpr qsizetype maxReply = 4 * 1024 * 1024;
  QElapsedTimer deadline;
  deadline.start();
  const auto remaining = [&]() { return std::max(0, timeoutMs - static_cast<int>(deadline.elapsed())); };
  QLocalSocket socket;
  socket.setReadBufferSize(maxReply + 14);
  socket.connectToServer(path, QIODevice::ReadWrite);
  if (!socket.waitForConnected(remaining()))
    return {};
  if (socket.write(request) != request.size() ||
      (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(remaining())))
    return {};
  QByteArray data;
  while (remaining() > 0) {
    data.append(socket.readAll());
    if (data.size() > maxReply + 14)
      return {};
    if (sway && data.size() >= 14) {
      quint32 length = 0;
      quint32 type = 0;
      std::memcpy(&length, data.constData() + 6, sizeof(length));
      std::memcpy(&type, data.constData() + 10, sizeof(type));
      if (!data.startsWith("i3-ipc") || type != 4 || length > maxReply)
        return {};
      if (data.size() >= 14 + static_cast<qsizetype>(length))
        return data.mid(14, length);
    } else if (!sway && !QJsonDocument::fromJson(data).isNull()) {
      return data;
    }
    if (!socket.waitForReadyRead(remaining()))
      break;
  }
  return {};
}

ForegroundGameState querySway(const QString &socket, const QStringList &gameApps)
{
  const quint32 length = 0;
  const quint32 type = 4; // GET_TREE, native endian as specified by i3/Sway IPC.
  QByteArray request("i3-ipc", 6);
  request.append(reinterpret_cast<const char *>(&length), sizeof(length));
  request.append(reinterpret_cast<const char *>(&type), sizeof(type));
  const auto document = QJsonDocument::fromJson(socketQuery(socket, request, true));
  if (!document.isObject())
    return unavailable(QStringLiteral("Sway/i3 foreground-state query failed"));
  return classify(foregroundGameUnix::swayWindow(document.object()), gameApps);
}

ForegroundGameState queryHyprland(const QStringList &gameApps)
{
  const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
  // The instance id is a path component, not a filesystem path or command.
  if (signature.isEmpty() || signature.contains('/') || signature.contains(".."))
    return unavailable(QStringLiteral("Invalid Hyprland instance identifier"));
  const QString path = qEnvironmentVariable("XDG_RUNTIME_DIR") + "/hypr/" + signature + "/.socket.sock";
  const auto document = QJsonDocument::fromJson(socketQuery(path, QByteArrayLiteral("j/activewindow"), false));
  if (!document.isObject())
    return unavailable(QStringLiteral("Hyprland foreground-state query failed"));
  // fullscreen is the compositor's effective state, not fullscreenClient.
  // Geometry alone cannot establish whether the application draws its own
  // decorations. Unflagged borderless windows need a compositor adapter.
  return classify(foregroundGameUnix::hyprlandWindow(document.object()), gameApps);
}

ForegroundGameState queryStateFile(const QString &path, const QStringList &gameApps)
{
  const QFileInfo info(path);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 fileAge = now - info.lastModified().toMSecsSinceEpoch();
  if (!info.isFile() || info.size() > 16384 || fileAge < -250 || fileAge > 1500)
    return unavailable(QStringLiteral("Foreground-state adapter file is missing or stale"));
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return unavailable(QStringLiteral("Cannot read foreground-state adapter file"));
  const auto document = QJsonDocument::fromJson(file.read(16385));
  const auto object = document.object();
  const qint64 age = now - object.value("updatedAtMs").toInteger();
  if (!document.isObject() || object.value("version").toInt() != 1 || age < -250 || age > 1500 ||
      !object.value("fullscreen").isBool() || !object.value("application").isString() ||
      (object.contains("game") && !object.value("game").isBool()))
    return unavailable(QStringLiteral("Invalid foreground-state adapter data"));
  WindowInfo window;
  window.identifiers = {object.value("application").toString()};
  window.fullscreen = object.value("fullscreen").toBool();
  // An empty application means there is no focused application.
  if (window.identifiers.first().isEmpty())
    window.fullscreen = false;
  return classify(window, gameApps, object.value("game").toBool());
}

#if defined(WINAPI_XWINDOWS)
template <typename T> auto reply(xcb_connection_t *connection, unsigned int sequence, const QElapsedTimer &deadline)
{
  void *value = nullptr;
  xcb_generic_error_t *error = nullptr;
  xcb_flush(connection);
  while (deadline.elapsed() < 30 && !xcb_connection_has_error(connection)) {
    if (xcb_poll_for_reply(connection, sequence, &value, &error)) {
      std::free(error);
      return std::unique_ptr<T, decltype(&std::free)>(static_cast<T *>(value), &std::free);
    }
    pollfd socket{xcb_get_file_descriptor(connection), POLLIN, 0};
    const int remaining = std::max(1, 30 - static_cast<int>(deadline.elapsed()));
    if (poll(&socket, 1, remaining) <= 0 || (socket.revents & (POLLERR | POLLHUP | POLLNVAL)))
      break;
  }
  xcb_discard_reply(connection, sequence);
  return std::unique_ptr<T, decltype(&std::free)>(nullptr, &std::free);
}

xcb_atom_t atom(xcb_connection_t *connection, const char *name, const QElapsedTimer &deadline)
{
  const auto value = reply<xcb_intern_atom_reply_t>(
      connection, xcb_intern_atom(connection, true, static_cast<uint16_t>(std::strlen(name)), name).sequence, deadline
  );
  return value ? value->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
}

std::vector<uint32_t> propertyNumbers(
    xcb_connection_t *connection, xcb_window_t window, const char *name, xcb_atom_t type, const QElapsedTimer &deadline
)
{
  const auto property = atom(connection, name, deadline);
  if (property == XCB_ATOM_NONE)
    return {};
  const auto value = reply<xcb_get_property_reply_t>(
      connection, xcb_get_property(connection, false, window, property, type, 0, 1024).sequence, deadline
  );
  if (!value || value->type != type || value->format != 32)
    return {};
  const auto *data = static_cast<const uint32_t *>(xcb_get_property_value(value.get()));
  return {data, data + value->value_len};
}

QString propertyText(xcb_connection_t *connection, xcb_window_t window, const char *name, const QElapsedTimer &deadline)
{
  const auto property = atom(connection, name, deadline);
  if (property == XCB_ATOM_NONE)
    return {};
  const auto value = reply<xcb_get_property_reply_t>(
      connection, xcb_get_property(connection, false, window, property, XCB_GET_PROPERTY_TYPE_ANY, 0, 1024).sequence,
      deadline
  );
  if (!value || value->format != 8)
    return {};
  return QString::fromUtf8(static_cast<const char *>(xcb_get_property_value(value.get())), value->value_len);
}

ForegroundGameState queryX11(const QStringList &gameApps, const QString &displayName)
{
  QElapsedTimer deadline;
  deadline.start();
  int screenIndex = 0;
  const QByteArray displayBytes = displayName.toLocal8Bit();
  std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection(
      xcb_connect(displayBytes.isEmpty() ? nullptr : displayBytes.constData(), &screenIndex), &xcb_disconnect
  );
  auto *display = connection.get();
  if (!display || xcb_connection_has_error(display))
    return unavailable(QStringLiteral("Cannot connect to X11 for foreground-state detection"));
  auto screens = xcb_setup_roots_iterator(xcb_get_setup(display));
  for (int i = 0; i < screenIndex && screens.rem; ++i)
    xcb_screen_next(&screens);
  if (!screens.rem)
    return unavailable(QStringLiteral("Cannot find the X11 screen"));
  const auto *screen = screens.data;
  const auto active = propertyNumbers(display, screen->root, "_NET_ACTIVE_WINDOW", XCB_ATOM_WINDOW, deadline);
  if (active.empty())
    return unavailable(QStringLiteral("The X11 window manager does not expose _NET_ACTIVE_WINDOW"));
  const auto window = active.front();
  if (window == XCB_WINDOW_NONE)
    return classify({}, gameApps);
  const auto attributes =
      reply<xcb_get_window_attributes_reply_t>(display, xcb_get_window_attributes(display, window).sequence, deadline);
  if (!attributes || attributes->map_state != XCB_MAP_STATE_VIEWABLE)
    return unavailable(QStringLiteral("The focused X11 window changed during detection"));
  WindowInfo info;
  const auto states = propertyNumbers(display, window, "_NET_WM_STATE", XCB_ATOM_ATOM, deadline);
  const auto fullscreen = atom(display, "_NET_WM_STATE_FULLSCREEN", deadline);
  info.fullscreen = fullscreen != XCB_ATOM_NONE && std::find(states.begin(), states.end(), fullscreen) != states.end();
  if (!info.fullscreen) {
    const auto frame = propertyNumbers(display, window, "_NET_FRAME_EXTENTS", XCB_ATOM_CARDINAL, deadline);
    const bool borderless = frame.size() == 4 && std::all_of(frame.begin(), frame.end(), [](auto v) { return v == 0; });
    if (borderless) {
      const auto geometry =
          reply<xcb_get_geometry_reply_t>(display, xcb_get_geometry(display, window).sequence, deadline);
      const auto position = reply<xcb_translate_coordinates_reply_t>(
          display, xcb_translate_coordinates(display, window, screen->root, 0, 0).sequence, deadline
      );
      if (geometry && position) {
        const QRect rect(position->dst_x, position->dst_y, geometry->width, geometry->height);
        const auto resources = reply<xcb_randr_get_screen_resources_current_reply_t>(
            display, xcb_randr_get_screen_resources_current(display, screen->root).sequence, deadline
        );
        if (resources) {
          const auto *crtcs = xcb_randr_get_screen_resources_current_crtcs(resources.get());
          for (int i = 0; i < resources->num_crtcs && !info.fullscreen; ++i) {
            const auto crtc = reply<xcb_randr_get_crtc_info_reply_t>(
                display, xcb_randr_get_crtc_info(display, crtcs[i], resources->config_timestamp).sequence, deadline
            );
            if (crtc)
              info.fullscreen = crtc->mode != XCB_NONE && rect == QRect(crtc->x, crtc->y, crtc->width, crtc->height);
          }
        }
        if (!info.fullscreen)
          info.fullscreen = rect == QRect(0, 0, screen->width_in_pixels, screen->height_in_pixels);
      }
    }
  }
  // WM_CLASS is two NUL-delimited strings: instance, then class.
  const auto windowClass = propertyText(display, window, "WM_CLASS", deadline);
  const auto classes = windowClass.split(QChar('\0'), Qt::SkipEmptyParts);
  for (auto i = classes.crbegin(); i != classes.crend(); ++i)
    info.identifiers.append(*i);
  for (const auto *property : {"_GTK_APPLICATION_ID", "_KDE_NET_WM_DESKTOP_FILE"}) {
    auto id = propertyText(display, window, property, deadline);
    if (id.endsWith(".desktop"))
      id.chop(8);
    if (!id.isEmpty())
      info.identifiers.append(id);
  }
  const auto pid = propertyNumbers(display, window, "_NET_WM_PID", XCB_ATOM_CARDINAL, deadline);
  if (!pid.empty())
    info.pid = static_cast<qint64>(pid.front());
  const auto steam = propertyNumbers(display, window, "_STEAM_GAME", XCB_ATOM_CARDINAL, deadline);
  // Do not publish a game from a window that lost focus during the queries.
  if (propertyNumbers(display, screen->root, "_NET_ACTIVE_WINDOW", XCB_ATOM_WINDOW, deadline) != active)
    return unavailable(QStringLiteral("The focused X11 window changed during detection"));
  return classify(info, gameApps, !steam.empty() && steam.front() > 0);
}
#endif
ForegroundGameState sample(const QStringList &gameApps, const QString &stateFile, const QString &displayName)
{
  if (!stateFile.isEmpty())
    return queryStateFile(stateFile, gameApps);
  // The X11 backend can explicitly select another display than the ambient
  // compositor. In that case ambient I3SOCK/SWAYSOCK must not override it. The
  // core ignores core/display in Wayland sessions, so preserve that behavior.
  if (!displayName.isEmpty() && qEnvironmentVariable("XDG_SESSION_TYPE") != "wayland") {
#if defined(WINAPI_XWINDOWS)
    return queryX11(gameApps, displayName);
#else
    return unavailable(QStringLiteral("The configured X11 display cannot be inspected in this build"));
#endif
  }
  const QString swaySocket = qEnvironmentVariable("SWAYSOCK");
  if (!swaySocket.isEmpty())
    return querySway(swaySocket, gameApps);
  if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
    return queryHyprland(gameApps);
  const bool wayland =
      qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland" || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
  // XWayland sees only X11 windows and may retain stale focus while a native
  // Wayland app is active. Never use it as a global Wayland focus detector.
  if (wayland)
    return unavailable(QStringLiteral(
        "This Wayland compositor needs a foreground-state adapter; configure core/fullscreenGameStateFile"
    ));
  const QString i3Socket = qEnvironmentVariable("I3SOCK");
  if (!i3Socket.isEmpty())
    return querySway(i3Socket, gameApps);
#if defined(WINAPI_XWINDOWS)
  return queryX11(gameApps, displayName);
#else
  return unavailable(QStringLiteral("X11 detection was not built; a foreground-state adapter is required"));
#endif
}

} // namespace

ForegroundGameState
queryForegroundGame(const QStringList &gameApps, const QString &stateFile, const QString &displayName)
{
  // Called by a single background worker; this never runs in a mouse hook.
  return sample(gameApps, stateFile, displayName);
}

} // namespace deskflow
