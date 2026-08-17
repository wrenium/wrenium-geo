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
/// The azimuthal *gnomonic* radial-distance formula: every great circle on
/// the sphere projects to a straight line on the plane, so any straight
/// line drawn on the output is the shortest path between the two points it
/// joins -- unlike equidistant.h/orthographic.h, where only lines through
/// the center have that property. Radius grows as tan(centralAngle), so it
/// diverges at the horizon (centralAngle == kHalfPi); only meaningful
/// clipped at clipRadiusRad < kHalfPi, strictly less than orthographic's
/// own <= kHalfPi limit. Reuses rotate()/unrotate()/RotationFrame
/// (rotation.h) unchanged, same as equidistant.h/orthographic.h.

namespace wrenium::geo::azimuthal {

/// Forward spherical gnomonic projection of an already-rotated point.
/// @param rotatedPoint A point already re-expressed via rotate() (detail/azimuthal/rotation.h).
/// @param scale Output units per kilometer -- projected radius is
/// kEarthRadiusKm * tan(centralAngle) * @p scale, growing without bound as
/// centralAngle approaches kHalfPi.
/// @return The planar (x, y) projection.
constexpr Point projectGnomonic(const GeoPoint &rotatedPoint, float scale)
{
    const float centralAngle = kHalfPi - rotatedPoint.latRad;
    const float bearing = rotatedPoint.lonRad;

    float sinAngle = 0.0f, cosAngle = 0.0f;
    f32math::sincos(centralAngle, sinAngle, cosAngle);
    const float radius = kEarthRadiusKm * (sinAngle / cosAngle) * scale;

    // Same north-up, compass convention as equidistant.h's projectEquidistant().
    float sinBearing = 0.0f, cosBearing = 0.0f;
    f32math::sincos(bearing, sinBearing, cosBearing);

    Point projected;
    projected.x = radius * sinBearing;
    projected.y = -radius * cosBearing;
    return projected;
}

/// Inverse of projectGnomonic(): recovers the rotated-frame point (still
/// relative to whatever center rotate() used, see azimuthal_pipeline.h's
/// unproject()) a planar point came from.
/// @param point A planar point in projectGnomonic()'s own output space.
/// @param scale Output units per kilometer -- must match whatever
/// projectGnomonic() was called with, or the recovered distance is wrong.
/// @return The rotated-frame point (feed to unrotate() to recover raw
/// (lat, lon)).
inline GeoPoint unprojectGnomonic(const Point &point, float scale) // NOLINT(bugprone-easily-swappable-parameters)
{
    const float radius = std::sqrt(point.x * point.x + point.y * point.y);
    const float centralAngle = f32math::atan2(radius, kEarthRadiusKm * scale);

    // Inverse of projectGnomonic()'s own north-up, compass convention
    // (x = radius*sin(bearing), y = -radius*cos(bearing)).
    const float bearing = f32math::atan2(point.x, -point.y);

    GeoPoint rotated;
    rotated.latRad = kHalfPi - centralAngle;
    rotated.lonRad = bearing;
    return rotated;
}

} // namespace wrenium::geo::azimuthal
