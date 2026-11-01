/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "AlignmentProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <cmath>
#include <limits>

namespace deskflow::alignment {
namespace {
bool text(const QJsonValue &value, qsizetype maximum = 255)
{
  return value.isString() && !value.toString().isEmpty() && value.toString().size() <= maximum &&
         !value.toString().contains(QChar::Null);
}
bool number(const QJsonValue &value, double low, double high)
{
  return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= low && value.toDouble() <= high;
}
bool points(const QJsonValue &value)
{
  if (!value.isArray() || (value.toArray().size() != 2 && value.toArray().size() != 3))
    return false;
  for (const auto &point : value.toArray()) {
    if (!number(point, 0, 1))
      return false;
  }
  return true;
}
} // namespace

bool validMessage(const QJsonObject &message, bool allowCapability)
{
  const auto type = message.value("type").toString();
  if (allowCapability && type == "capability") {
    return message.value("version").toInt() == 1 && message.value("uiReady").isBool() &&
           (!message.contains("source") || text(message.value("source")));
  }
  const auto session = message.value("session").toString();
  const QUuid uuid(session);
  if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != session.toLower() || !text(message.value("target")))
    return false;
  if (message.contains("source") && !text(message.value("source")))
    return false;
  if (message.contains("revision") &&
      (!number(message.value("revision"), 1, 2147483647) ||
       message.value("revision").toDouble() != std::floor(message.value("revision").toDouble())))
    return false;
  if ((type == "show" || type == "ready" || type == "points") && !message.contains("revision"))
    return false;
  if ((type == "ready" || type == "points") &&
      (!number(message.value("seq"), 0, 9007199254740991.0) ||
       message.value("seq").toDouble() != std::floor(message.value("seq").toDouble())))
    return false;
  if (message.contains("reason") &&
      (!message.value("reason").isString() || message.value("reason").toString().size() > 256))
    return false;
  if (type == "show") {
    const auto edge = message.value("edge").toString();
    return (edge == "left" || edge == "right" || edge == "top" || edge == "bottom") &&
           points(message.value("points")) && (!message.contains("check") || message.value("check").isBool());
  }
  if (type == "ready") {
    const auto screen = message.value("screen").toObject();
    return points(message.value("points")) && number(screen.value("width"), 1, 1000000) &&
           number(screen.value("height"), 1, 1000000) && number(screen.value("dpr"), 0.01, 100) &&
           number(screen.value("screenCount"), 1, 128) &&
           screen.value("screenCount").toDouble() == std::floor(screen.value("screenCount").toDouble());
  }
  if (type == "points") {
    const auto seq = message.value("seq");
    return points(message.value("points")) && message.value("dragging").isBool() &&
           number(seq, 0, 9007199254740991.0) && seq.toDouble() == std::floor(seq.toDouble());
  }
  return type == "ping" || type == "close" || type == "error" || type == "cancel";
}

QByteArray encode(const QJsonObject &message, bool allowCapability)
{
  if (!validMessage(message, allowCapability))
    return {};
  const auto data = QJsonDocument(message).toJson(QJsonDocument::Compact);
  return data.size() <= maxMessageBytes ? data : QByteArray();
}

std::optional<QJsonObject> decode(const QByteArray &data, bool allowCapability)
{
  if (data.isEmpty() || data.size() > maxMessageBytes)
    return std::nullopt;
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
      !validMessage(document.object(), allowCapability))
    return std::nullopt;
  return document.object();
}

QString encodeIpc(const QJsonObject &message)
{
  // The local IPC delimiter is '='. Omit base64 padding so old parsing cannot
  // truncate JSON containing delimiter/newline characters inside string values.
  const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
  if (bytes.size() > maxMessageBytes)
    return {};
  return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

std::optional<QJsonObject> decodeIpc(const QString &data)
{
  if (data.isEmpty() || data.size() > ((maxMessageBytes + 2) / 3) * 4)
    return std::nullopt;
  const auto decoded = QByteArray::fromBase64Encoding(
      data.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors
  );
  if (!decoded || decoded.decoded.size() > maxMessageBytes)
    return std::nullopt;
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(decoded.decoded, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
    return std::nullopt;
  return document.object();
}

QJsonObject error(const QJsonObject &request, const QString &source, const QString &reason)
{
  return {{"type", "error"},  {"session", request.value("session")}, {"source", source},
          {"target", source}, {"peer", request.value("target")},     {"reason", reason}};
}
} // namespace deskflow::alignment
