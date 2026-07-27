// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cmath>

#include "wrenium/f32math/atan2.h"
#include "wrenium/f32math/trig.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/projection.h"

/// @file
/// Sphere rotation shared by every azimuthal-family projection -- not
/// specific to azimuthal equidistant (detail/azimuthal/equidistant.h),
/// which is just one such formula. rotate() re-expresses a sphere point
/// relative to an arbitrary center as if that center were the north pole;
/// clip.h's clip test and any azimuthal radial-distance formula both build
/// on top of this same rotated representation. A different azimuthal
/// variant (stereographic, orthographic, equal-area, ...) would reuse
/// rotate()/RotationFrame unchanged and only need its own radial-distance
/// formula alongside equidistant.h, not a different rotation step.
///
/// rotate()'s returned GeoPoint's latRad is kHalfPi at the center itself,
/// decreasing with true angular distance from it; its lonRad is the
/// initial compass bearing (0 = north, increasing clockwise) from center
/// to point. Computed directly via atan2-based spherical-trig formulas
/// rather than a 3D rotation matrix, since that's all any azimuthal
/// projection actually needs.
///
/// atan2-based, not acos-based: better conditioned near antipodal/
/// coincident points, where acos loses precision sharply. float
/// throughout (via wrenium-f32math), never double.

namespace wrenium::geo::azimuthal {

/// Precomputed once-per-`center` quantities rotate() needs on every call:
/// sin/cos of center.latRad, and center.lonRad itself. Building this once
/// and reusing it avoids redoing those trig calls on every point rotated
/// against the same center (clip.h's per-ring/per-line hot loops call
/// rotate() up to thousands of times per recompute, always with the same
/// center).
struct RotationFrame
{
    float centerLonRad;
    float sinCenterLat;
    float cosCenterLat;
};

/// Builds the once-per-center RotationFrame @ref rotate(const GeoPoint&, const RotationFrame&) needs.
/// @param center The projection center.
inline RotationFrame makeRotationFrame(const GeoPoint &center)
{
    RotationFrame frame;
    frame.centerLonRad = center.lonRad;
    f32math::sincos(center.latRad, frame.sinCenterLat, frame.cosCenterLat);
    return frame;
}

/// Holds rotate()'s intermediate state after its central-angle atan2f but
/// before its bearing atan2f -- see rotateBegin()/rotateFinish() below.
/// crossTermA/crossTermB double as the bearing atan2f's own (y, x)
/// arguments (a genuine identity shared by the distance and bearing
/// formulas, not a coincidence), so rotateFinish() needs no trig beyond
/// that one atan2f call.
struct RotatePartial
{
    float rotatedLat;
    float crossTermA;
    float crossTermB;
};

/// First half of rotate(): computes only #RotatePartial::rotatedLat (via
/// the central-angle atan2f), skipping the bearing atan2f -- for a caller
/// that only needs to test angular distance from center (clip.h's
/// inside/outside test, for example) and would otherwise throw the bearing
/// away for every point that fails that test. atan2f costs several times
/// what a single sinf/cosf call does, so on real (non-whole-world) view
/// data, where most rotated points fail the clip test, this measured a
/// 15-18% pipeline speedup at continental/regional zoom on real coastline
/// data (city-scale/whole-world zoom see negligible difference either
/// way -- too few points reach rotation at all, or nearly all of them
/// survive, respectively). Call rotateFinish() on the result once a point
/// is confirmed to need its bearing too.
/// @param point The sphere point to re-express.
/// @param frame A frame built by makeRotationFrame() for the desired center.
/// @return Partial state; rotatedLat alone is already the same value
/// rotate() would have returned as GeoPoint::latRad.
inline RotatePartial rotateBegin(const GeoPoint &point, const RotationFrame &frame)
{
    const float dLon = point.lonRad - frame.centerLonRad;

    const float sinCenterLat = frame.sinCenterLat;
    const float cosCenterLat = frame.cosCenterLat;
    float sinPointLat, cosPointLat;
    f32math::sincos(point.latRad, sinPointLat, cosPointLat);
    float sinDLon, cosDLon;
    f32math::sincos(dLon, sinDLon, cosDLon);

    // Central angle (true angular distance from center to point), computed
    // via the atan2-based (Vincenty) formula -- not
    // acos(sinC*sinP + cosC*cosP*cosDLon), which loses precision badly as
    // its argument approaches +-1 (near-coincident or near-antipodal
    // points).
    const float crossTermA = cosPointLat * sinDLon;
    const float crossTermB = cosCenterLat * sinPointLat - sinCenterLat * cosPointLat * cosDLon;
    const float centralAngleNumerator = sqrtf(crossTermA * crossTermA + crossTermB * crossTermB);
    const float centralAngleDenominator = sinCenterLat * sinPointLat + cosCenterLat * cosPointLat * cosDLon;
    const float centralAngle = f32math::atan2(centralAngleNumerator, centralAngleDenominator);

    RotatePartial partial;
    partial.rotatedLat = kHalfPi - centralAngle;
    partial.crossTermA = crossTermA;
    partial.crossTermB = crossTermB;
    return partial;
}

/// Second half of rotate(): the bearing atan2f rotateBegin() deferred.
/// @param partial A RotatePartial from rotateBegin(), for the same point/frame.
/// @return @p partial's point, fully re-expressed (same result rotate() would give).
inline GeoPoint rotateFinish(const RotatePartial &partial)
{
    GeoPoint rotated;
    rotated.latRad = partial.rotatedLat;
    rotated.lonRad = f32math::atan2(partial.crossTermA, partial.crossTermB);
    return rotated;
}

/// Re-expresses @p point relative to the center a RotationFrame was built
/// from. Prefer this overload when rotating many points against the same
/// center (build the frame once, outside the loop); the GeoPoint-center
/// overload below is for a single one-off rotation. Equivalent to
/// rotateFinish(rotateBegin(point, frame)) -- use that split instead when a
/// caller might not need the bearing (see rotateBegin()'s own comment).
/// @param point The sphere point to re-express.
/// @param frame A frame built by makeRotationFrame() for the desired center.
/// @return @p point re-expressed relative to the frame's center.
inline GeoPoint rotate(const GeoPoint &point, const RotationFrame &frame)
{
    return rotateFinish(rotateBegin(point, frame));
}

/// Convenience overload for a single one-off rotation -- builds a
/// RotationFrame internally and forwards to the overload above.
/// @param point The sphere point to re-express.
/// @param center The projection center.
/// @return @p point re-expressed relative to @p center.
inline GeoPoint rotate(const GeoPoint &point, const GeoPoint &center)
{
    return rotate(point, makeRotationFrame(center));
}

} // namespace wrenium::geo::azimuthal
