// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/point_storage.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"

/// @file
/// Never allocated fresh per call, on heap or stack -- a several-thousand-
/// point buffer as a function-local stack variable risks overflowing a
/// constrained target's stack.
///
/// After a successful @ref wrenium::geo::projectRings() "projectRings" /
/// @ref wrenium::geo::projectLines() "projectLines" call (pipeline.h),
/// read the result back via `svgPath` (SVG text), projectedPoint() (one
/// point by index), or projectedPoints() + projectedRingSizes() (the raw
/// projected points, to feed a different emitter for example).

namespace wrenium::geo {

/// A single object owning every buffer the rotate/clip/project pipeline
/// needs, sized via compile-time template parameters. Meant to have static
/// storage duration -- a `static` global or a long-lived, caller-owned
/// object -- never allocated fresh per call.
/// @tparam MaxPoints Capacity of each point-stage buffer.
/// @tparam MaxRings Capacity of the ring-size lists (how many independent
/// closed rings the geometry can have at once).
/// @tparam MaxRingPoints Capacity of the internal per-ring rotation cache --
/// defaults to MaxPoints (always correct: no single ring can have more
/// points than the whole dataset), but a caller who knows their actual
/// largest ring is much smaller can override this down to reclaim RAM.
/// @tparam OutputCharCapacity Capacity of the SVG-text output buffer (SVG
/// is the "primary" format, hence it -- not the binary format -- gets a
/// dedicated slot on Workspace itself; a consumer wanting binary output
/// instantiates its own Buffer<std::uint8_t, N>).
template <std::size_t MaxPoints, std::size_t MaxRings = 256, std::size_t MaxRingPoints = MaxPoints, std::size_t OutputCharCapacity = (MaxPoints * 24) + (MaxRings * 4) + 16>
struct Workspace
{
    /// @cond WRENIUM_GEO_INTERNAL
    // Clip's working buffer (rotated, then projected in place). Public
    // only because projectRings()/projectLines() (pipeline.h, free
    // functions) need direct access -- use projectedPoint()/
    // projectedPoints()/projectedRingSizes() below to read the result
    // instead. Element type is a union (detail/point_storage.h), not
    // GeoPoint or Point directly; see that file for why.
    Buffer<PointStorage, MaxPoints> stageB;
    // Ring sizes for stageB, post-clip -- read via projectedRingSizes()
    // instead.
    Buffer<std::size_t, MaxRings> ringSizesB;

    // Scratch cache projectRings() (pipeline.h) hands to clipRingToSink()
    // (detail/azimuthal/clip.h): each ring's own points, rotated once
    // during clip's classification pass and reused for the output-
    // emitting pass instead of being rotated a second time. Reused ring
    // by ring within a single call -- not part of the result, never read
    // by a caller.
    GeoPoint ringRotatedCache[MaxRingPoints];
    /// @endcond

    /// The final emitted SVG path text -- write into it directly (via
    /// emitSvgPath(), for example) and read it back the same way, no
    /// separate publish step needed.
    Buffer<char, OutputCharCapacity> svgPath;

    /// The final projected point at @p index, valid after a successful
    /// @ref projectRings() / @ref projectLines() call. @p index ranges over the same
    /// flattened point list projectedRingSizes() describes -- ring 0's
    /// points first, then ring 1's, and so on.
    Point projectedPoint(std::size_t index) const
    {
        return stageB[index].point;
    }

    /// All final projected points as one contiguous array, flattened
    /// across every ring in projectedRingSizes() order -- what
    /// emitSvgPath()/`BinaryPathEmitter::encode` (or a caller's own
    /// emitter) read from.
    const Point *projectedPoints() const
    {
        // Safe: PointStorage is a union of two standard-layout structs
        // sharing a common initial sequence (two floats), so reinterpreting
        // a contiguous run of them through .point is well-defined, and its
        // size/alignment exactly match Point's (no extra members, no
        // padding difference).
        return reinterpret_cast<const Point *>(stageB.data());
    }

    /// Each ring's point count within projectedPoints(), one entry per
    /// surviving ring, in the same order.
    const Buffer<std::size_t, MaxRings> &projectedRingSizes() const
    {
        return ringSizesB;
    }
};

} // namespace wrenium::geo
