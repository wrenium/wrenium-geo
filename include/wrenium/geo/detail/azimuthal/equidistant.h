// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#ifndef WRENIUM_GEO_DETAIL_AZIMUTHAL_EQUIDISTANT_H
#define WRENIUM_GEO_DETAIL_AZIMUTHAL_EQUIDISTANT_H

#include <cmath>

#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/projection.h"

/// @file
/// The azimuthal *equidistant* radial-distance formula specifically: turns
/// an already-rotated point (detail/azimuthal/rotation.h) into a planar
/// (x, y) where radius is directly proportional to true angular distance
/// from center. This is the one piece that's specific to this particular
/// azimuthal variant -- a different one (stereographic, orthographic,
/// equal-area, ...) would replace just project() with its own radial
/// formula and reuse rotation.h unchanged.

namespace wrenium::geo {

/// Forward spherical azimuthal equidistant projection of an already-rotated
/// point.
/// @param rotatedPoint A point already re-expressed via rotate() (detail/azimuthal/rotation.h).
/// @param scale Output units per kilometer -- projected radius is exactly
/// distanceKm * scale, so a caller can reproduce this same formula for
/// range rings, ticks, etc. using the same @p scale value.
/// @return The planar (x, y) projection.
inline Point project(const GeoPoint &rotatedPoint, float scale)
{
    const float centralAngle = kHalfPi - rotatedPoint.latRad;
    const float bearing = rotatedPoint.lonRad;

    const float distanceKm = centralAngle * kEarthRadiusKm;
    const float radius = distanceKm * scale;

    // North-up, compass convention: bearing 0 (north) -> (0, -radius) (up
    // the screen, since SVG/screen y increases downward); bearing pi/2
    // (east) -> (radius, 0) (right).
    Point projected;
    projected.x = radius * sinf(bearing);
    projected.y = -radius * cosf(bearing);
    return projected;
}

} // namespace wrenium::geo

#endif // WRENIUM_GEO_DETAIL_AZIMUTHAL_EQUIDISTANT_H
