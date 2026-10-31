/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QRegularExpression>
#include <QString>

namespace deskflow::probe {

inline constexpr int kMaximumDurationMs = 15 * 60 * 1000;
inline constexpr int kQueryTimeoutMs = 1000;

// Pure validation: invalid CLI arguments never invoke native inspection.
inline QString validateSampling(const QString &samplesText, const QString &intervalText, int &samples, int &intervalMs)
{
  bool valid = false;
  samples = samplesText.toInt(&valid);
  if (!valid || samples < 1 || samples > 1200)
    return QStringLiteral("--samples must be an integer from 1 to 1200");
  intervalMs = intervalText.toInt(&valid);
  if (!valid || intervalMs < 100 || intervalMs > 60000)
    return QStringLiteral("--interval-ms must be an integer from 100 to 60000");
  if (static_cast<qint64>(samples - 1) * intervalMs > kMaximumDurationMs - kQueryTimeoutMs)
    return QStringLiteral("The requested sampling schedule exceeds the 15-minute limit");
  return {};
}

inline bool isLocalX11Display(const QString &display)
{
  static const QRegularExpression local(QStringLiteral("\\A(?::|unix:|unix/:)[0-9]+(?:\\.[0-9]+)?\\z"));
  return display.isEmpty() || local.match(display).hasMatch();
}

} // namespace deskflow::probe
