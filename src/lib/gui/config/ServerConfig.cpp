/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerConfig.h"

#include "common/Hotkey.h"
#include "common/Settings.h"

#include <QAbstractButton>
#include <QHash>
#include <QPushButton>

#include <algorithm>
#include <limits>

using enum ComputerConfig::SwitchCorner;
using enum ComputerConfig::Fix;

static const struct
{
  int x;
  int y;
  const char *name;
} neighbourDirs[] = {
    {1, 0, "right"},
    {-1, 0, "left"},
    {0, -1, "up"},
    {0, 1, "down"},

};

const int serverDefaultIndex = 7;

ServerConfig::ServerConfig(int columns, int rows) : m_computers(columns), m_columns(columns), m_rows(rows)
{
  recall();
}

bool ServerConfig::save(const QString &fileName) const
{
  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    return false;

  save(file);
  file.close();

  return true;
}

bool ServerConfig::operator==(const ServerConfig &sc) const
{
  return m_computers == sc.m_computers && m_Hotkeys == sc.m_Hotkeys && m_columns == sc.m_columns &&
         m_rows == sc.m_rows && m_screenAlignments == sc.m_screenAlignments;
}

void ServerConfig::save(QFile &file) const
{
  QTextStream outStream(&file);
  outStream << *this;
}

void ServerConfig::setupComputers()
{
  computers().clear();
  hotkeys().clear();
  m_screenAlignments.clear();

  // There must always be computer objects for each cell in the computers QList.
  // Unused computers are identified by having an empty name.
  for (int i = 0; i < m_columns * m_rows; i++)
    addComputer(Computer());
}

void ServerConfig::commit()
{
  qDebug("committing server config");
  reconcileScreenAlignments();

  settings().beginGroup("internalConfig");
  settings().remove("");

  settings().beginWriteArray("screens");
  for (int i = 0; i < computers().size(); i++) {
    settings().setArrayIndex(i);
    const auto &computer = computers()[i];
    computer.saveSettings(settings());
    auto computerName = Settings::value(Settings::Core::ComputerName).toString();
    if (computer.isServer() && computerName != computer.name()) {
      Settings::setValue(Settings::Core::ComputerName, computer.name());
    }
  }
  settings().endArray();

  settings().beginWriteArray("hotkeys");
  for (int i = 0; i < hotkeys().size(); i++) {
    settings().setArrayIndex(i);
    hotkeys()[i].saveSettings(settings().get());
  }
  settings().endArray();

  settings().beginWriteArray("screenAlignments");
  for (int i = 0; i < m_screenAlignments.size(); ++i) {
    settings().setArrayIndex(i);
    const auto &alignment = m_screenAlignments[i];
    settings().setValue("version", 1);
    settings().setValue("firstName", alignment.firstName);
    settings().setValue("secondName", alignment.secondName);
    settings().setValue("orientation", alignment.orientation == Qt::Horizontal ? "horizontal" : "vertical");
    settings().setValue("enabled", alignment.enabled);
    const auto decimal = [](double value) {
      return QString::number(value, 'g', std::numeric_limits<double>::max_digits10);
    };
    settings().setValue("firstWidthMm", decimal(alignment.firstSizeMm.width()));
    settings().setValue("firstHeightMm", decimal(alignment.firstSizeMm.height()));
    settings().setValue("secondWidthMm", decimal(alignment.secondSizeMm.width()));
    settings().setValue("secondHeightMm", decimal(alignment.secondSizeMm.height()));
    settings().setValue("offsetMm", decimal(alignment.offsetMm));
  }
  settings().endArray();

  settings().endGroup();
}

void ServerConfig::recall()
{
  qDebug("recalling server config");

  settings().beginGroup("internalConfig");

  m_columns = Settings::value(Settings::Server::GridWidth).toInt();
  m_rows = Settings::value(Settings::Server::GridHeight).toInt();

  // we need to know the number of columns and rows before we can set up
  // ourselves
  setupComputers();

  int numComputers = settings().beginReadArray("screens");
  Q_ASSERT(numComputers <= computers().size());
  for (int i = 0; i < numComputers; i++) {
    settings().setArrayIndex(i);
    computers()[i].loadSettings(settings());
    if (getServerName() == computers()[i].name()) {
      computers()[i].markAsServer();
    }
  }
  settings().endArray();

  int numHotkeys = settings().beginReadArray("hotkeys");
  for (int i = 0; i < numHotkeys; i++) {
    settings().setArrayIndex(i);
    Hotkey h;
    h.loadSettings(settings().get());
    hotkeys().append(h);
  }
  settings().endArray();

  const int numAlignments = settings().beginReadArray("screenAlignments");
  for (int i = 0; i < numAlignments; ++i) {
    settings().setArrayIndex(i);
    const auto orientation = settings().value("orientation").toString();
    if (settings().value("version").toInt() != 1 || (orientation != "horizontal" && orientation != "vertical"))
      continue;
    ScreenAlignment alignment;
    alignment.firstName = settings().value("firstName").toString();
    alignment.secondName = settings().value("secondName").toString();
    alignment.orientation = orientation == "horizontal" ? Qt::Horizontal : Qt::Vertical;
    alignment.enabled = settings().value("enabled", false).toBool();
    bool numericValuesValid = true;
    const auto readNumber = [this, &numericValuesValid](const QString &key) {
      bool ok = false;
      const double value = settings().value(key).toDouble(&ok);
      numericValuesValid = numericValuesValid && ok;
      return value;
    };
    alignment.firstSizeMm = {readNumber("firstWidthMm"), readNumber("firstHeightMm")};
    alignment.secondSizeMm = {readNumber("secondWidthMm"), readNumber("secondHeightMm")};
    alignment.offsetMm = readNumber("offsetMm");
    if (numericValuesValid && alignment.intervals())
      m_screenAlignments.append(alignment);
    else
      qWarning("ignoring invalid physical screen alignment metadata");
  }
  settings().endArray();

  settings().endGroup();
  reconcileScreenAlignments();
}

int ServerConfig::adjacentComputerIndex(int idx, int deltaColumn, int deltaRow) const
{
  if (computers()[idx].isNull())
    return -1;

  // if we're at the left or right end of the table, don't find results going
  // further left or right
  if ((deltaColumn > 0 && (idx + 1) % m_columns == 0) || (deltaColumn < 0 && idx % m_columns == 0))
    return -1;

  int arrayPos = idx + deltaColumn + deltaRow * m_columns;

  if (arrayPos >= computers().size() || arrayPos < 0)
    return -1;

  return arrayPos;
}

QList<ScreenAlignmentPair> ServerConfig::adjacentPairs() const
{
  QList<ScreenAlignmentPair> pairs;
  if (m_columns <= 0)
    return pairs;

  QHash<QString, int> nameCounts;
  for (const auto &computer : m_computers) {
    if (!computer.isNull())
      ++nameCounts[computer.name().toCaseFolded()];
  }
  for (int i = 0; i < m_computers.size(); ++i) {
    const auto &first = m_computers[i];
    if (first.isNull() || nameCounts.value(first.name().toCaseFolded()) != 1)
      continue;
    for (const auto orientation : {Qt::Horizontal, Qt::Vertical}) {
      const int neighbor = adjacentComputerIndex(i, orientation == Qt::Horizontal, orientation == Qt::Vertical);
      if (neighbor < 0)
        continue;
      const auto &second = m_computers[neighbor];
      if (!second.isNull() && nameCounts.value(second.name().toCaseFolded()) == 1)
        pairs.append({first.name(), second.name(), orientation});
    }
  }
  return pairs;
}

std::optional<ScreenAlignment> ServerConfig::screenAlignment(const ScreenAlignmentPair &pair) const
{
  if (!adjacentPairs().contains(pair))
    return std::nullopt;
  const auto entry = std::ranges::find_if(m_screenAlignments, [&pair](const ScreenAlignment &alignment) {
    return alignment.pair() == pair && alignment.intervals().has_value();
  });
  if (entry == m_screenAlignments.cend())
    return std::nullopt;
  return *entry;
}

bool ServerConfig::setScreenAlignment(const ScreenAlignment &alignment)
{
  if (!alignment.intervals() || !adjacentPairs().contains(alignment.pair()))
    return false;
  clearScreenAlignment(alignment.pair());
  m_screenAlignments.append(alignment);
  reconcileScreenAlignments();
  return true;
}

bool ServerConfig::clearScreenAlignment(const ScreenAlignmentPair &pair)
{
  return m_screenAlignments.removeIf([&pair](const ScreenAlignment &alignment) { return alignment.pair() == pair; }) >
         0;
}

void ServerConfig::reconcileScreenAlignments()
{
  QList<ScreenAlignment> valid;
  for (const auto &pair : adjacentPairs()) {
    if (const auto alignment = screenAlignment(pair))
      valid.append(*alignment);
  }
  m_screenAlignments = valid;
}

QTextStream &operator<<(QTextStream &outStream, const ServerConfig &config)
{
  outStream << "section: links" << Qt::endl;

  for (int i = 0; const auto &computer : config.computers()) {
    if (!computer.isNull()) {
      outStream << "\t" << computer.name() << ":\n";
      for (const auto &neighbour : std::as_const(neighbourDirs)) {
        int idx = config.adjacentComputerIndex(i, neighbour.x, neighbour.y);
        if (idx == -1 || config.computers()[idx].isNull())
          continue;
        const auto &destination = config.computers()[idx];
        const bool forward = neighbour.x > 0 || neighbour.y > 0;
        const ScreenAlignmentPair pair{
            forward ? computer.name() : destination.name(), forward ? destination.name() : computer.name(),
            neighbour.x != 0 ? Qt::Horizontal : Qt::Vertical
        };
        const auto alignment = config.screenAlignment(pair);
        if (!alignment || !alignment->enabled) {
          outStream << "\t\t" << neighbour.name << " = " << destination.name() << Qt::endl;
          continue;
        }
        const auto intervals = alignment->intervals();
        if (!intervals || !intervals->hasOverlap)
          continue;
        const auto rangeText = [](const ScreenAlignmentRange &range) {
          return QStringLiteral("(%1,%2)").arg(
              QString::number(range.startPercent, 'g', std::numeric_limits<double>::max_digits10),
              QString::number(range.endPercent, 'g', std::numeric_limits<double>::max_digits10)
          );
        };
        outStream << "\t\t" << neighbour.name << rangeText(forward ? intervals->first : intervals->second) << " = "
                  << destination.name() << rangeText(forward ? intervals->second : intervals->first) << Qt::endl;
      }
    }
    i++;
  }

  outStream << "end" << Qt::endl << Qt::endl;

  outStream << "section: options" << Qt::endl;

  for (const Hotkey &hotkey : config.hotkeys())
    outStream << hotkey;

  outStream << "end" << Qt::endl << Qt::endl;

  return outStream;
}

int ServerConfig::numComputers() const
{
  int rval = 0;

  for (const Computer &s : computers()) {
    if (!s.isNull())
      rval++;
  }

  return rval;
}

QString ServerConfig::getServerName() const
{
  return Settings::value(Settings::Core::ComputerName).toString();
}

void ServerConfig::updateServerName()
{
  for (auto &computer : computers()) {
    if (computer.isServer()) {
      computer.setName(Settings::value(Settings::Core::ComputerName).toString());
      reconcileScreenAlignments();
      break;
    }
  }
}

QString ServerConfig::configFile() const
{
  return Settings::value(Settings::Server::ExternalConfigFile).toString();
}

bool ServerConfig::useExternalConfig() const
{
  return Settings::value(Settings::Server::ExternalConfig).toBool();
}

bool ServerConfig::isFull() const
{
  bool isFull = true;

  for (const auto &computer : computers()) {
    if (computer.isNull()) {
      isFull = false;
      break;
    }
  }

  return isFull;
}

bool ServerConfig::computerExists(const QString &computerName) const
{
  bool isExists = false;

  for (const auto &computer : computers()) {
    if (!computer.isNull() && computer.name() == computerName) {
      isExists = true;
      break;
    }
  }

  return isExists;
}

void ServerConfig::addClient(const QString &clientName)
{
  int serverIndex = -1;
  const auto computerName = Settings::value(Settings::Core::ComputerName).toString();

  if (findComputerName(computerName, serverIndex)) {
    m_computers[serverIndex].markAsServer();
  } else {
    fixNoServer(computerName, serverIndex);
  }

  m_computers.addComputerByPriority(Computer(clientName));
  reconcileScreenAlignments();
}

void ServerConfig::setConfigFile(const QString &configFile) const
{
  Settings::setValue(Settings::Server::ExternalConfigFile, configFile);
}

void ServerConfig::setUseExternalConfig(bool useExternalConfig) const
{
  Settings::setValue(Settings::Server::ExternalConfig, useExternalConfig);
}

bool ServerConfig::findComputerName(const QString &name, int &index)
{
  bool found = false;
  for (int i = 0; i < computers().size(); i++) {
    if (!computers()[i].isNull() && computers()[i].name().compare(name) == 0) {
      index = i;
      found = true;
      break;
    }
  }
  return found;
}

bool ServerConfig::fixNoServer(const QString &name, int &index)
{
  bool fixed = false;
  if (computers()[serverDefaultIndex].isNull()) {
    m_computers[serverDefaultIndex].setName(name);
    m_computers[serverDefaultIndex].markAsServer();
    index = serverDefaultIndex;
    fixed = true;
  }

  return fixed;
}

QSettingsProxy &ServerConfig::settings()
{
  return Settings::proxy();
}
