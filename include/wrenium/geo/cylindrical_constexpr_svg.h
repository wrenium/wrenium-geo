// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/cylindrical/mercator.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/svg_emitter.h"

/// @file
/// projectRingsToSvgConstexpr(): produces an SVG path string entirely at
/// compile time, given a compile-time-constant center/scale and point
/// list -- `constexpr auto svg = projectRingsToSvgConstexpr<...>(...)`, no
/// runtime call at all for that fixed input. For a fixed view (a splash
/// screen's map, a hard-coded location marker), this costs zero runtime
/// bytes and zero runtime cycles -- the result is just static data in the
/// compiled binary. Not a replacement for cylindrical::projectRingsToSvg()
/// (cylindrical_svg.h) for anything that varies at runtime -- panning,
/// zooming, a live GPS center.
///
/// cylindrical::project() (detail/cylindrical/mercator.h) recenters by a
/// plain longitude subtraction, not a sphere rotation, so it's called
/// directly here.
///
/// Doesn't handle antimeridian wraparound or pole-encircling rings the
/// way cylindrical_pipeline.h's own projectRings() does (that's real,
/// complex boundary-splitting logic, not reused here) -- fine for a fixed
/// view chosen so nothing of interest crosses +-180 degrees longitude;
/// not a drop-in replacement for that pipeline's own antimeridian
/// handling.

namespace wrenium::geo::cylindrical {

/// Projects @p ringCount closed rings (flattened in @p points, one entry
/// per ring in @p ringSizes -- same layout as
/// cylindrical::projectRings(), input_format.h's own "ring" definition)
/// to an SVG path string, entirely within one call -- given
/// compile-time-constant arguments throughout, this evaluates at compile
/// time; the returned Buffer's contents are then just static data, no
/// project/emit call left at runtime.
/// @tparam MaxPoints Working capacity for points -- size it to @p points'
/// own total point count.
/// @tparam MaxRings Working capacity for ring count -- size it to @p ringCount.
/// @tparam OutputCharCapacity Capacity of the returned SVG text buffer --
/// see svgOutputCharCapacityForRings() (float_format.h) to size it exactly.
/// @param points Flat array covering every ring back to back, sphere-space
/// (radians).
/// @param ringSizes Point count of ring i is `ringSizes[i]`.
/// @param ringCount Number of rings.
/// @param center The recenter point -- see project()'s identical parameter.
/// @param scale Output units per kilometer -- see project()'s identical
/// parameter.
/// @return The SVG path text -- Error::CapacityExceeded (either working
/// buffer, or the output text itself) is silently absorbed by dropping
/// what didn't fit rather than surfaced to the caller; size @p MaxPoints/
/// @p MaxRings/@p OutputCharCapacity generously for a fixed, known input.
// center/scale are documented and always passed in this order, matching
// project()'s own order -- reordering one alone would be its own hazard.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t OutputCharCapacity>
constexpr Buffer<char, OutputCharCapacity> projectRingsToSvgConstexpr(
    const GeoPoint *points,
    const std::size_t *ringSizes,
    std::size_t ringCount,
    const GeoPoint &center,
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
            if (projected.pushBack(project(points[pointOffset + i], center, scale)) == Error::Ok) {
                ++kept;
            }
        }
        keptRingSizes.pushBack(kept);
        pointOffset += ringSize;
    }

    Buffer<char, OutputCharCapacity> out;
    emitSvgPath(projected.data(), keptRingSizes.data(), keptRingSizes.size(), out);
    return out;
}

} // namespace wrenium::geo::cylindrical
