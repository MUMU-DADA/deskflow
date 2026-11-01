/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerConfigTests.h"

#include "server/Config.h"

#include <limits>
#include <sstream>

class OnlySystemFilter : public InputFilter::Condition
{
public:
  Condition *clone() const override
  {
    return new OnlySystemFilter();
  }
  std::string format() const override
  {
    return "";
  }

  InputFilter::FilterStatus match(const Event &ev) override
  {
    return ev.getType() == EventTypes::System ? InputFilter::FilterStatus::Activate
                                              : InputFilter::FilterStatus::NoMatch;
  }
};

using namespace deskflow::server;

void ServerConfigTests::equalityCheck()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(a != b);

  QVERIFY(b.addComputer("computerB"));
  QVERIFY(a != b);

  QVERIFY(a.addComputer("computerB"));
  QVERIFY(a.addComputer("computerC"));
  QVERIFY(a.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(a.connect("computerB", Direction::Left, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(b.addComputer("computerC"));
  QVERIFY(b.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(b.connect("computerB", Direction::Left, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(a.addOption("computerA", kOptionClipboardSharing, 1));
  QVERIFY(b.addOption("computerA", kOptionClipboardSharing, 1));
  QVERIFY(a.addOption(std::string(), kOptionClipboardSharing, 1));
  QVERIFY(b.addOption(std::string(), kOptionClipboardSharing, 1));

  a.getInputFilter()->addFilterRule(InputFilter::Rule{new OnlySystemFilter()});
  b.getInputFilter()->addFilterRule(InputFilter::Rule{new OnlySystemFilter()});
  QVERIFY(a.addAlias("computerA", "aliasA"));
  QVERIFY(b.addAlias("computerA", "aliasA"));
  /* TODO Fix linking to the proper libs
  NetworkAddress addr1("localhost", 8080);
  addr1.resolve();
  NetworkAddress addr2("localhost", 8080);
  addr2.resolve();
  a.setDeskflowAddress(addr1);
  b.setDeskflowAddress(addr2);
  */
  QVERIFY(a == b);
}

void ServerConfigTests::equalityCheck_diff_options()
{
  Config a(nullptr);
  Config b(nullptr);

  QVERIFY(a.addComputer("computerA"));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(a.addOption("computerA", kOptionClipboardSharing, 0));
  QVERIFY(b.addOption("computerA", kOptionClipboardSharing, 1));
  QVERIFY(a != b);
}

void ServerConfigTests::equalityCheck_diff_alias()
{
  Config a(nullptr);
  Config b(nullptr);

  QVERIFY(a.addComputer("computerA"));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(b.addAlias("computerA", "aliasA"));
  QVERIFY(a != b);

  QVERIFY(a.addAlias("computerA", "aliasA"));
  QVERIFY(b.addAlias("computerA", "aliasB"));
  QVERIFY(a != b);
}

void ServerConfigTests::equalityCheck_diff_filters()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(b.addComputer("computerA"));

  a.getInputFilter()->addFilterRule(InputFilter::Rule{new OnlySystemFilter()});
  QVERIFY(a != b);
}

// TODO FIX
/*
void ServerConfigTests::equalityCheck_diff_address()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(b.addComputer("computerA"));
  a.setDeskflowAddress(NetworkAddress(8000));
  b.setDeskflowAddress(NetworkAddress(9000));
  QVERIFY(a != b);
}
*/

void ServerConfigTests::equalityCheck_diff_neighbours1()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(a.addComputer("computerB"));
  QVERIFY(a.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(b.addComputer("computerB"));
  QVERIFY(a != b);
  QVERIFY(b != a);
}

void ServerConfigTests::equalityCheck_diff_neighbours2()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(a.addComputer("computerB"));
  QVERIFY(a.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(b.addComputer("computerB"));
  QVERIFY(b.connect("computerA", Direction::Bottom, 0.0f, 0.25f, "computerB", 0.25f, 1.0f));
  QVERIFY(a != b);
}

void ServerConfigTests::equalityCheck_diff_neighbours3()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(a.addComputer("computerB"));
  QVERIFY(a.addComputer("computerC"));
  QVERIFY(a.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(b.addComputer("computerB"));
  QVERIFY(b.addComputer("computerC"));
  QVERIFY(b.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerC", 0.5f, 1.0f));
  QVERIFY(a != b);
}

void ServerConfigTests::decimalIntervalsRoundTrip_data()
{
  QTest::addColumn<QString>("start");
  QTest::addColumn<QString>("end");
  QTest::newRow("default") << "0" << "100";
  QTest::newRow("integer") << "25" << "75";
  QTest::newRow("fractional") << "12.3456789" << "87.654321";
  QTest::newRow("narrow-upper-edge") << "99.99999" << "100";
  QTest::newRow("adjacent-floats") << "50" << "50.000005";
  QTest::newRow("near-zero") << "0" << "0.0000001";
  QTest::newRow("subnormal") << "1e-40" << "2e-40";
}

void ServerConfigTests::decimalIntervalsRoundTrip()
{
  QFETCH(QString, start);
  QFETCH(QString, end);
  std::istringstream empty;
  ConfigReadContext context(empty);
  const auto expected = context.parseInterval({start.toStdString(), end.toStdString()});

  Config config(nullptr);
  QVERIFY(config.addComputer("computerA"));
  QVERIFY(config.addComputer("computerB"));
  QVERIFY(config.connect(
      "computerA", Direction::Right, expected.first, expected.second, "computerB", expected.first, expected.second
  ));
  std::ostringstream serialized;
  serialized << config;

  // Exercise the real config serializer and the same name/interval parser used
  // by readSectionLinks, without resolving the unrelated options address.
  std::istringstream input(serialized.str());
  ConfigReadContext reader(input);
  std::string line;
  bool foundLink = false;
  while (reader.readLine(line)) {
    if (!line.starts_with("right"))
      continue;
    foundLink = true;
    std::string name;
    ConfigReadContext::ArgList args;
    std::string::size_type index = 0;
    reader.parseNameWithArgs("link", line, "=", index, name, args);
    QCOMPARE(name, "right");
    const auto source = reader.parseInterval(args);
    QVERIFY(source == expected);
    ++index;
    reader.parseNameWithArgs("computer", line, "", index, name, args);
    QCOMPARE(name, "computerB");
    const auto destination = reader.parseInterval(args);
    QVERIFY(destination == expected);
    if (start == "0" && end == "100")
      QCOMPARE(line, "right = computerB");
    if (start == "25" && end == "75")
      QCOMPARE(line, "right(25,75) = computerB(25,75)");
  }
  QVERIFY(foundLink);
}

void ServerConfigTests::invalidIntervals_data()
{
  QTest::addColumn<QString>("start");
  QTest::addColumn<QString>("end");
  QTest::newRow("nan-start") << "nan" << "100";
  QTest::newRow("nan-end") << "0" << "nan";
  QTest::newRow("infinity") << "0" << "inf";
  QTest::newRow("negative-infinity") << "-inf" << "100";
  QTest::newRow("overflow") << "0" << "1e999";
  QTest::newRow("junk") << "0" << "10x";
  QTest::newRow("negative") << "-0.1" << "100";
  QTest::newRow("above-range") << "0" << "100.1";
  QTest::newRow("equal") << "50" << "50";
  QTest::newRow("reversed") << "51" << "50";
  QTest::newRow("collapsed-middle") << "50" << "50.00000001";
  QTest::newRow("collapsed-upper-edge") << "99.9999999" << "100";
  QTest::newRow("collapsed-underflow") << "1e-50" << "2e-50";
}

void ServerConfigTests::invalidIntervals()
{
  QFETCH(QString, start);
  QFETCH(QString, end);
  std::istringstream input;
  ConfigReadContext context(input);
  const ConfigReadContext::ArgList args{start.toStdString(), end.toStdString()};
  QVERIFY_EXCEPTION_THROWN(context.parseInterval(args), ServerConfigReadException);
}

void ServerConfigTests::connectRejectsInvalidIntervals()
{
  Config config(nullptr);
  QVERIFY(config.addComputer("computerA"));
  QVERIFY(config.addComputer("computerB"));
  const Config::Interval invalid[] = {
      {std::numeric_limits<float>::quiet_NaN(), 1},
      {0, std::numeric_limits<float>::infinity()},
      {-0.1f, 1},
      {0, 1.1f},
      {0.5f, 0.5f},
      {1, 0}
  };
  for (const auto &[start, end] : invalid) {
    QVERIFY(!config.connect("computerA", Direction::Right, start, end, "computerB", 0, 1));
    QVERIFY(!config.connect("computerA", Direction::Right, 0, 1, "computerB", start, end));
  }
  QVERIFY(config.connect("computerA", Direction::Right, 0, 1, "computerB", 0, 1));
}

QTEST_MAIN(ServerConfigTests)
