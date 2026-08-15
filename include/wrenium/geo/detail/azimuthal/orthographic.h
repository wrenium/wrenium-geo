// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cmath>

#include "wrenium/f32math/asin.h"
#include "wrenium/f32math/atan2.h"
#include "wrenium/f32math/trig.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/projection.h"

/// @file
/// The azimuthal *orthographic* radial-distance formula: renders the sphere
/// as if viewed from infinitely far away, centered on the projection
/// center -- the disk edge (centralAngle == kHalfPi) is exactly the
/// horizon, and points beyond it fold back inward, so this only makes
/// sense clipped at clipRadiusRad <= kHalfPi. Reuses rotate()/unrotate()/
/// RotationFrame (rotation.h) unchanged, same as equidistant.h -- this is
/// the other half of the split that file's own comment describes,
/// differing from it only in how rotatedPoint's centralAngle maps to a
/// planar radius, and back.

namespace wrenium::geo::azimuthal {

/// Forward spherical azimuthal orthographic projection of an already-rotated
/// point.
/// @param rotatedPoint A point already re-expressed via rotate() (detail/azimuthal/rotation.h).
/// @param scale Output units per kilometer -- projected radius is
/// kEarthRadiusKm * sin(centralAngle) * @p scale, reaching its maximum
/// (kEarthRadiusKm * scale) exactly at the horizon, centralAngle == kHalfPi.
/// @return The planar (x, y) projection.
constexpr Point projectOrthographic(const GeoPoint &rotatedPoint, float scale)
{
    const float centralAngle = kHalfPi - rotatedPoint.latRad;
    const float bearing = rotatedPoint.lonRad;

    const float radius = kEarthRadiusKm * f32math::sin(centralAngle) * scale;

    // Same north-up, compass convention as equidistant.h's projectEquidistant().
    float sinBearing = 0.0f, cosBearing = 0.0f;
    f32math::sincos(bearing, sinBearing, cosBearing);

    Point projected;
    projected.x = radius * sinBearing;
    projected.y = -radius * cosBearing;
    return projected;
}

/// Inverse of projectOrthographic(): recovers the rotated-frame point
/// (still relative to whatever center rotate() used, see
/// azimuthal_pipeline.h's unproject()) a planar point came from.
/// sin(centralAngle) is clamped to <= 1 before asin() as a
/// float-precision safety margin -- a point exactly at the horizon can
/// round to a hair past kEarthRadiusKm * scale, and a point *past* the
/// horizon (clicked outside the rendered disk) saturates to the horizon
/// itself instead of hitting asin()'s undefined behavior outside its
/// domain.
/// @param point A planar point in projectOrthographic()'s own output space.
/// @param scale Output units per kilometer -- must match whatever
/// projectOrthographic() was called with, or the recovered distance is
/// wrong.
/// @return The rotated-frame point (feed to unrotate() to recover raw
/// (lat, lon)).
inline GeoPoint unprojectOrthographic(const Point &point, float scale) // NOLINT(bugprone-easily-swappable-parameters)
{
    const float radius = std::sqrt(point.x * point.x + point.y * point.y);
    float sinCentralAngle = radius / (kEarthRadiusKm * scale);
    if (sinCentralAngle > 1.0f) {
        sinCentralAngle = 1.0f;
    }
    const float centralAngle = f32math::asin(sinCentralAngle);

    // Inverse of projectOrthographic()'s own north-up, compass convention
    // (x = radius*sin(bearing), y = -radius*cos(bearing)).
    const float bearing = f32math::atan2(point.x, -point.y);

    GeoPoint rotated;
    rotated.latRad = kHalfPi - centralAngle;
    rotated.lonRad = bearing;
    return rotated;
}

} // namespace wrenium::geo::azimuthal
