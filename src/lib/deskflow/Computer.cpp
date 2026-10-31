/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2003 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/Computer.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/IPlatformComputer.h"

#include <QProcess>
#include <thread>

#ifdef Q_OS_WIN
#include "arch/win32/ArchMiscWindows.h"
#include "platform/MSWindowsProcess.h"
#endif

namespace deskflow {

namespace {

bool runComputerCommand(const QString &commandLine)
{
#ifdef Q_OS_WIN
  using deskflow::platform::MSWindowsProcess;
  if (ArchMiscWindows::isProcessElevated()) {
    LOG_DEBUG("current process is elevated, starting detached process as session user");
    return MSWindowsProcess::startDetachedAsSessionUser(commandLine.toStdWString());
  }
#endif

  auto args = QProcess::splitCommand(commandLine);
  if (args.isEmpty()) {
    return false;
  }
  const auto program = args.takeFirst();
  return QProcess::startDetached(program, args);
}

} // namespace

//
// Computer
//

Computer::Computer(IPlatformComputer *platformComputer, IEventQueue *events)
    : m_gameGuardDisplay(Settings::value(Settings::Core::Display).toString()),
      m_computer(platformComputer),
      m_isPrimary(platformComputer->isPrimary()),
      m_entered(m_isPrimary),
      m_events(events)
{
  assert(m_computer != nullptr);

  // reset options
  resetOptions();

  LOG_DEBUG("opened display");
}

Computer::~Computer()
{

  if (m_enabled) {
    disable();
  }
  assert(!m_enabled);

  // Originally there was an assert here added before 2009 (history not in
  // tact). This condition seems to occur on a Windows client when the process
  // is shut down to make way for a new elevated process (e.g. at login screen).
  // The reason why this assert was originally added is unclear, and was causing
  // pain when using debug builds; you lose control of the client when it's at
  // the login screen. Therefore it has been converted to a warning so that we
  // can still see when it happens but it won't cause the process to pause. This
  // also gives us the added benefit of seeing when it happens in production.
  // Perhaps it indicates that the cursor is still being controlled on the
  // client while it's shutting down? i.e. the computer is entered and is not the
  // server, or the computer is not entered and is the server.
  if (m_entered != m_isPrimary) {
    LOG(
        (CLOG_DEBUG "current computer: entered=%s, primary=%s", //
         m_entered ? "yes" : "no", m_isPrimary ? "yes" : "no")
    );
    if (m_isPrimary) {
      LOG_WARN("current primary computer is not entered on shutdown");
    } else {
      LOG_WARN("current secondary computer is entered on shutdown");
    }
  }

  delete m_computer;
  LOG_DEBUG("closed display");
}

void Computer::enable()
{
  invalidateForegroundGameGuard();
  assert(!m_enabled);

  m_computer->updateKeyMap();
  m_computer->updateKeyState();
  m_computer->enable();
  if (m_isPrimary) {
    enablePrimary();
  } else {
    enableSecondary();
  }

  // note activation
  m_enabled = true;
}

void Computer::disable()
{
  assert(m_enabled);

  if (!m_isPrimary && m_entered) {
    leave();
  } else if (m_isPrimary && !m_entered) {
    enter(0);
  }
  m_computer->disable();
  if (m_isPrimary) {
    disablePrimary();
  } else {
    disableSecondary();
  }

  // note deactivation
  m_enabled = false;
}

void Computer::enter(KeyModifierMask toggleMask)
{
  invalidateForegroundGameGuard();
  LOG_INFO("entering computer");

  if (m_entered) {
    LOG_WARN("computer already entered");
  }

  // now on computer
  m_entered = true;

  m_computer->enter();
  if (m_isPrimary) {
    enterPrimary();
  } else {
    enterSecondary(toggleMask);
  }

  if (Settings::value(Settings::Core::EnableEnterCommand).toBool()) {
    const auto commandLine = Settings::value(Settings::Core::ComputerEnterCommand).toString();
    LOG_DEBUG("running computer enter command: %s", qPrintable(commandLine));
    if (!runComputerCommand(commandLine))
      LOG_ERR("failed to run computer enter command");
  }
}

bool Computer::leave()
{
  invalidateForegroundGameGuard();
  LOG_INFO("leaving computer");

  if (!m_entered) {
    LOG_WARN("computer already left");
  }

  if (!m_computer->canLeave()) {
    return false;
  }

  if (m_isPrimary) {
    leavePrimary();
  } else {
    leaveSecondary();
  }

  m_computer->leave();
  if (Settings::value(Settings::Core::EnableExitCommand).toBool()) {
    const auto commandLine = Settings::value(Settings::Core::ComputerExitCommand).toString();
    LOG_DEBUG("running computer exit command: %s", qPrintable(commandLine));
    if (!runComputerCommand(commandLine))
      LOG_ERR("failed to run computer exit command");
  }

  // make sure our idea of clipboard ownership is correct
  m_computer->checkClipboards();

  // now not on computer
  m_entered = false;

  return true;
}

void Computer::reconfigure(uint32_t activeSides)
{
  assert(m_isPrimary);
  m_computer->reconfigure(activeSides);
}

void Computer::warpCursor(int32_t x, int32_t y)
{
  assert(m_isPrimary);
  m_computer->warpCursor(x, y);
}

void Computer::setClipboard(ClipboardID id, const IClipboard *clipboard)
{
  m_computer->setClipboard(id, clipboard);
}

void Computer::grabClipboard(ClipboardID id)
{
  m_computer->setClipboard(id, nullptr);
}

void Computer::screensaver(bool) const
{
  // do nothing
}

void Computer::keyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &lang)
{
  // check for ctrl+alt+del emulation
  if (id == kKeyDelete && (mask & (KeyModifierControl | KeyModifierAlt)) == (KeyModifierControl | KeyModifierAlt)) {
    LOG_DEBUG("emulating ctrl+alt+del press");
    if (m_computer->fakeCtrlAltDel()) {
      return;
    }
  }
  m_computer->fakeKeyDown(id, mask, button, lang);
}

void Computer::keyRepeat(KeyID id, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &lang)
{
  assert(!m_isPrimary);
  m_computer->fakeKeyRepeat(id, mask, count, button, lang);
}

void Computer::keyUp(KeyID, KeyModifierMask, KeyButton button)
{
  m_computer->fakeKeyUp(button);
}

void Computer::mouseDown(ButtonID button)
{
  m_computer->fakeMouseButton(button, true);
}

void Computer::mouseUp(ButtonID button)
{
  m_computer->fakeMouseButton(button, false);
}

void Computer::mouseMove(int32_t x, int32_t y)
{
  assert(!m_isPrimary);
  m_computer->fakeMouseMove(x, y);
}

void Computer::mouseRelativeMove(int32_t dx, int32_t dy) const
{
  assert(!m_isPrimary);
  m_computer->fakeMouseRelativeMove(dx, dy);
}

void Computer::mouseWheel(int32_t xDelta, int32_t yDelta) const
{
  assert(!m_isPrimary);
  m_computer->fakeMouseWheel({xDelta, yDelta});
}

void Computer::resetOptions()
{
  // reset options
  m_halfDuplex = 0;

  // let screen handle its own options
  m_computer->resetOptions();
}

void Computer::setOptions(const OptionsList &options)
{
  if (options.size() % 2 != 0) {
    LOG_ERR("options are the incorrect size, can not process them");
    return;
  }

  // update options
  for (uint32_t i = 0, n = (uint32_t)options.size(); i < n; i += 2) {
    if (options[i] == kOptionHalfDuplexCapsLock) {
      if (options[i + 1] != 0) {
        m_halfDuplex |= KeyModifierCapsLock;
      } else {
        m_halfDuplex &= ~KeyModifierCapsLock;
      }
      LOG_VERBOSE("half-duplex caps-lock %s", ((m_halfDuplex & KeyModifierCapsLock) != 0) ? "on" : "off");
    } else if (options[i] == kOptionHalfDuplexNumLock) {
      if (options[i + 1] != 0) {
        m_halfDuplex |= KeyModifierNumLock;
      } else {
        m_halfDuplex &= ~KeyModifierNumLock;
      }
      LOG_VERBOSE("half-duplex num-lock %s", ((m_halfDuplex & KeyModifierNumLock) != 0) ? "on" : "off");
    } else if (options[i] == kOptionHalfDuplexScrollLock) {
      if (options[i + 1] != 0) {
        m_halfDuplex |= KeyModifierScrollLock;
      } else {
        m_halfDuplex &= ~KeyModifierScrollLock;
      }
      LOG_VERBOSE("half-duplex scroll-lock %s", ((m_halfDuplex & KeyModifierScrollLock) != 0) ? "on" : "off");
    }
  }

  // update half-duplex options
  m_computer->setHalfDuplexMask(m_halfDuplex);

  // let screen handle its own options
  m_computer->setOptions(options);
}

void Computer::setSequenceNumber(uint32_t seqNum)
{
  m_computer->setSequenceNumber(seqNum);
}

uint32_t Computer::registerHotKey(KeyID key, KeyModifierMask mask)
{
  return m_computer->registerHotKey(key, mask);
}

void Computer::unregisterHotKey(uint32_t id)
{
  m_computer->unregisterHotKey(id);
}

void Computer::fakeInputBegin()
{
  assert(!m_fakeInput);

  m_fakeInput = true;
  m_computer->fakeInputBegin();
}

void Computer::fakeInputEnd()
{
  assert(m_fakeInput);

  m_fakeInput = false;
  m_computer->fakeInputEnd();
}

bool Computer::isOnComputer() const
{
  return m_entered;
}

bool Computer::isLockedToComputer() const
{
  if (uint32_t buttonID = 0; m_computer->isAnyMouseButtonDown(buttonID)) {
    LOG_DEBUG("locked by mouse buttonID: %d", buttonID);
    return true;
  }
  // not locked
  return false;
}

bool Computer::isForegroundGameGuarded() const
{
  using namespace std::chrono;
  const auto now = steady_clock::now();
  // QSettings is shared, so capture all inputs on the event-loop thread.
  const bool enabled = Settings::value(Settings::Core::EnableFullscreenGameGuard).toBool();
  const auto apps = Settings::value(Settings::Core::FullscreenGameApps).toStringList();
  const auto stateFile = Settings::value(Settings::Core::FullscreenGameStateFile).toString();
  if (enabled != m_gameGuardEnabled || apps != m_gameGuardApps || stateFile != m_gameGuardStateFile) {
    invalidateForegroundGameGuard();
    m_gameGuardEnabled = enabled;
    m_gameGuardApps = apps;
    m_gameGuardStateFile = stateFile;
  }
  if (!enabled) {
    return false;
  }

  if (m_gameGuardQuery.valid() && m_gameGuardQuery.wait_for(seconds(0)) == std::future_status::ready) {
    ForegroundGameState state;
    try {
      state = m_gameGuardQuery.get();
    } catch (const std::exception &e) {
      state.detail = QStringLiteral("Fullscreen game detection failed: %1").arg(QString::fromUtf8(e.what()));
    }
    if (now - m_gameGuardQueryStarted >= seconds(1)) {
      state = {};
      state.detail = QStringLiteral("Detection timed out; fullscreen game protection is temporarily unavailable");
    }
    if (m_gameGuardQueryGeneration == m_gameGuardGeneration) {
      m_cachedGameGuard = state.supported && state.fullscreen && state.game;
      m_gameGuardValid = true;
      // Freshness starts when inspection began, not when its result happened
      // to be consumed. A delayed observation must not relock a different app.
      m_gameGuardObserved = m_gameGuardQueryStarted;
      const auto diagnostic = !state.detail.isEmpty() ? state.detail
                              : m_cachedGameGuard     ? QStringLiteral("Foreground fullscreen game protected")
                                                      : QStringLiteral("No foreground fullscreen game");
      if (diagnostic != m_gameGuardDiagnostic) {
        m_gameGuardDiagnostic = diagnostic;
        if (!state.supported || (state.fullscreen && !state.game)) {
          LOG_WARN("fullscreen game guard: %s (%s)", qPrintable(diagnostic), qPrintable(state.application));
        } else {
          LOG_DEBUG("fullscreen game guard: %s (%s)", qPrintable(diagnostic), qPrintable(state.application));
        }
      }
    }
  }

  if (m_gameGuardQuery.valid() && now - m_gameGuardQueryStarted >= seconds(1)) {
    const auto diagnostic =
        QStringLiteral("Detection timed out; fullscreen game protection is temporarily unavailable");
    if (m_gameGuardDiagnostic != diagnostic) {
      m_gameGuardDiagnostic = diagnostic;
      LOG_WARN("fullscreen game guard: %s", qPrintable(diagnostic));
    }
  }

  if (!m_gameGuardQuery.valid() && now - m_gameGuardQueryStarted >= milliseconds(100)) {
    // Settings owns a QSettings instance: snapshot it here, never on the worker.
    m_gameGuardQueryGeneration = m_gameGuardGeneration;
    m_gameGuardQueryStarted = now;
    std::packaged_task<ForegroundGameState()> query([apps, stateFile, displayName = m_gameGuardDisplay] {
      return queryForegroundGame(apps, stateFile, displayName);
    });
    m_gameGuardQuery = query.get_future();
    // A stalled OS service must not block destruction of Computer. This worker
    // owns its snapshots and native handles, and never refers to this object.
    try {
      std::thread(std::move(query)).detach();
    } catch (const std::system_error &e) {
      m_gameGuardQuery = {};
      m_gameGuardValid = true;
      m_cachedGameGuard = false;
      m_gameGuardObserved = now;
      LOG_WARN("unable to start fullscreen game detection: %s", e.what());
    }
  }

  // Briefly inhibit edge crossing on entry until the first current observation.
  // A broken detector cannot trap the cursor indefinitely; explicit switches
  // are always available. Subsequent samples preserve the last known result.
  return m_gameGuardValid ? m_cachedGameGuard && now - m_gameGuardObserved < seconds(1)
                          : now - m_gameGuardInvalidated < seconds(1);
}

void Computer::invalidateForegroundGameGuard() const
{
  ++m_gameGuardGeneration;
  m_gameGuardValid = false;
  m_gameGuardInvalidated = std::chrono::steady_clock::now();
}

int32_t Computer::getJumpZoneSize() const
{
  if (!m_isPrimary) {
    return 0;
  } else {
    return m_computer->getJumpZoneSize();
  }
}

void Computer::getCursorCenter(int32_t &x, int32_t &y) const
{
  m_computer->getCursorCenter(x, y);
}

KeyModifierMask Computer::getActiveModifiers() const
{
  return m_computer->getActiveModifiers();
}

KeyModifierMask Computer::pollActiveModifiers() const
{
  return m_computer->pollActiveModifiers();
}

void *Computer::getEventTarget() const
{
  return m_computer;
}

bool Computer::getClipboard(ClipboardID id, IClipboard *clipboard) const
{
  return m_computer->getClipboard(id, clipboard);
}

void Computer::getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const
{
  m_computer->getShape(x, y, w, h);
}

void Computer::getCursorPos(int32_t &x, int32_t &y) const
{
  m_computer->getCursorPos(x, y);
}

void Computer::enablePrimary()
{
  // get notified of screen saver activation/deactivation
  m_computer->openScreensaver(true);

  // claim computer changed size
  m_events->addEvent(Event(EventTypes::ComputerShapeChanged, getEventTarget()));
}

void Computer::enableSecondary()
{
  // assume primary has all clipboards
  for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
    grabClipboard(id);
  }
}

void Computer::disablePrimary()
{
  // done with screen saver
  m_computer->closeScreensaver();
}

void Computer::disableSecondary()
{
  // done with screen saver
  m_computer->closeScreensaver();
}

void Computer::enterPrimary() const
{
  // do nothing
}

void Computer::enterSecondary(KeyModifierMask) const
{
  // do nothing
}

void Computer::leavePrimary()
{
  // we don't track keys while on the primary computer so update our
  // idea of them now.  this is particularly to update the state of
  // the toggle modifiers.
  m_computer->updateKeyState();
}

void Computer::leaveSecondary()
{
  // release any keys we think are still down
  m_computer->fakeAllKeysUp();
}

std::string Computer::getSecureInputApp() const
{
  return m_computer->getSecureInputApp();
}

} // namespace deskflow
