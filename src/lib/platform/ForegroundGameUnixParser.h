/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QStringList>

namespace deskflow::foregroundGameUnix {

struct WindowInfo
{
  bool fullscreen = false;
  QStringList identifiers;
  qint64 pid = 0;
};

inline QRect jsonRect(const QJsonObject &object)
{
  return {
      object.value("x").toInt(), object.value("y").toInt(), object.value("width").toInt(),
      object.value("height").toInt()
  };
}

inline WindowInfo swayWindow(const QJsonObject &node, QRect output = {}, int depth = 0)
{
  if (depth > 64)
    return {};
  if (node.value("type").toString() == "output")
    output = jsonRect(node.value("rect").toObject());

  const auto properties = node.value("window_properties").toObject();
  const QString appId = node.value("app_id").toString();
  const QString windowClass = properties.value("class").toString();
  if (node.value("focused").toBool() && node.value("visible").toBool(true) &&
      (!appId.isEmpty() || !windowClass.isEmpty())) {
    WindowInfo info;
    info.identifiers = {appId, windowClass, properties.value("instance").toString()};
    info.identifiers.removeAll(QString{});
    info.pid = node.value("pid").toInteger();
    const QRect rect = jsonRect(node.value("rect").toObject());
    const bool borderless = node.value("border").toString() == "none";
    info.fullscreen = node.value("fullscreen_mode").toInt() != 0 || (borderless && output.isValid() && rect == output);
    return info;
  }
  for (const auto &children : {QStringLiteral("nodes"), QStringLiteral("floating_nodes")}) {
    for (const auto &value : node.value(children).toArray()) {
      auto result = swayWindow(value.toObject(), output, depth + 1);
      if (!result.identifiers.isEmpty())
        return result;
    }
  }
  return {};
}

inline WindowInfo hyprlandWindow(const QJsonObject &window)
{
  WindowInfo info;
  if (window.isEmpty() || window.value("hidden").toBool() || !window.value("mapped").toBool(true))
    return info;
  info.identifiers = {window.value("class").toString(), window.value("initialClass").toString()};
  info.identifiers.removeAll(QString{});
  info.pid = window.value("pid").toInteger();
  // Internal compositor state, not fullscreenClient (an app can believe it is
  // fullscreen while the compositor keeps it in a small window). Bit 1 is the
  // fullscreen state; bit 0 alone only means maximized.
  info.fullscreen = (window.value("fullscreen").toInt() & 2) != 0;
  return info;
}

} // namespace deskflow::foregroundGameUnix
