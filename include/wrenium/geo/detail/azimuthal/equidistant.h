// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cmath>

#include "wrenium/f32math/atan2.h"
#include "wrenium/f32math/trig.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/projection.h"

/// @file
/// The azimuthal *equidistant* radial-distance formula specifically: turns
/// an already-rotated point (detail/azimuthal/rotation.h) into a planar
/// (x, y) where radius is directly proportional to true angular distance
/// from center, and back. This is the one piece that's specific to this
/// particular azimuthal variant -- a different one (stereographic,
/// orthographic, equal-area, ...) would replace just projectEquidistant()/
/// unprojectEquidistant() with its own radial formula (and that formula's
/// own inverse) and reuse rotation.h unchanged.

namespace wrenium::geo::azimuthal {

/// Forward spherical azimuthal equidistant projection of an already-rotated
/// point.
/// @param rotatedPoint A point already re-expressed via rotate() (detail/azimuthal/rotation.h).
/// @param scale Output units per kilometer -- projected radius is exactly
/// distanceKm * scale, so a caller can reproduce this same formula for
/// range rings, ticks, etc. using the same @p scale value.
/// @return The planar (x, y) projection.
inline Point projectEquidistant(const GeoPoint &rotatedPoint, float scale)
{
    const float centralAngle = kHalfPi - rotatedPoint.latRad;
    const float bearing = rotatedPoint.lonRad;

    const float distanceKm = centralAngle * kEarthRadiusKm;
    const float radius = distanceKm * scale;

    // North-up, compass convention: bearing 0 (north) -> (0, -radius) (up
    // the screen, since SVG/screen y increases downward); bearing pi/2
    // (east) -> (radius, 0) (right).
    float sinBearing, cosBearing;
    f32math::sincos(bearing, sinBearing, cosBearing);

    Point projected;
    projected.x = radius * sinBearing;
    projected.y = -radius * cosBearing;
    return projected;
}

/// Inverse of projectEquidistant(): recovers the rotated-frame point (still
/// relative to whatever center rotate() used, see azimuthal_pipeline.h's
/// unproject()) a planar point came from.
/// @param point A planar point in projectEquidistant()'s own output space.
/// @param scale Output units per kilometer -- must match whatever
/// projectEquidistant() was called with, or the recovered distance is wrong.
/// @return The rotated-frame point (feed to unrotate() to recover raw
/// (lat, lon)).
inline GeoPoint unprojectEquidistant(const Point &point, float scale) // NOLINT(bugprone-easily-swappable-parameters)
{
    const float radius = std::sqrt(point.x * point.x + point.y * point.y);
    const float distanceKm = radius / scale;
    const float centralAngle = distanceKm / kEarthRadiusKm;

    // Inverse of projectEquidistant()'s own north-up, compass convention
    // (x = radius*sin(bearing), y = -radius*cos(bearing)).
    const float bearing = f32math::atan2(point.x, -point.y);

    GeoPoint rotated;
    rotated.latRad = kHalfPi - centralAngle;
    rotated.lonRad = bearing;
    return rotated;
}

} // namespace wrenium::geo::azimuthal
