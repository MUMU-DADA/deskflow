/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QSizeF>
#include <QString>
#include <Qt>

#include <optional>

// Canonical order: first is left of second (Horizontal), or above it (Vertical).
// Names bind calibration to computers, never to reusable grid cell indices.
struct ScreenAlignmentPair
{
  QString firstName;
  QString secondName;
  Qt::Orientation orientation = Qt::Horizontal;

  bool operator==(const ScreenAlignmentPair &) const = default;
};

struct ScreenAlignmentRange
{
  double startPercent = 0;
  double endPercent = 100;

  bool operator==(const ScreenAlignmentRange &) const = default;
};

struct ScreenAlignmentIntervals
{
  ScreenAlignmentRange first;
  ScreenAlignmentRange second;
  bool hasOverlap = false;

  bool operator==(const ScreenAlignmentIntervals &) const = default;
};

struct ScreenAlignment
{
  QString firstName;
  QString secondName;
  Qt::Orientation orientation = Qt::Horizontal;
  QSizeF firstSizeMm;
  QSizeF secondSizeMm;
  // Second top relative to first top; for vertical adjacency, second left relative to first left.
  double offsetMm = 0;
  bool enabled = false;

  ScreenAlignmentPair pair() const;
  bool isValid() const;
  // nullopt means invalid/unrepresentable geometry. A valid disjoint pair has hasOverlap=false.
  // Calculation is independent of enabled, so a preview can display a disabled draft.
  std::optional<ScreenAlignmentIntervals> intervals() const;
  bool operator==(const ScreenAlignment &) const = default;
};
