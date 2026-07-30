// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/cylindrical/mercator.h"
#include "wrenium/geo/error.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"
#include "wrenium/geo/workspace.h"

/// @file
/// Cylindrical-family counterpart to pipeline.h -- deliberately a
/// separate file, not added to pipeline.h itself (which is explicitly
/// azimuthal-only: it calls into `azimuthal::` directly and its own
/// file-level comment says so). No rotate stage, and no circular
/// clip-radius concept the way azimuthal has: `center` recenters (see
/// cylindrical::project()), and each entry point's `clipLatRad`/
/// `clipLonRad` let a whole ring/run that provably can't be visible skip
/// projection entirely -- a coarse, conservative pre-check (see each
/// function's own doc comment), not an exact geometric clip.
///
/// **Antimeridian handling.** Every point's own map position is
/// `wrapPi(point.lonRad - center.lonRad)` -- independently computed, no
/// accumulated or seeded state carried across a ring/run. Two consecutive
/// points' *edge* is tested for whether it crosses the map's own
/// `+-halfWorldWidth` boundary using only that edge's own short geographic
/// step (`detail::stepAcrossBoundary`, below) -- also a purely local test,
/// no ring-wide state. When an edge crosses, a real boundary point is
/// inserted (latitude linearly interpolated at the crossing) instead of
/// letting the ring jump straight across the map or relying on a
/// downstream correction pass.
///
/// This design follows the same shape as real polygon-clipping libraries'
/// antimeridian handling -- **d3-geo's `d3.geoClipAntimeridian`**
/// (`d3-geo/src/clip/antimeridian.js`) is the direct reference: rather than
/// accumulating one longitude value across a whole ring and correcting it
/// after the fact, each point is positioned independently and the
/// antimeridian is treated as a real clip boundary, with crossing edges
/// cut exactly at the boundary and the resulting fragments closed along
/// the boundary itself. d3-geo's own version clips against a general
/// spherical small circle and stitches fragments via a full boundary walk
/// (which also handles a ring encircling a pole through the very same
/// mechanism); what's here adapts the same idea to this library's simpler
/// case -- a rectangular Mercator map, clipping against two straight
/// vertical lines (`x = +-halfWorldWidth`) -- rather than porting d3's
/// implementation directly. See projectRingsMercator()'s own comment for
/// where the two approaches (same-edge closing vs. a pole-encircling ring)
/// diverge and why.
///
/// This file previously accumulated one seeded, ring-wide "unwrapped
/// longitude" value and patched it after the fact (a seed correction for a
/// point stored exactly at the wrap boundary, a whole-ring shift for a
/// wide ring landing in the wrong 360-degree copy, a second crossing check
/// for a ring straddling a given center's own antipodal meridian without
/// crossing the raw +-180 boundary). Each patch was correct for the bug
/// that motivated it and was then undermined by the next one -- the seeded
/// approach has no local way to distinguish those cases. The per-edge
/// design above needs none of those special cases: every one of those
/// live-reported bugs is naturally correct under it, as detailed in this
/// file's git history and tests/test_mercator.cpp's own comments.

namespace wrenium::geo::cylindrical {

namespace detail {

/// Result of testing whether stepping from a point already positioned at
/// @p x0 (its own independently-wrapped, therefore `(-kPi, kPi]`-bounded
/// position) by the short geographic delta to the next point would cross
/// the map's own `+-kPi` (center-relative) boundary.
struct BoundaryStep
{
    /// Where the next point would land if simply added to @p x0 --
    /// meaningful whether or not this step actually crosses: equal to the
    /// next point's own true position when it doesn't, or a value just
    /// past `+-kPi` when it does (see #crossesPositive/#crossesNegative).
    float candidate;
    bool crossesPositive; // candidate > kPi
    bool crossesNegative; // candidate < -kPi
};

/// The one local primitive the whole file's antimeridian handling is
/// built from: whether stepping by @p deltaRad from @p x0 crosses the
/// map's own boundary, decidable from just these two numbers -- no
/// ring-wide state. Relies on a simple algebraic fact (worth restating
/// here since it's what makes the *lack* of a seed-correction pass in
/// this file correct, not just convenient): for two points @p a, @p b
/// with @p center 's own longitude subtracted out, `wrapPi(a) +
/// wrapPi(b - a)` always equals `wrapPi(b)` -- wrapping a sum of already-
/// wrapped terms gives the same result as wrapping the raw sum, and the
/// raw sum here telescopes to `b - center`. So the position of the point
/// *right after* a crossing, computed by wrapping @p x0 plus the step
/// (`wrapPi(candidate)` below), is always identical to that point's own
/// plain, independent `wrapPi(nextPoint - center)` -- there is never a
/// "which 360-degree copy" decision left to make for a freshly-started
/// piece; it fully determined by this one local step.
inline BoundaryStep stepAcrossBoundary(float x0, float deltaRad)
{
    float candidate = x0 + deltaRad;
    // Floating-point rounding across the two wrapPi calls that typically
    // feed x0 and deltaRad (a real, measured effect for a ring closing
    // exactly at +-180 degrees, a common data convention -- see this
    // file's own overview comment) can push a candidate that's
    // mathematically exactly at the boundary a hair past it, causing a
    // spurious "crossing" for a ring that shouldn't have one at all (a
    // real bug this fixed: a 4-point island exactly on the antimeridian
    // exploded into stray corner points at the whole checked-in dataset's
    // own whole-world zoom). Snap anything this close to the boundary to
    // sit exactly on it -- never a real geographic crossing at this
    // tolerance (a small fraction of a degree), and this can only ever
    // make the crossing test *less* likely to fire spuriously, never miss
    // a genuine crossing.
    constexpr float kBoundaryToleranceRad = 1e-4f;
    if (candidate > kPi && candidate <= kPi + kBoundaryToleranceRad) {
        candidate = kPi;
    } else if (candidate < -kPi && candidate >= -kPi - kBoundaryToleranceRad) {
        candidate = -kPi;
    }
    return BoundaryStep{candidate, candidate > kPi, candidate < -kPi};
}

} // namespace detail

/// Projects @p input's closed coastline-style rings under the Mercator
/// projection, splitting any ring that crosses the map's own
/// center-relative boundary into multiple output pieces (see this file's
/// own overview comment for the antimeridian-handling design) -- every
/// point in a ring that survives the visibility pre-check below is
/// projected unconditionally (no exact geometric clip). Read the result
/// back the same way as @ref projectRings() (pipeline.h):
/// `workspace.svgPath`, `projectedPoint()`, or `projectedPoints()` /
/// `projectedRingSizes()`.
///
/// A ring crosses this boundary an even number of times unless it winds
/// around a pole (e.g. Antarctica's coastline, which legitimately sweeps
/// all 360 degrees of longitude) -- a standard topological fact (cutting a
/// sphere along a pole-to-pole arc gives a disk; a simple closed curve not
/// passing through either pole crosses that cut an even number of times,
/// odd only if it winds around one of the cut's own endpoints). A
/// pole-encircling ring is squared off instead of closed with a plain
/// line: see the comment inside this function, at the point where such a
/// ring is detected, for why and how -- this part of the design predates
/// and is unchanged by this file's own antimeridian rewrite (see this
/// file's overview comment), only the primitive it locates its own
/// rotation point with does.
///
/// For a non-pole-encircling ring's own N (even) crossings: this produces
/// N fragments, each with a boundary point where it entered and where it
/// exited (`+halfWorldWidth` or `-halfWorldWidth`). When both are the same
/// edge -- the common case, e.g. the checked-in dataset's own Australia
/// ring straddling a given center's own seam -- the existing implicit "Z"
/// closing draws a correct, edge-hugging vertical segment with no further
/// work. When they differ (a real possibility for a ring whose coastline
/// jaggedly re-crosses near the boundary, though not confirmed to occur in
/// the checked-in dataset), this function closes it the same way a
/// pole-encircling ring closes -- two synthetic corner points at a
/// detour latitude -- rather than d3-geo's own fully general fix (a
/// boundary walk that stitches arbitrarily many fragments together in the
/// correct order): a deliberate, documented simplification, not an
/// oversight.
///
/// @p clipLatRad/@p clipLonRad let a ring that provably can't be visible
/// skip the expensive walk below entirely -- real, measured savings on
/// constrained targets, not just a desktop nicety: on a real dataset, most
/// rings are nowhere near a zoomed-in viewport. This is a *coarse*
/// (whole-ring) and *conservative* (never wrongly excludes a ring that
/// could be visible, may keep one that turns out fully off-screen)
/// pre-check, not an exact geometric clip -- a caller still needs its own
/// pixel-level crop for correct visual output (e.g. `clip: true` on the
/// containing item in Qt Quick). A second, more precise check culls each
/// individual output *fragment* against the same window once its own
/// exact bounds are known (cheap: two comparisons per point, no extra
/// pass). Default (`kPi`, `kPi`) matches this function's own pre-culling
/// behavior exactly: provably a no-op for every ring, since the
/// accumulated longitude range computed below always contains the seed
/// value (itself within `(-kPi, kPi]`), so neither cull test can ever
/// trigger at that default.
/// @param workspace The Workspace to project into.
/// @param input The loaded ring geometry, from loadInputGeometry() (pipeline.h).
/// @param center The recenter point -- see cylindrical::project().
/// @param scale Output units per kilometer.
/// @param clipLatRad Half-height of the visible window, radians of
/// latitude around @p center. A ring entirely outside
/// `[center.latRad - clipLatRad, center.latRad + clipLatRad]` is skipped.
/// @param clipLonRad Half-width of the visible window, radians of
/// longitude around @p center. A ring entirely outside
/// `[-clipLonRad, clipLonRad]` (in center-relative unwrapped longitude)
/// is skipped, and likewise for each individual output fragment.
/// @return Error::Ok on success, or Error::CapacityExceeded if the result
/// doesn't fit in @p workspace.
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t OutputCharCapacity, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectRingsMercator(
    Workspace<MaxPoints, MaxRings, MaxRingPoints, OutputCharCapacity> &workspace,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const GeoPoint &center,
    float scale,
    float clipLatRad = kPi,
    float clipLonRad = kPi)
{
    const Buffer<GeoPoint, InputMaxPoints> &inputPoints = input.points;
    const Buffer<std::size_t, InputMaxRings> &inputRingSizes = input.ringSizes;
    const Buffer<float, InputMaxRings> &inputRingMinLat = input.ringMinLat;
    const Buffer<float, InputMaxRings> &inputRingMaxLat = input.ringMaxLat;

    workspace.stageB.clear();
    workspace.ringSizesB.clear();

    // center is a true 2D recenter point (project()'s own doc comment) --
    // computed once per call, not per point, since it's the same
    // subtraction for every point projected below.
    const float centerY = detail::projectY(center.latRad, scale);
    const float halfWorldWidth = kPi * kEarthRadiusKm * scale;
    const float visLatMin = center.latRad - clipLatRad;
    const float visLatMax = center.latRad + clipLatRad;

    std::size_t inputOffset = 0;
    for (std::size_t r = 0; r < inputRingSizes.size(); ++r) {
        const std::size_t ringSize = inputRingSizes[r];
        const GeoPoint *ring = &inputPoints[inputOffset];
        inputOffset += ringSize;

        if (ringSize < 3) {
            continue;
        }

        // Cheap latitude pre-check using this ring's own bounds, already
        // computed once by loadInputGeometry() -- no per-point work at
        // all, so this always runs before the more expensive checks
        // below.
        if (inputRingMaxLat[r] < visLatMin || inputRingMinLat[r] > visLatMax) {
            continue;
        }

        // First pass: sums each step's short (wrapped) longitude delta
        // all the way around to find this ring's total winding (for
        // almost every ring this is ~0 -- it comes back to where it
        // started without going all the way around -- but a ring that
        // actually encircles a pole nets a full turn, +-2*kPi instead;
        // half a turn is a safe threshold given those are the only two
        // cases that occur in practice), tracks which pole (by summed
        // latitude sign), locates this ring's own first center-relative
        // boundary crossing (if any -- used below purely to pick a
        // *rotation point* so the walk never needs to special-case
        // wrapping through the array's own start/end, not to correct a
        // seed the way the old design's centerEdgeJump did), and -- via
        // the same accumulated-from-ring[0] value this file's design
        // doesn't otherwise use for positioning -- a conservative
        // longitude range for the coarse cull check below. That
        // accumulation can itself land in the "wrong" 360-degree copy for
        // a ring whose own first point sits near this center's boundary
        // (the same class of issue the per-edge walk below exists to
        // avoid) -- harmless here, since the shifted-window checks below
        // only ever decide whether to *skip this ring entirely*, and
        // conservatively keeping a ring that turns out not actually
        // visible is always a safe failure mode, unlike using this same
        // value for actual positioning (which is exactly what the old
        // design did, and exactly what led to it needing three further
        // patches).
        std::size_t firstCrossing = ringSize; // sentinel: none found
        bool firstCrossingPositive = false;   // meaningful only if firstCrossing != ringSize
        float totalWindingRad = 0.0f;
        float latSum = 0.0f;
        float unwrappedLon = wrenium::geo::detail::wrapPi(ring[0].lonRad - center.lonRad);
        float lonMin = unwrappedLon;
        float lonMax = unwrappedLon;
        for (std::size_t i = 0; i < ringSize; ++i) {
            const GeoPoint &p0 = ring[i];
            const GeoPoint &p1 = ring[(i + 1) % ringSize];
            const float delta = wrenium::geo::detail::wrapPi(p1.lonRad - p0.lonRad);
            totalWindingRad += delta;
            latSum += p0.latRad;
            if (i > 0) {
                unwrappedLon += delta;
                if (unwrappedLon < lonMin) {
                    lonMin = unwrappedLon;
                } else if (unwrappedLon > lonMax) {
                    lonMax = unwrappedLon;
                }
            }
            if (firstCrossing == ringSize) {
                const float x0 = wrenium::geo::detail::wrapPi(p0.lonRad - center.lonRad);
                const detail::BoundaryStep step = detail::stepAcrossBoundary(x0, delta);
                if (step.crossesPositive || step.crossesNegative) {
                    firstCrossing = i;
                    firstCrossingPositive = step.crossesPositive;
                }
            }
        }
        const bool poleEncircling = (totalWindingRad > kPi) || (totalWindingRad < -kPi);
        const bool southPole = latSum < 0.0f;

        // No separate pole-encircling exemption needed here: such a ring's
        // own accumulated longitude range always spans a full turn (that's
        // what "encircling" means), so it naturally straddles any
        // clipLonRad window and is never excluded by this check -- only a
        // genuine latitude mismatch (the cheap pre-check above) can ever
        // cull one, which is correct (e.g. Antarctica genuinely isn't
        // visible from a Finland-zoomed view).
        //
        // ring[0] itself (this accumulation's seed) can sit exactly on the
        // wrap boundary (a real, common data convention; the checked-in
        // dataset's own Antarctica ring does), where wrapPi's (-kPi, kPi]
        // convention can pick either side -- shifting this whole range by
        // a full turn from where a differently-seeded walk would put it.
        // Check the range shifted by a full turn in both directions as
        // well rather than trying to derive a "correct" seed -- this cull
        // check's only job is "could this ring possibly be visible",
        // where conservative (never wrongly excludes) is all that's
        // required.
        const bool inWindow = !(lonMax < -clipLonRad || lonMin > clipLonRad);
        const bool inWindowShiftedDown = !((lonMax - 2.0f * kPi) < -clipLonRad || (lonMin - 2.0f * kPi) > clipLonRad);
        const bool inWindowShiftedUp = !((lonMax + 2.0f * kPi) < -clipLonRad || (lonMin + 2.0f * kPi) > clipLonRad);
        if (!inWindow && !inWindowShiftedDown && !inWindowShiftedUp) {
            continue;
        }

        const bool hasCrossing = (firstCrossing != ringSize);
        const std::size_t startIdx = hasCrossing ? (firstCrossing + 1) % ringSize : 0;

        if (poleEncircling) {
            // Rotate to start right after this ring's own first
            // center-relative crossing (if none was found -- shouldn't
            // happen for a ring that genuinely encircles a pole, given the
            // odd-crossing-count fact this function's own doc comment
            // explains, but startIdx already defaults to 0 defensively)
            // and walk the *whole* ring as one continuous piece, letting
            // the accumulated position run past +-kPi freely (no per-edge
            // clipping here) -- its real first and last points sit on
            // opposite edges of the projected map, geographically adjacent
            // (that's *why* the ring winds a full turn), so square it off
            // below instead of closing with a plain line.
            float unwrappedLonRad = wrenium::geo::detail::wrapPi(ring[startIdx].lonRad - center.lonRad);
            std::size_t pieceSize = 0;
            for (std::size_t step = 0; step < ringSize; ++step) {
                const std::size_t idx = (startIdx + step) % ringSize;
                const GeoPoint &current = ring[idx];
                if (step > 0) {
                    const std::size_t prevIdx = (startIdx + step - 1) % ringSize;
                    unwrappedLonRad += wrenium::geo::detail::wrapPi(current.lonRad - ring[prevIdx].lonRad);
                }

                wrenium::geo::detail::PointStorage storage(current);
                storage.point.x = unwrappedLonRad * kEarthRadiusKm * scale;
                storage.point.y = detail::projectY(current.latRad, scale) - centerY;
                const Error err = workspace.stageB.pushBack(storage);
                if (err != Error::Ok) {
                    return err;
                }
                ++pieceSize;
            }

            // Square it off: two extra points at the map's left/right
            // edges, at the clamped pole latitude, so the closed shape
            // follows the map boundary and the pole-latitude line the way
            // the real polar cap does.
            const float poleLatRad = southPole ? -kMercatorMaxLatRad : kMercatorMaxLatRad;
            const float poleY = detail::projectY(poleLatRad, scale) - centerY;

            const std::size_t firstIdx = workspace.stageB.size() - pieceSize;
            const std::size_t lastIdx = workspace.stageB.size() - 1;
            const float firstX = workspace.stageB[firstIdx].point.x;
            const float lastX = workspace.stageB[lastIdx].point.x;

            wrenium::geo::detail::PointStorage cornerNearLast;
            cornerNearLast.point.x = (lastX >= 0.0f) ? halfWorldWidth : -halfWorldWidth;
            cornerNearLast.point.y = poleY;
            Error err = workspace.stageB.pushBack(cornerNearLast);
            if (err != Error::Ok) {
                return err;
            }
            ++pieceSize;

            wrenium::geo::detail::PointStorage cornerNearFirst;
            cornerNearFirst.point.x = (firstX >= 0.0f) ? halfWorldWidth : -halfWorldWidth;
            cornerNearFirst.point.y = poleY;
            err = workspace.stageB.pushBack(cornerNearFirst);
            if (err != Error::Ok) {
                return err;
            }
            ++pieceSize;

            const Error ringErr = workspace.ringSizesB.pushBack(pieceSize);
            if (ringErr != Error::Ok) {
                return ringErr;
            }
            continue;
        }

        // Non-pole-encircling: rotate to start right after this ring's own
        // first crossing (if any -- startIdx already defaults to 0
        // otherwise) purely to avoid an artificial split at the ring's own
        // arbitrary index 0, then walk it, cutting a real boundary point
        // into the output at every crossing (this file's own overview
        // comment) instead of jumping straight across the map. The very
        // last edge checked (from this ring's own last point, wrapping
        // back to startIdx) is exactly the rotation edge itself, so it's
        // always detected as a crossing too when hasCrossing is true --
        // closing the final piece without needing any separate handling
        // after the loop.
        float x = wrenium::geo::detail::wrapPi(ring[startIdx].lonRad - center.lonRad);
        // The edge this ring's own *first* piece entered through -- the
        // opposite side from whichever way firstCrossing itself exits,
        // since that's the crossing this piece's own walk starts right
        // after. Meaningless (never read) when !hasCrossing.
        bool pieceEnteredPositive = !firstCrossingPositive;

        std::size_t pieceSize = 0;
        float pieceMinX = x;
        float pieceMaxX = x;

        for (std::size_t step = 0; step < ringSize; ++step) {
            const std::size_t idx = (startIdx + step) % ringSize;
            const GeoPoint &current = ring[idx];

            wrenium::geo::detail::PointStorage storage(current);
            storage.point.x = x * kEarthRadiusKm * scale;
            storage.point.y = detail::projectY(current.latRad, scale) - centerY;
            Error err = workspace.stageB.pushBack(storage);
            if (err != Error::Ok) {
                return err;
            }
            ++pieceSize;
            if (x < pieceMinX) {
                pieceMinX = x;
            } else if (x > pieceMaxX) {
                pieceMaxX = x;
            }

            const std::size_t nextIdx = (startIdx + step + 1) % ringSize;
            const GeoPoint &next = ring[nextIdx];
            const float delta = wrenium::geo::detail::wrapPi(next.lonRad - current.lonRad);
            const detail::BoundaryStep boundaryStep = detail::stepAcrossBoundary(x, delta);

            if (!boundaryStep.crossesPositive && !boundaryStep.crossesNegative) {
                x = boundaryStep.candidate;
                continue;
            }

            // Crossing: interpolate the latitude at the exact boundary
            // (linear in the raw points -- consistent with this library's
            // existing precision level elsewhere) and cut in a real
            // boundary point instead of jumping across the map.
            const float boundary = boundaryStep.crossesPositive ? kPi : -kPi;
            const float t = (boundary - x) / delta;
            const float crossLat = current.latRad + t * (next.latRad - current.latRad);
            const float crossY = detail::projectY(crossLat, scale) - centerY;
            const float exitX = boundary * kEarthRadiusKm * scale;

            wrenium::geo::detail::PointStorage exitPoint;
            exitPoint.point.x = exitX;
            exitPoint.point.y = crossY;
            Error exitErr = workspace.stageB.pushBack(exitPoint);
            if (exitErr != Error::Ok) {
                return exitErr;
            }
            ++pieceSize;
            if (boundary < pieceMinX) {
                pieceMinX = boundary;
            } else if (boundary > pieceMaxX) {
                pieceMaxX = boundary;
            }

            // Close this piece. Same edge in vs. out (the common case --
            // e.g. the checked-in dataset's own Australia ring straddling
            // a given center's own seam) needs nothing further: the
            // piece's own first point (its entry) and this exit point
            // already share the same x, so the implicit "Z" close draws a
            // correct, edge-hugging vertical segment. Different edges (see
            // this function's own doc comment) need the same two-corner
            // square-off a pole-encircling ring uses, at a detour latitude
            // chosen by which hemisphere this piece's own crossings sit
            // in.
            const bool exitedPositive = boundaryStep.crossesPositive;
            if (pieceEnteredPositive != exitedPositive) {
                const float detourLatRad = (crossLat < 0.0f) ? -kMercatorMaxLatRad : kMercatorMaxLatRad;
                const float detourY = detail::projectY(detourLatRad, scale) - centerY;

                wrenium::geo::detail::PointStorage cornerAtExit;
                cornerAtExit.point.x = exitX;
                cornerAtExit.point.y = detourY;
                Error cornerErr = workspace.stageB.pushBack(cornerAtExit);
                if (cornerErr != Error::Ok) {
                    return cornerErr;
                }
                ++pieceSize;

                wrenium::geo::detail::PointStorage cornerAtEntry;
                cornerAtEntry.point.x = (pieceEnteredPositive ? 1.0f : -1.0f) * halfWorldWidth;
                cornerAtEntry.point.y = detourY;
                cornerErr = workspace.stageB.pushBack(cornerAtEntry);
                if (cornerErr != Error::Ok) {
                    return cornerErr;
                }
                ++pieceSize;
            }

            const bool pieceVisible = !(pieceMaxX < -clipLonRad || pieceMinX > clipLonRad);
            if (pieceSize >= 2 && pieceVisible) {
                const Error ringErr = workspace.ringSizesB.pushBack(pieceSize);
                if (ringErr != Error::Ok) {
                    return ringErr;
                }
            } else {
                workspace.stageB.truncate(workspace.stageB.size() - pieceSize);
            }
            pieceSize = 0;

            const bool lastStep = (step == ringSize - 1);
            if (lastStep) {
                // This was the rotation edge closing the loop -- no new
                // piece starts.
                break;
            }

            const float entryX = -boundary * kEarthRadiusKm * scale;
            wrenium::geo::detail::PointStorage entryPoint;
            entryPoint.point.x = entryX;
            entryPoint.point.y = crossY;
            const Error entryErr = workspace.stageB.pushBack(entryPoint);
            if (entryErr != Error::Ok) {
                return entryErr;
            }
            pieceSize = 1;
            pieceMinX = -boundary;
            pieceMaxX = pieceMinX;
            pieceEnteredPositive = !exitedPositive;
            x = wrenium::geo::detail::wrapPi(next.lonRad - center.lonRad);
        }

        // A ring that never crossed at all closes on itself -- needs 3+
        // points to be a real shape. Otherwise (hasCrossing) every piece
        // is normally already finalized above, including the last one
        // (the loop's own last edge is exactly the rotation edge, so it's
        // always re-detected as a crossing too, breaking right after
        // finalizing it -- see the loop's own comment). pieceSize is 0
        // here in that normal case, making this whole block a no-op --
        // except for a real, live-reported edge case: the first pass's
        // own crossing detection and the walk's own (algebraically
        // equivalent, but not bit-for-bit identical after accumulating
        // through many points -- see this file's overview comment on the
        // algebraic fact this relies on) detection can disagree right at
        // this specific edge for a long, many-point ring, so the walk's
        // own last edge occasionally does *not* re-trigger, leaving a
        // real, non-empty piece never pushed to ringSizesB -- silently
        // corrupting every later ring's own offset into stageB, since its
        // points remain physically present but unaccounted for. Finalize
        // it here too as a safety net (2+ points, same bar a split-off
        // fragment uses, since this piece -- crossing detected or not --
        // is exactly that).
        const bool pieceVisible = !(pieceMaxX < -clipLonRad || pieceMinX > clipLonRad);
        const std::size_t minTrailingSize = hasCrossing ? 2 : 3;
        if (pieceSize >= minTrailingSize && pieceVisible) {
            const Error ringErr = workspace.ringSizesB.pushBack(pieceSize);
            if (ringErr != Error::Ok) {
                return ringErr;
            }
        } else if (pieceSize > 0) {
            workspace.stageB.truncate(workspace.stageB.size() - pieceSize);
        }
    }

    return Error::Ok;
}

/// Border-line counterpart to @ref projectRingsMercator(), for *open*
/// polyline data -- same antimeridian handling (see this file's own
/// overview comment for the design and its d3-geo reference), but every
/// piece (including a run that never crosses at all) is open, matching
/// @ref projectLines() (pipeline.h). No pole-encircling square-off and no
/// same-edge-vs-different-edge closing distinction: those exist to fix an
/// *implicit closing edge* a filled ring gets and an open line never does,
/// so a line has nothing to close at all -- every crossing simply ends one
/// run and starts the next, with a real boundary point inserted at each
/// (this file's own overview comment) rather than a jump across the map.
///
/// @p clipLatRad/@p clipLonRad: same visibility pre-check as
/// @ref projectRingsMercator() (see its own doc comment for the details
/// and the safety property), including the same per-fragment (here,
/// per-run) precise check once each run's own exact bounds are known.
/// @param workspace The Workspace to project into.
/// @param input The loaded line geometry, from loadInputGeometry() (pipeline.h).
/// @param center The recenter point -- see cylindrical::project().
/// @param scale Output units per kilometer.
/// @param clipLatRad See @ref projectRingsMercator()'s identical parameter.
/// @param clipLonRad See @ref projectRingsMercator()'s identical parameter.
/// @return Error::Ok on success, or Error::CapacityExceeded if the result
/// doesn't fit in @p workspace.
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t OutputCharCapacity, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectLinesMercator(
    Workspace<MaxPoints, MaxRings, MaxRingPoints, OutputCharCapacity> &workspace,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const GeoPoint &center,
    float scale,
    float clipLatRad = kPi,
    float clipLonRad = kPi)
{
    const Buffer<GeoPoint, InputMaxPoints> &inputPoints = input.points;
    const Buffer<std::size_t, InputMaxRings> &inputRingSizes = input.ringSizes;
    const Buffer<float, InputMaxRings> &inputRingMinLat = input.ringMinLat;
    const Buffer<float, InputMaxRings> &inputRingMaxLat = input.ringMaxLat;

    workspace.stageB.clear();
    workspace.ringSizesB.clear();

    // See projectRingsMercator()'s identical comment.
    const float centerY = detail::projectY(center.latRad, scale);
    const float visLatMin = center.latRad - clipLatRad;
    const float visLatMax = center.latRad + clipLatRad;

    std::size_t inputOffset = 0;
    for (std::size_t r = 0; r < inputRingSizes.size(); ++r) {
        const std::size_t lineSize = inputRingSizes[r];
        const GeoPoint *line = &inputPoints[inputOffset];
        inputOffset += lineSize;

        if (lineSize < 2) {
            continue;
        }

        if (inputRingMaxLat[r] < visLatMin || inputRingMinLat[r] > visLatMax) {
            continue;
        }

        // See projectRingsMercator()'s identical longitude-bounds pass and
        // its identical shifted-range cull check below for the reasoning
        // -- this coarse, whole-run pre-check is unaffected by this file's
        // antimeridian-handling rewrite.
        float unwrappedLon = wrenium::geo::detail::wrapPi(line[0].lonRad - center.lonRad);
        float lonMin = unwrappedLon;
        float lonMax = unwrappedLon;
        for (std::size_t i = 1; i < lineSize; ++i) {
            unwrappedLon += wrenium::geo::detail::wrapPi(line[i].lonRad - line[i - 1].lonRad);
            if (unwrappedLon < lonMin) {
                lonMin = unwrappedLon;
            } else if (unwrappedLon > lonMax) {
                lonMax = unwrappedLon;
            }
        }
        const bool inWindow = !(lonMax < -clipLonRad || lonMin > clipLonRad);
        const bool inWindowShiftedDown = !((lonMax - 2.0f * kPi) < -clipLonRad || (lonMin - 2.0f * kPi) > clipLonRad);
        const bool inWindowShiftedUp = !((lonMax + 2.0f * kPi) < -clipLonRad || (lonMin + 2.0f * kPi) > clipLonRad);
        if (!inWindow && !inWindowShiftedDown && !inWindowShiftedUp) {
            continue;
        }

        // No rotation needed -- an open line has no "array wraparound" to
        // avoid splitting across the way a closed ring does, so it simply
        // walks from its own first point.
        float x = wrenium::geo::detail::wrapPi(line[0].lonRad - center.lonRad);
        std::size_t pieceSize = 1;
        float pieceMinX = x;
        float pieceMaxX = x;

        wrenium::geo::detail::PointStorage firstStorage(line[0]);
        firstStorage.point.x = x * kEarthRadiusKm * scale;
        firstStorage.point.y = detail::projectY(line[0].latRad, scale) - centerY;
        Error err = workspace.stageB.pushBack(firstStorage);
        if (err != Error::Ok) {
            return err;
        }

        for (std::size_t i = 0; i + 1 < lineSize; ++i) {
            const GeoPoint &current = line[i];
            const GeoPoint &next = line[i + 1];
            const float delta = wrenium::geo::detail::wrapPi(next.lonRad - current.lonRad);
            const detail::BoundaryStep step = detail::stepAcrossBoundary(x, delta);

            if (!step.crossesPositive && !step.crossesNegative) {
                x = step.candidate;
                wrenium::geo::detail::PointStorage storage(next);
                storage.point.x = x * kEarthRadiusKm * scale;
                storage.point.y = detail::projectY(next.latRad, scale) - centerY;
                const Error pushErr = workspace.stageB.pushBack(storage);
                if (pushErr != Error::Ok) {
                    return pushErr;
                }
                ++pieceSize;
                if (x < pieceMinX) {
                    pieceMinX = x;
                } else if (x > pieceMaxX) {
                    pieceMaxX = x;
                }
                continue;
            }

            // See projectRingsMercator()'s identical crossing/interpolation
            // comment.
            const float boundary = step.crossesPositive ? kPi : -kPi;
            const float t = (boundary - x) / delta;
            const float crossLat = current.latRad + t * (next.latRad - current.latRad);
            const float crossY = detail::projectY(crossLat, scale) - centerY;
            const float exitX = boundary * kEarthRadiusKm * scale;

            wrenium::geo::detail::PointStorage exitPoint;
            exitPoint.point.x = exitX;
            exitPoint.point.y = crossY;
            Error exitErr = workspace.stageB.pushBack(exitPoint);
            if (exitErr != Error::Ok) {
                return exitErr;
            }
            ++pieceSize;
            if (boundary < pieceMinX) {
                pieceMinX = boundary;
            } else if (boundary > pieceMaxX) {
                pieceMaxX = boundary;
            }

            const bool pieceVisible = !(pieceMaxX < -clipLonRad || pieceMinX > clipLonRad);
            if (pieceSize >= 2 && pieceVisible) {
                const Error runErr = workspace.ringSizesB.pushBack(pieceSize);
                if (runErr != Error::Ok) {
                    return runErr;
                }
            } else {
                workspace.stageB.truncate(workspace.stageB.size() - pieceSize);
            }

            const float entryX = -boundary * kEarthRadiusKm * scale;
            wrenium::geo::detail::PointStorage entryPoint;
            entryPoint.point.x = entryX;
            entryPoint.point.y = crossY;
            const Error entryErr = workspace.stageB.pushBack(entryPoint);
            if (entryErr != Error::Ok) {
                return entryErr;
            }
            pieceSize = 1;
            pieceMinX = -boundary;
            pieceMaxX = pieceMinX;

            // The entry point above sits exactly at the boundary, not at
            // `next`'s own (slightly further) true position -- push that
            // too, same as the non-crossing branch above would if this
            // edge hadn't crossed. Without this, `next`'s own real point
            // is silently skipped: the following iteration only checks
            // the edge *from* `next`, it never re-pushes `next` itself
            // (every other point in this walk is pushed exactly once, by
            // whichever step first reaches it).
            x = wrenium::geo::detail::wrapPi(next.lonRad - center.lonRad);
            wrenium::geo::detail::PointStorage afterEntry(next);
            afterEntry.point.x = x * kEarthRadiusKm * scale;
            afterEntry.point.y = detail::projectY(next.latRad, scale) - centerY;
            const Error afterEntryErr = workspace.stageB.pushBack(afterEntry);
            if (afterEntryErr != Error::Ok) {
                return afterEntryErr;
            }
            ++pieceSize;
            if (x < pieceMinX) {
                pieceMinX = x;
            } else if (x > pieceMaxX) {
                pieceMaxX = x;
            }
        }

        const bool pieceVisible = !(pieceMaxX < -clipLonRad || pieceMinX > clipLonRad);
        if (pieceSize >= 2 && pieceVisible) {
            const Error runErr = workspace.ringSizesB.pushBack(pieceSize);
            if (runErr != Error::Ok) {
                return runErr;
            }
        } else {
            workspace.stageB.truncate(workspace.stageB.size() - pieceSize);
        }
    }

    return Error::Ok;
}

} // namespace wrenium::geo::cylindrical
