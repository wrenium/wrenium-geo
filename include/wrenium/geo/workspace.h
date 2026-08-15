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
/// After a successful call to either projection family's own
/// `projectRings()`/`projectLines()` (azimuthal_pipeline.h,
/// cylindrical_pipeline.h), read the result back via `svgPath` (SVG
/// text), projectedPoint() (one point by index), or projectedPoints() +
/// projectedRingSizes() (the raw projected points, to feed a different
/// emitter for example). See the Workspace struct's own comment for how
/// to size one.

namespace wrenium::geo {

/// A single object owning every buffer the rotate/clip/project pipeline
/// needs, sized via compile-time template parameters. Meant to have static
/// storage duration -- a `static` global or a long-lived, caller-owned
/// object -- never allocated fresh per call.
///
/// ## Sizing a Workspace (and its InputGeometry)
///
/// There's no heap here, so Workspace's/InputGeometry's own capacities
/// (MaxPoints, MaxRings, ...) are fixed at compile time -- some number
/// has to be chosen. What follows is how to choose it, from a quick safe
/// default up to finetuning for a genuinely RAM-constrained target.
///
/// ### 1. InputGeometry's capacity is exact
///
/// InputGeometry (input_format.h) holds your *entire* checked-in dataset,
/// loaded once and clipped from repeatedly -- so its capacity has to fit
/// that dataset exactly, unconditionally, regardless of what clip radius
/// you'll ever use. This is a fact about your data, not a design choice,
/// and it's already known the moment the data is generated: `topojson2bin`
/// emits it alongside each generated header as `<name>Info.pointCount`/
/// `.ringCount` (see tools/wrenium_geo_convert's own README section).
/// Use those directly -- never hand-copy a number here, it can silently
/// go stale the next time the dataset is regenerated.
///
/// ### 2. A safe starting point for Workspace
///
/// Workspace's own capacity is a different question: it only has to fit
/// the *clipped view* your app actually draws, which depends on your
/// clip-radius range -- genuinely variable, not a fact about the raw
/// data. The simplest safe choice, with no measurement at all: reuse
/// InputGeometry's own size, plus a margin. That margin matters for
/// closed rings specifically -- clipping a ring can produce *more*
/// points than the original had (closing a ring that got cut by the clip
/// circle synthesizes extra boundary-following points, "arc bridging" --
/// see azimuthal_pipeline.h's own projectRings() comment), so
/// InputGeometry's exact size is a floor for Workspace, not a ceiling.
/// Open polylines don't need this margin -- clipLineToSink()
/// (detail/azimuthal/clip.h) just ends a run at the crossing point
/// instead of re-closing it, so it can only gain a couple of points, not
/// a whole arc's worth. This is exactly what
/// common/wrenium_geo_qt_bridge/WreniumGeoBridge.h does, and it costs
/// nothing worth worrying about on a desktop target.
///
/// ### 3. Finetuning for a real RAM-constrained target
///
/// Past that safe default, there are two different kinds of unknown
/// here, and two different tools for them -- don't reach for the wrong
/// one:
///
/// - **MaxPoints/MaxRings/MaxRingPoints are data-dependent.** No formula
///   predicts them from clipRadiusKm alone -- it depends on where your
///   actual coastlines/borders are, which is a fact about your dataset,
///   not something derivable from first principles. *Measure* it
///   instead: define WRENIUM_GEO_TRACK_HIGH_WATER_MARK project-wide
///   (see buffer.h's own comment), run your real workload -- a
///   representative sweep of centers/clip radii, a replay of real device
///   logs, or just the app itself for a while -- then read
///   `stageB.highWaterMark()`/`ringSizesB.highWaterMark()` back. Exact,
///   arc-bridging included, no guessing.
/// - **OutputCharCapacity is different: it's calculable, not
///   data-dependent at all.** The largest coordinate your output can
///   ever reach is fully determined by numbers you already know -- your
///   own clipRadiusKm x scale ceiling, or just viewportRadiusPx if going
///   through makeViewport() (viewport.h; clipRadiusKm cancels out of
///   that helper's own scale formula). Compute it directly with
///   svgOutputCharCapacityForRings()/svgOutputCharCapacityForLines()
///   (float_format.h) instead of guessing *or* measuring.
///
/// MaxRingPoints (the per-ring rotation-cache size) defaults to
/// MaxPoints, always safe -- shrink it only if you know your dataset's
/// actual largest ring (see its own template-parameter description
/// below).
///
/// If a single Workspace is shared across a closed-ring dataset and an
/// open-polyline dataset (both drawn through the same buffers, one at a
/// time), workspace_sizing.h's sharedWorkspaceSizeFor() computes
/// MaxPoints/MaxRings/OutputCharCapacity for that pair in one call,
/// instead of doing steps 2 and 3 above by hand for each dataset and
/// taking the max yourself.
///
/// ### 4. However you size it, check for Error::CapacityExceeded
///
/// A too-small capacity fails safely -- Error::CapacityExceeded, never
/// memory corruption or a silent truncation -- but nothing forces a
/// caller to check it. Unchecked, it looks identical to "there's nothing
/// to draw here," not an error. Always check the returned Error, and
/// make a wrong guess loud during development (log/assert on it) rather
/// than silent -- see WreniumGeoBridge.cpp's own warnOnError() for a
/// worked example of that pattern.
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
    // only because both pipelines' own projectRings()/projectLines() (free
    // functions) need direct access -- use projectedPoint()/
    // projectedPoints()/projectedRingSizes() below to read the result
    // instead. Element type is a union (detail/point_storage.h), not
    // GeoPoint or Point directly; see that file for why.
    Buffer<detail::PointStorage, MaxPoints> stageB;
    // Ring sizes for stageB, post-clip -- read via projectedRingSizes()
    // instead.
    Buffer<std::size_t, MaxRings> ringSizesB;

    // Scratch cache azimuthal::projectRings() (azimuthal_pipeline.h)
    // hands to clipRingToSink() (detail/azimuthal/clip.h): each ring's
    // own points, rotated once
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
    /// call to either projection family's own `projectRings()`/
    /// `projectLines()`. @p index ranges over the same
    /// flattened point list projectedRingSizes() describes -- ring 0's
    /// points first, then ring 1's, and so on.
    constexpr Point projectedPoint(std::size_t index) const
    {
        return Point{stageB[index].x, stageB[index].y};
    }

    /// All final projected points as one contiguous array, flattened
    /// across every ring in projectedRingSizes() order -- what
    /// emitSvgPath()/`BinaryPathEmitter::encode` (or a caller's own
    /// emitter) read from.
    const Point *projectedPoints() const
    {
        // Safe: PointStorage (detail/point_storage.h) is the same
        // standard-layout two-float shape as Point, same size/alignment,
        // no extra members, no padding difference -- so reinterpreting a
        // contiguous run of them through Point is well-defined. Not
        // constexpr-eligible itself, though (reinterpret_cast isn't usable
        // in a constant expression) -- projectedPoint() above is, if that's
        // what a constexpr caller needs.
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
