// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#ifndef WRENIUM_GEO_DETAIL_AZIMUTHAL_ROTATION_H
#define WRENIUM_GEO_DETAIL_AZIMUTHAL_ROTATION_H

#include <cmath>

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
/// coincident points, where acos loses precision sharply. float/sinf/
/// cosf/atan2f throughout, never double.

namespace wrenium::geo {

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
    frame.sinCenterLat = sinf(center.latRad);
    frame.cosCenterLat = cosf(center.latRad);
    return frame;
}

/// Re-expresses @p point relative to the center a RotationFrame was built
/// from. Prefer this overload when rotating many points against the same
/// center (build the frame once, outside the loop); the GeoPoint-center
/// overload below is for a single one-off rotation.
/// @param point The sphere point to re-express.
/// @param frame A frame built by makeRotationFrame() for the desired center.
/// @return @p point re-expressed relative to the frame's center.
inline GeoPoint rotate(const GeoPoint &point, const RotationFrame &frame)
{
    const float dLon = point.lonRad - frame.centerLonRad;

    const float sinCenterLat = frame.sinCenterLat;
    const float cosCenterLat = frame.cosCenterLat;
    const float sinPointLat = sinf(point.latRad);
    const float cosPointLat = cosf(point.latRad);
    const float sinDLon = sinf(dLon);
    const float cosDLon = cosf(dLon);

    // Central angle (true angular distance from center to point), computed
    // via the atan2-based (Vincenty) formula -- not
    // acos(sinC*sinP + cosC*cosP*cosDLon), which loses precision badly as
    // its argument approaches +-1 (near-coincident or near-antipodal
    // points).
    const float crossTermA = cosPointLat * sinDLon;
    const float crossTermB = cosCenterLat * sinPointLat - sinCenterLat * cosPointLat * cosDLon;
    const float centralAngleNumerator = sqrtf(crossTermA * crossTermA + crossTermB * crossTermB);
    const float centralAngleDenominator = sinCenterLat * sinPointLat + cosCenterLat * cosPointLat * cosDLon;
    const float centralAngle = atan2f(centralAngleNumerator, centralAngleDenominator);

    // Initial compass bearing from center to point, also atan2-based.
    // bearingX is exactly crossTermB above -- a genuine identity shared by
    // the standard distance and bearing formulas, not a coincidence, so it
    // is only computed once.
    const float bearingY = sinDLon * cosPointLat;
    const float bearingX = crossTermB;
    const float bearing = atan2f(bearingY, bearingX);

    GeoPoint rotated;
    rotated.latRad = kHalfPi - centralAngle;
    rotated.lonRad = bearing;
    return rotated;
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

} // namespace wrenium::geo

#endif // WRENIUM_GEO_DETAIL_AZIMUTHAL_ROTATION_H
