// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>

#include "wrenium/geo/detail/angle.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"

/// @file
/// contains(): whether a point on the sphere falls inside a set of closed
/// rings -- independent of any projection, rotation, or clip radius, so it
/// works the same regardless of what center/scale a caller might otherwise
/// be projecting the same rings with, or whether they're projecting them
/// at all.

namespace wrenium::geo {

/// True if @p point is enclosed by an odd number of @p geometry's rings
/// (the even-odd fill rule the SVG/binary output already uses, so this
/// agrees with what actually gets drawn). Standard even-odd ray-casting
/// point-in-polygon test, ray cast due north from @p point.
///
/// Tested directly in raw (lat, lon) space, not by rotating/projecting
/// first -- projecting is numerically unstable for a point near the
/// projection center's antipode, which would corrupt the ray-cast parity.
/// @tparam MaxPoints Capacity of @p geometry's own points.
/// @tparam MaxRings Capacity of @p geometry's own ring sizes.
/// @param geometry The rings to test against.
/// @param point The point to test.
/// @return True iff @p point is enclosed by an odd number of @p geometry's rings.
template <std::size_t MaxPoints, std::size_t MaxRings>
inline bool contains(const InputGeometry<MaxPoints, MaxRings> &geometry, const GeoPoint &point)
{
    using wrenium::geo::detail::wrapPi;

    bool inside = false;
    std::size_t offset = 0;

    for (std::size_t r = 0; r < geometry.ringSizes.size(); ++r) {
        const std::size_t ringSize = geometry.ringSizes[r];
        for (std::size_t step = 0; step < ringSize; ++step) {
            const GeoPoint &p0 = geometry.points[offset + step];
            const GeoPoint &p1 = geometry.points[offset + (step + 1) % ringSize];

            const float delta = wrapPi(p1.lonRad - p0.lonRad);
            if (delta == 0.0f) {
                continue;
            }
            const float pointDelta = wrapPi(point.lonRad - p0.lonRad);
            // Distance from p1 to point, computed directly (not as
            // delta - pointDelta) so a shared vertex can't be claimed by
            // both its adjacent edges -- two independently wrapPi()'d
            // deltas can disagree by a hair when p1 and point represent
            // the same meridian via different float values (e.g. -180 vs
            // +180).
            const float remainingDelta = wrapPi(p1.lonRad - point.lonRad);

            const bool crosses = (delta > 0.0f)
                ? (pointDelta >= 0.0f && remainingDelta > 0.0f)
                : (pointDelta <= 0.0f && remainingDelta < 0.0f);
            if (!crosses) {
                continue;
            }

            const float t = pointDelta / delta;
            const float crossingLat = p0.latRad + (p1.latRad - p0.latRad) * t;
            if (crossingLat > point.latRad) {
                inside = !inside;
            }
        }
        offset += ringSize;
    }

    return inside;
}

} // namespace wrenium::geo
