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

// One color's matching positions, measured from the top (side-by-side) or
// left (stacked) along each complete edge. Coordinates are dimensionless.
struct ScreenEdgePoint
{
  double first = 0;
  double second = 0;

  bool operator==(const ScreenEdgePoint &) const = default;
};

struct ScreenEdgeTransform
{
  // In units of the first edge's length: first = offset + ratio * second.
  double ratio = 1;
  double offset = 0;

  bool operator==(const ScreenEdgeTransform &) const = default;
};

struct ScreenEdgeCalibration
{
  enum class Error
  {
    None,
    NonFinite,
    OutOfRange,
    PointsTooClose,
    Reversed,
    InvalidAspectRatio
  };

  ScreenEdgePoint a{0.2, 0.2};
  ScreenEdgePoint b{0.8, 0.8};
  // Width / height, used only to draw a schematic preview. These do not
  // contribute to the edge calibration or imply measured physical dimensions.
  double firstAspectRatio = 16.0 / 9.0;
  double secondAspectRatio = 16.0 / 9.0;

  static constexpr double minimumPointSeparation = 0.1;
  Error error() const;
  std::optional<ScreenEdgeTransform> transform() const;
  // Signed discrepancy in units of the first edge's length. A check point
  // never changes the transform derived from a and b.
  std::optional<double> validationResidual(const ScreenEdgePoint &point) const;
  bool operator==(const ScreenEdgeCalibration &) const = default;
};

struct ScreenAlignmentVisualGeometry
{
  QSizeF firstSize;
  QSizeF secondSize;
  double offset = 0;
  bool relative = false; // When true, sizes and offset have no physical unit.
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
  // When present, calibration is authoritative. The manual millimeter fields
  // are unused and must not be displayed as measured calibration results.
  std::optional<ScreenEdgeCalibration> calibration;

  ScreenAlignmentPair pair() const;
  bool isValid() const;
  std::optional<ScreenAlignmentVisualGeometry> visualGeometry() const;
  // nullopt means invalid/unrepresentable geometry. A valid disjoint pair has hasOverlap=false.
  // Calculation is independent of enabled, so a preview can display a disabled draft.
  std::optional<ScreenAlignmentIntervals> intervals() const;
  bool operator==(const ScreenAlignment &) const;
};
