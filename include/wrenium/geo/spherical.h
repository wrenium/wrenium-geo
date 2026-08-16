// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>

#include "wrenium/f32math/trig.h"
#include "wrenium/geo/detail/angle.h"
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
/// Also has interpolate() (a point partway along the great circle between
/// two others, composing the three functions above), length() (a
/// polyline's total arc length, composing distanceKm() over consecutive
/// points), area() (a closed ring's own enclosed area), and a few plain
/// degree-space helpers (wrapLongitudeDeg(), clampLatitudeDeg(),
/// shortestAngleDeltaDeg()) for callers that keep their own
/// location/bearing state in degrees and need it kept within range after
/// arithmetic that can push it out.

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

/// The point at fraction @p t of the way from @p a to @p b along the
/// great circle through them -- t=0 is @p a, t=1 is @p b. Composes
/// distanceKm()/bearingRad()/destinationPoint() above rather than a
/// separate formula: the same great circle those already work with, just
/// read out at an arbitrary point along it instead of only its endpoint.
/// @param a The starting point (t=0).
/// @param b The ending point (t=1).
/// @param t Fraction of the distance from @p a to @p b -- values outside
/// [0, 1] extrapolate past @p b or before @p a.
/// @return The point at fraction @p t along the great circle from @p a to @p b.
inline GeoPoint interpolate(const GeoPoint &a, const GeoPoint &b, float t) // NOLINT(bugprone-easily-swappable-parameters)
{
    return destinationPoint(a, distanceKm(a, b) * t, bearingRad(a, b));
}

/// Total arc length of a polyline through @p points, in kilometers -- the
/// sum of each consecutive pair's distanceKm(). A flight's planned route
/// or a ship's track, for example: length feeds directly into fuel
/// planning, not just drawing the route.
/// @param points The polyline's points, in order.
/// @param count Number of points in @p points.
/// @param closed Whether to add the closing edge from the last point back
/// to the first (a ring's own perimeter) on top of the sum between
/// consecutive points -- false (the default) stops at the last point (an
/// open route/track).
/// @return The total arc length, in kilometers. Zero if @p count < 2.
inline float length(const GeoPoint *points, std::size_t count, bool closed = false)
{
    if (count < 2) {
        return 0.0f;
    }
    float total = 0.0f;
    for (std::size_t i = 0; i + 1 < count; ++i) {
        total += distanceKm(points[i], points[i + 1]);
    }
    if (closed) {
        total += distanceKm(points[count - 1], points[0]);
    }
    return total;
}

/// Area enclosed by the closed ring through @p points, in square
/// kilometers. Chamberlain & Duquette's spherical-excess formula
/// (JPL, "Some Algorithms for Polygons on a Sphere", 2007), rearranged into
/// a single pass over edges instead of vertices (algebraically identical:
/// `sum((lon[i+1] - lon[i-1]) * sin(lat[i]))` regrouped by edge gives
/// `sum(lon[i+1] * sin(lat[i]) - lon[i] * sin(lat[i+1]))`, the same shape
/// as the planar shoelace formula with longitude standing in for x and
/// sin(latitude) for y), so no scratch array is needed for a ring of any
/// size.
///
/// Longitude is tracked as a running delta from @p points's own first
/// point, each step wrapPi()'d rather than a raw difference -- the same
/// technique cylindrical_pipeline.h's own ring accumulation already uses,
/// for the identical reason: a ring crossing the antimeridian must use
/// each edge's true short-way angular step, not the near-360-degree jump
/// a raw longitude difference would see there.
///
/// Accurate for rings shaped like real digitized data (many points, each
/// edge spanning at most a few degrees) -- the formula treats each edge
/// as a straight line in (longitude, sin(latitude)) space, which only
/// matches the edge's own true great-circle path closely when the edge is
/// short. A ring built from just a handful of widely-spaced vertices (a
/// hand-drawn few-point polygon, not a coastline) underestimates area,
/// growing roughly with the square of edge length -- measured ~0.05% for
/// 3-degree edges, ~6% for 30-degree edges. Densify long edges with
/// interpolate() first if @p points didn't come from real geographic data.
/// @param points The ring's points, in order (no duplicated closing vertex).
/// @param count Number of points in @p points.
/// @return The enclosed area, in square kilometers. Zero if @p count < 3.
/// Undefined for a ring that encircles a pole -- like contains()
/// (contains.h), no real coastline ring does.
inline float area(const GeoPoint *points, std::size_t count)
{
    if (count < 3) {
        return 0.0f;
    }

    using wrenium::geo::detail::wrapPi;

    const float lonFirst = points[0].lonRad;
    const float latFirst = points[0].latRad;
    float lonPrev = lonFirst;
    float latPrev = latFirst;

    float sum = 0.0f;
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const float lonNext = lonPrev + wrapPi(points[i + 1].lonRad - points[i].lonRad);
        const float latNext = points[i + 1].latRad;
        sum += lonNext * f32math::sin(latPrev) - lonPrev * f32math::sin(latNext);
        lonPrev = lonNext;
        latPrev = latNext;
    }
    // Closing edge, back to points[0].
    sum += lonFirst * f32math::sin(latPrev) - lonPrev * f32math::sin(latFirst);

    const float unsignedSum = sum < 0.0f ? -sum : sum;
    return unsignedSum * kEarthRadiusKm * kEarthRadiusKm * 0.5f;
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
