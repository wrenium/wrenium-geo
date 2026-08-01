// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "wrenium/geo/detail/azimuthal/rotation.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/projection.h"

/// @file
/// Great-circle distance and initial bearing between two arbitrary
/// points -- general spherical trig, not tied to any projection or
/// rendering pipeline. Built directly on azimuthal::rotate()'s own
/// already-verified atan2-based (Vincenty-style) formula
/// (detail/azimuthal/rotation.h): "distance and bearing from A to B" is
/// exactly what rotate(B, A) already computes as its rotated-frame
/// representation (rotatedLat = kHalfPi - centralAngle, rotatedLon =
/// bearing), so this just reads that same result out in more familiar
/// units instead of re-deriving the same spherical trig a second time.

namespace wrenium::geo {

/// Great-circle distance between two points, in kilometers.
/// @param from One endpoint.
/// @param to The other endpoint.
/// @return The shortest-path distance between them along the sphere, in km.
inline float distanceKm(const GeoPoint &from, const GeoPoint &to) // NOLINT(bugprone-easily-swappable-parameters)
{
    const azimuthal::RotationFrame frame = azimuthal::makeRotationFrame(from);
    // Only the distance is needed here, so rotateBegin() alone -- the
    // same partial rotation clip.h's own hot loop uses when it doesn't
    // need bearing either (see rotateBegin()'s own comment) -- skips the
    // second atan2 call rotate() would otherwise spend on a bearing this
    // function never returns.
    const float centralAngle = kHalfPi - azimuthal::rotateBegin(to, frame).rotatedLat;
    return centralAngle * kEarthRadiusKm;
}

/// Initial compass bearing from @p from to @p to, in radians (0 = north,
/// increasing clockwise) -- the same convention every projection formula
/// in this library uses for bearing.
/// @param from The point bearing is measured from.
/// @param to The point bearing is measured toward.
/// @return The initial bearing, in radians.
inline float bearingRad(const GeoPoint &from, const GeoPoint &to) // NOLINT(bugprone-easily-swappable-parameters)
{
    return azimuthal::rotate(to, from).lonRad;
}

} // namespace wrenium::geo
