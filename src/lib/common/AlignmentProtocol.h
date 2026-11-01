/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace deskflow::alignment {
inline constexpr qsizetype maxMessageBytes = 8192;
inline constexpr qsizetype maxIpcLineBytes = 65536;

bool validMessage(const QJsonObject &message, bool allowCapability = false);
QByteArray encode(const QJsonObject &message, bool allowCapability = false);
std::optional<QJsonObject> decode(const QByteArray &data, bool allowCapability = false);
QString encodeIpc(const QJsonObject &message);
std::optional<QJsonObject> decodeIpc(const QString &data);
QJsonObject error(const QJsonObject &request, const QString &source, const QString &reason);
} // namespace deskflow::alignment
