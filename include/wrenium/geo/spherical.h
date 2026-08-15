// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "wrenium/geo/detail/azimuthal/rotation.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/projection.h"

/// @file
/// Great-circle distance, bearing, and destination point for two arbitrary
/// points -- general spherical trig, not tied to any projection or
/// rendering pipeline. Built directly on azimuthal::rotate()/unrotate()'s
/// own already-verified, pole-safe formulas (detail/azimuthal/rotation.h):
/// "distance and bearing from A to B" is exactly what rotate(B, A) already
/// computes as its rotated-frame representation (rotatedLat = kHalfPi -
/// centralAngle, rotatedLon = bearing), and "the point at distance D,
/// bearing B from A" is exactly unrotate()'s own inverse of that same
/// representation -- this just reads those results out in more familiar
/// units instead of re-deriving the same spherical trig a second time.
///
/// Also has a few plain degree-space helpers (wrapLongitudeDeg(),
/// clampLatitudeDeg(), shortestAngleDeltaDeg()) for callers that keep their
/// own location/bearing state in degrees and need it kept within range
/// after arithmetic that can push it out.

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

/// The point reached by travelling @p distanceKm along the great circle
/// from @p origin at initial bearing @p bearingRad -- the inverse of
/// distanceKm()/bearingRad(): destinationPoint(origin, distanceKm(origin,
/// to), bearingRad(origin, to)) recovers @p to, up to this library's own
/// float/trig approximation budget.
/// @param origin The starting point.
/// @param distanceKm Distance to travel, in kilometers.
/// @param bearingRad Initial compass bearing, in radians -- same
/// convention as bearingRad() above.
/// @return The destination point.
inline GeoPoint destinationPoint(const GeoPoint &origin, float distanceKm, float bearingRad) // NOLINT(bugprone-easily-swappable-parameters)
{
    const float centralAngle = distanceKm / kEarthRadiusKm;
    return azimuthal::unrotate(GeoPoint{kHalfPi - centralAngle, bearingRad}, origin);
}

/// Wraps @p lonDeg to within `(-180, 180]` degrees -- for a longitude
/// value pushed out of range by arithmetic (adding a delta past +-180,
/// for example).
/// @param lonDeg Longitude in degrees, any range.
/// @return The equivalent longitude within `(-180, 180]`.
inline float wrapLongitudeDeg(float lonDeg)
{
    while (lonDeg > 180.0f) {
        lonDeg -= 360.0f;
    }
    while (lonDeg <= -180.0f) {
        lonDeg += 360.0f;
    }
    return lonDeg;
}

/// Clamps @p latDeg to `[-90 + marginDeg, 90 - marginDeg]`.
/// @param latDeg Latitude in degrees.
/// @param marginDeg Distance to keep away from each pole, in degrees.
/// @return @p latDeg unchanged if already within range, otherwise the
/// bound it crossed.
inline float clampLatitudeDeg(float latDeg, float marginDeg) // NOLINT(bugprone-easily-swappable-parameters)
{
    const float limit = 90.0f - marginDeg;
    if (latDeg > limit) {
        return limit;
    }
    if (latDeg < -limit) {
        return -limit;
    }
    return latDeg;
}

/// Shortest signed angle from @p fromDeg to @p toDeg, wrapped to
/// `(-180, 180]` -- adding this to @p fromDeg reaches @p toDeg by the
/// shorter way around, positive turning clockwise.
/// @param fromDeg Starting angle in degrees.
/// @param toDeg Target angle in degrees.
/// @return The signed delta, in degrees, within `(-180, 180]`.
inline float shortestAngleDeltaDeg(float fromDeg, float toDeg) // NOLINT(bugprone-easily-swappable-parameters)
{
    return wrapLongitudeDeg(toDeg - fromDeg);
}

} // namespace wrenium::geo
