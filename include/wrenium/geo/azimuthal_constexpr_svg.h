// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>

#include "wrenium/f32math/atan2.h"
#include "wrenium/f32math/trig.h"
#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/azimuthal/equidistant.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/projection.h"
#include "wrenium/geo/svg_emitter.h"

/// @file
/// projectRingsToSvgConstexpr(): produces an SVG path string entirely at
/// compile time, given a compile-time-constant center/clip radius/scale
/// and point list -- `constexpr auto svg = projectRingsToSvgConstexpr<...>(...)`,
/// no runtime call at all for that fixed input. For a single fixed view
/// (a splash screen's map, a product's one hard-coded location marker),
/// this costs zero runtime bytes and zero runtime cycles -- the result is
/// just static data in the compiled binary, not something computed at
/// startup.
///
/// Not a replacement for azimuthal::projectRingsToSvg() (azimuthal_svg.h):
/// that one is for anything center/clip radius/scale/data can vary at
/// runtime for (panning, zooming, a live GPS center), which needs
/// detail/azimuthal/clip.h's real boundary-accurate clipping -- something
/// this file can't offer (see below) and most real uses of this library
/// actually need. This is for the narrower case where everything is
/// already fixed before the program even runs.
///
/// Uses the real pipeline wherever it's already constexpr-capable
/// (f32math::sincos()/atan2(), azimuthal::projectEquidistant(),
/// emitSvgPath()) -- only rotate() itself is reimplemented below, with a
/// manual Newton-Raphson square root standing in for rotateBegin()'s
/// `sqrtf()` call, which isn't usable in a C++17 constant expression on
/// every compiler.
///
/// Narrower than the real pipeline in one more way: visibility here is a
/// per-point angular-distance test, not detail/azimuthal/clip.h's actual
/// boundary clipping -- a ring crossing the clip radius loses the points
/// outside it rather than being cut exactly at the boundary (no
/// arc-bridging). Fine for a fixed view chosen so nothing of interest sits
/// right at the edge; not a drop-in replacement for clip.h's own
/// boundary-accurate behavior.

namespace wrenium::geo::azimuthal {

namespace detail {

/// Square root, Newton-Raphson -- `std::sqrt()`/`sqrtf()` aren't usable in
/// a C++17 constant expression on every compiler (see this file's own
/// comment), so rotateConstexpr() below needs its own.
constexpr float sqrtConstexpr(float x)
{
    if (x <= 0.0f) {
        return 0.0f;
    }
    float guess = (x < 1.0f) ? 1.0f : x;
    for (int i = 0; i < 12; ++i) {
        guess = 0.5f * (guess + x / guess);
    }
    return guess;
}

/// Same result as azimuthal::rotate() (detail/azimuthal/rotation.h) --
/// point re-expressed relative to center, latRad = kHalfPi at the center
/// itself decreasing with distance from it, lonRad the compass bearing to
/// it -- computed the same way (atan2-based central angle/bearing), just
/// with sqrtConstexpr() above in place of rotateBegin()'s sqrtf().
constexpr GeoPoint rotateConstexpr(const GeoPoint &point, const GeoPoint &center)
{
    const float dLon = point.lonRad - center.lonRad;

    float sinCenterLat = 0.0f, cosCenterLat = 0.0f;
    wrenium::f32math::sincos(center.latRad, sinCenterLat, cosCenterLat);
    float sinPointLat = 0.0f, cosPointLat = 0.0f;
    wrenium::f32math::sincos(point.latRad, sinPointLat, cosPointLat);
    float sinDLon = 0.0f, cosDLon = 0.0f;
    wrenium::f32math::sincos(dLon, sinDLon, cosDLon);

    const float crossTermA = cosPointLat * sinDLon;
    const float crossTermB = cosCenterLat * sinPointLat - sinCenterLat * cosPointLat * cosDLon;
    const float centralAngleNumerator = sqrtConstexpr(crossTermA * crossTermA + crossTermB * crossTermB);
    const float centralAngleDenominator = sinCenterLat * sinPointLat + cosCenterLat * cosPointLat * cosDLon;
    const float centralAngle = wrenium::f32math::atan2(centralAngleNumerator, centralAngleDenominator);

    GeoPoint rotated;
    rotated.latRad = kHalfPi - centralAngle;
    rotated.lonRad = wrenium::f32math::atan2(crossTermA, crossTermB);
    return rotated;
}

/// True if @p rotated's angular distance from center (already folded into
/// its own latRad by rotateConstexpr()) is within @p clipRadiusRad -- a
/// per-point visibility test, not detail/azimuthal/clip.h's own boundary
/// clipping; see this file's own comment for what that means.
constexpr bool isVisibleConstexpr(const GeoPoint &rotated, float clipRadiusRad)
{
    return (kHalfPi - rotated.latRad) <= clipRadiusRad;
}

} // namespace detail

/// Projects @p ringCount closed rings (flattened in @p points, one entry
/// per ring in @p ringSizes -- same layout as azimuthal::projectRings(),
/// input_format.h's own "ring" definition) to an SVG path string, entirely
/// within one call -- given compile-time-constant arguments throughout,
/// this evaluates at compile time; the returned Buffer's contents are then
/// just static data, no rotate/project/emit call left at runtime.
/// @tparam MaxPoints Working capacity for points surviving the visibility
/// test -- size it to @p points' own total point count, the worst case
/// where every point survives.
/// @tparam MaxRings Working capacity for ring count -- size it to @p ringCount.
/// @tparam OutputCharCapacity Capacity of the returned SVG text buffer --
/// see svgOutputCharCapacityForRings() (float_format.h) to size it exactly.
/// @param points Flat array covering every ring back to back, sphere-space
/// (radians).
/// @param ringSizes Point count of ring i is `ringSizes[i]`.
/// @param ringCount Number of rings.
/// @param center The projection center.
/// @param clipRadiusRad Points farther than this from center are dropped
/// (see detail::isVisibleConstexpr()'s own comment).
/// @param scale Output units per kilometer -- see
/// azimuthal::projectEquidistant()'s identical parameter.
/// @return The SVG path text -- Error::CapacityExceeded (either working
/// buffer, or the output text itself) is silently absorbed by dropping
/// what didn't fit rather than surfaced to the caller; size @p MaxPoints/
/// @p MaxRings/@p OutputCharCapacity generously for a fixed, known input.
// center/clipRadiusRad/scale are documented and always passed in this
// order, matching azimuthal::projectRings()'s own order -- reordering one
// alone would be its own hazard.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t OutputCharCapacity>
constexpr Buffer<char, OutputCharCapacity> projectRingsToSvgConstexpr(
    const GeoPoint *points,
    const std::size_t *ringSizes,
    std::size_t ringCount,
    const GeoPoint &center,
    float clipRadiusRad,
    float scale)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    Buffer<Point, MaxPoints> projected;
    Buffer<std::size_t, MaxRings> keptRingSizes;

    std::size_t pointOffset = 0;
    for (std::size_t ringIndex = 0; ringIndex < ringCount; ++ringIndex) {
        const std::size_t ringSize = ringSizes[ringIndex];
        std::size_t kept = 0;
        for (std::size_t i = 0; i < ringSize; ++i) {
            const GeoPoint rotated = detail::rotateConstexpr(points[pointOffset + i], center);
            if (detail::isVisibleConstexpr(rotated, clipRadiusRad)) {
                if (projected.pushBack(projectEquidistant(rotated, scale)) == Error::Ok) {
                    ++kept;
                }
            }
        }
        keptRingSizes.pushBack(kept);
        pointOffset += ringSize;
    }

    Buffer<char, OutputCharCapacity> out;
    emitSvgPath(projected.data(), keptRingSizes.data(), keptRingSizes.size(), out);
    return out;
}

} // namespace wrenium::geo::azimuthal
