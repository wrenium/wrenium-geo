// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "wrenium/f32math/trig.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/projection.h"

/// @file
/// The azimuthal *orthographic* radial-distance formula: renders the sphere
/// as if viewed from infinitely far away, centered on the projection
/// center -- the disk edge (centralAngle == kHalfPi) is exactly the
/// horizon, and points beyond it fold back inward, so this only makes
/// sense clipped at clipRadiusRad <= kHalfPi. Reuses rotate()/RotationFrame
/// (rotation.h) unchanged, same as equidistant.h -- this is the other half
/// of the split that file's own comment describes, differing from it only
/// in how rotatedPoint's centralAngle maps to a planar radius.

namespace wrenium::geo::azimuthal {

/// Forward spherical azimuthal orthographic projection of an already-rotated
/// point.
/// @param rotatedPoint A point already re-expressed via rotate() (detail/azimuthal/rotation.h).
/// @param scale Output units per kilometer -- projected radius is
/// kEarthRadiusKm * sin(centralAngle) * @p scale, reaching its maximum
/// (kEarthRadiusKm * scale) exactly at the horizon, centralAngle == kHalfPi.
/// @return The planar (x, y) projection.
inline Point projectOrthographic(const GeoPoint &rotatedPoint, float scale)
{
    const float centralAngle = kHalfPi - rotatedPoint.latRad;
    const float bearing = rotatedPoint.lonRad;

    const float radius = kEarthRadiusKm * f32math::sin(centralAngle) * scale;

    // Same north-up, compass convention as equidistant.h's project().
    float sinBearing, cosBearing;
    f32math::sincos(bearing, sinBearing, cosBearing);

    Point projected;
    projected.x = radius * sinBearing;
    projected.y = -radius * cosBearing;
    return projected;
}

} // namespace wrenium::geo::azimuthal
