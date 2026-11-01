/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenAlignment.h"

#include <algorithm>
#include <cmath>

ScreenAlignmentPair ScreenAlignment::pair() const
{
  return {firstName, secondName, orientation};
}

bool ScreenAlignment::isValid() const
{
  const auto positive = [](double value) { return std::isfinite(value) && value > 0; };
  return !firstName.trimmed().isEmpty() && !secondName.trimmed().isEmpty() &&
         firstName.compare(secondName, Qt::CaseInsensitive) != 0 &&
         (orientation == Qt::Horizontal || orientation == Qt::Vertical) && positive(firstSizeMm.width()) &&
         positive(firstSizeMm.height()) && positive(secondSizeMm.width()) && positive(secondSizeMm.height()) &&
         std::isfinite(offsetMm);
}

std::optional<ScreenAlignmentIntervals> ScreenAlignment::intervals() const
{
  if (!isValid())
    return std::nullopt;

  const double firstExtent = orientation == Qt::Horizontal ? firstSizeMm.height() : firstSizeMm.width();
  const double secondExtent = orientation == Qt::Horizontal ? secondSizeMm.height() : secondSizeMm.width();
  const double secondEnd = offsetMm + secondExtent;
  if (!std::isfinite(secondEnd))
    return std::nullopt;

  const double start = std::max(0.0, offsetMm);
  const double end = std::min(firstExtent, secondEnd);
  ScreenAlignmentIntervals result;
  if (end <= start)
    return result;

  // Intersected coordinates are mathematically inside each screen; decimal
  // dimensions can still produce 100.00000000000004 at an exact endpoint.
  const auto percent = [](double position, double extent) { return 100 * std::clamp(position / extent, 0.0, 1.0); };
  result.first = {percent(start, firstExtent), percent(end, firstExtent)};
  result.second = {percent(start - offsetMm, secondExtent), percent(end - offsetMm, secondExtent)};
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
