/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenAlignment.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
bool finitePoint(const ScreenEdgePoint &point)
{
  return std::isfinite(point.first) && std::isfinite(point.second);
}

bool pointInsideEdge(const ScreenEdgePoint &point)
{
  return point.first >= 0 && point.first <= 1 && point.second >= 0 && point.second <= 1;
}
} // namespace

ScreenEdgeCalibration::Error ScreenEdgeCalibration::error() const
{
  if (!finitePoint(a) || !finitePoint(b))
    return Error::NonFinite;
  if (!pointInsideEdge(a) || !pointInsideEdge(b))
    return Error::OutOfRange;

  const double firstSpan = b.first - a.first;
  const double secondSpan = b.second - a.second;
  // Decimal positions such as 0.2 and 0.3 can subtract to just below 0.1.
  // Allow only floating-point representation noise, not a shorter baseline.
  constexpr double tolerance = 16 * std::numeric_limits<double>::epsilon();
  if (std::abs(firstSpan) + tolerance < minimumPointSeparation ||
      std::abs(secondSpan) + tolerance < minimumPointSeparation)
    return Error::PointsTooClose;
  if ((firstSpan < 0) != (secondSpan < 0))
    return Error::Reversed;

  const double ratio = firstSpan / secondSpan;
  const auto validAspect = [ratio](double aspect) {
    // Either orientation must produce positive finite schematic dimensions.
    return std::isfinite(aspect) && aspect > 0 && std::isfinite(1 / aspect) && std::isfinite(ratio * aspect) &&
           ratio * aspect > 0 && std::isfinite(ratio / aspect) && ratio / aspect > 0;
  };
  if (!validAspect(firstAspectRatio) || !validAspect(secondAspectRatio))
    return Error::InvalidAspectRatio;
  return Error::None;
}

std::optional<ScreenEdgeTransform> ScreenEdgeCalibration::transform() const
{
  if (error() != Error::None)
    return std::nullopt;
  const double ratio = (b.first - a.first) / (b.second - a.second);
  return ScreenEdgeTransform{ratio, std::fma(-ratio, a.second, a.first)};
}

std::optional<double> ScreenEdgeCalibration::validationResidual(const ScreenEdgePoint &point) const
{
  if (!finitePoint(point) || !pointInsideEdge(point))
    return std::nullopt;
  const auto fitted = transform();
  if (!fitted)
    return std::nullopt;
  return point.first - std::fma(fitted->ratio, point.second, fitted->offset);
}

ScreenAlignmentPair ScreenAlignment::pair() const
{
  return {firstName, secondName, orientation};
}

bool ScreenAlignment::operator==(const ScreenAlignment &other) const
{
  if (pair() != other.pair() || enabled != other.enabled || calibration != other.calibration)
    return false;
  // Relative calibrations do not persist the unused manual measurement fields.
  return calibration ||
         (firstSizeMm == other.firstSizeMm && secondSizeMm == other.secondSizeMm && offsetMm == other.offsetMm);
}

bool ScreenAlignment::isValid() const
{
  const auto positive = [](double value) { return std::isfinite(value) && value > 0; };
  if (firstName.trimmed().isEmpty() || secondName.trimmed().isEmpty() ||
      firstName.compare(secondName, Qt::CaseInsensitive) == 0 ||
      (orientation != Qt::Horizontal && orientation != Qt::Vertical))
    return false;
  if (calibration)
    return calibration->error() == ScreenEdgeCalibration::Error::None;
  return positive(firstSizeMm.width()) && positive(firstSizeMm.height()) && positive(secondSizeMm.width()) &&
         positive(secondSizeMm.height()) && std::isfinite(offsetMm);
}

std::optional<ScreenAlignmentVisualGeometry> ScreenAlignment::visualGeometry() const
{
  if (!isValid())
    return std::nullopt;
  if (!calibration)
    return ScreenAlignmentVisualGeometry{firstSizeMm, secondSizeMm, offsetMm, false};

  const auto fitted = calibration->transform();
  ScreenAlignmentVisualGeometry geometry;
  geometry.relative = true;
  geometry.offset = fitted->offset;
  if (orientation == Qt::Horizontal) {
    geometry.firstSize = {calibration->firstAspectRatio, 1};
    geometry.secondSize = {fitted->ratio * calibration->secondAspectRatio, fitted->ratio};
  } else {
    geometry.firstSize = {1, 1 / calibration->firstAspectRatio};
    geometry.secondSize = {fitted->ratio, fitted->ratio / calibration->secondAspectRatio};
  }
  return geometry;
}

std::optional<ScreenAlignmentIntervals> ScreenAlignment::intervals() const
{
  if (!isValid())
    return std::nullopt;

  double firstExtent;
  double secondExtent;
  double offset;
  if (calibration) {
    const auto fitted = calibration->transform();
    firstExtent = 1;
    secondExtent = fitted->ratio;
    offset = fitted->offset;
  } else {
    firstExtent = orientation == Qt::Horizontal ? firstSizeMm.height() : firstSizeMm.width();
    secondExtent = orientation == Qt::Horizontal ? secondSizeMm.height() : secondSizeMm.width();
    offset = offsetMm;
  }
  const double secondEnd = offset + secondExtent;
  if (!std::isfinite(secondEnd))
    return std::nullopt;

  const double start = std::max(0.0, offset);
  const double end = std::min(firstExtent, secondEnd);
  ScreenAlignmentIntervals result;
  if (end <= start)
    return result;

  // Intersected coordinates are mathematically inside each screen; decimal
  // dimensions can still produce 100.00000000000004 at an exact endpoint.
  const auto percent = [](double position, double extent) { return 100 * std::clamp(position / extent, 0.0, 1.0); };
  result.first = {percent(start, firstExtent), percent(end, firstExtent)};
  result.second = {percent(start - offset, secondExtent), percent(end - offset, secondExtent)};
  const auto usable = [](const ScreenAlignmentRange &range) {
    // The core stores normalized endpoints as float. Do not emit a range that
    // collapses there even though its double-precision physical overlap is positive.
    return std::isfinite(range.startPercent) && std::isfinite(range.endPercent) && range.startPercent >= 0 &&
           range.endPercent <= 100 && range.startPercent < range.endPercent &&
           static_cast<float>(range.startPercent / 100) < static_cast<float>(range.endPercent / 100);
  };
  if (!usable(result.first) || !usable(result.second))
    return std::nullopt;

  result.hasOverlap = true;
  return result;
}
