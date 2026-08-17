// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/conic/lambert_conformal.h"
#include "wrenium/geo/error.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/workspace.h"

/// @file
/// Conic-family counterpart to azimuthal_pipeline.h/cylindrical_pipeline.h
/// -- deliberately its own file and its own `conic::` namespace, for the
/// same reason cylindrical is separate from azimuthal: each family is a
/// genuinely different orchestration in its own right. Only one radial
/// formula exists so far (Lambert conformal conic,
/// detail/conic/lambert_conformal.h), so there's no `ProjectionType`-style
/// dispatch here either -- matching cylindrical's own shape (one formula,
/// no enum); azimuthal has several formulas sharing one rotation step,
/// which is what its own dispatch is for.
///
/// **Wedge-boundary handling.** A Lambert conformal conic map is
/// inherently a regional/continental one -- the cone unrolls into a wedge
/// (narrower than a full circle whenever the cone constant `n` isn't
/// exactly 1), the wedge's own open edge sitting at longitude `originLon
/// +- 180`, the same real-world meridian cylindrical_pipeline.h's own
/// antimeridian handling cuts along -- **d3-geo's own conic clipping is
/// the direct reference**, same as that file's. A real geographic step
/// crossing that meridian is cut exactly at the boundary (latitude
/// linearly interpolated, projected directly at the wedge's own edge
/// angle via detail::conic::projectAtTheta -- lambert_conformal.h's own
/// comment on why), closing the gap at the wedge itself rather than
/// leaving it to jump straight across the map. The wedge's own two edges
/// meet at a single finite point -- rho == 0, the cone's own apex, which
/// is this frame's near pole (north for `n >= 0`, south for `n < 0`; see
/// makeLambertConformalConicFrame()'s own comment) -- so closing a piece
/// that entered and exited via different edges only ever needs that one
/// point inserted. A ring that encircles the *opposite* pole (the one
/// that recedes to infinity under this projection -- a real, inherent
/// limit any actual printed LCC chart shares, e.g. a CONUS chart was
/// never going to show Antarctica either) is skipped; see
/// projectRings()'s own comment for the exact test.
///
/// `clipLatRad`/`clipLonRad` are the same coarse, conservative, whole-
/// piece pre-check cylindrical_pipeline.h's own identical parameters are
/// -- see that file's own doc comment for the exact contract. This file
/// skips cylindrical's own finer per-fragment visibility cull (a
/// deliberate, documented simplification: that check is purely a
/// performance optimization there, and every fragment this file produces
/// is still geometrically correct without it). Read the result back the
/// same way as azimuthal::projectRings() (azimuthal_pipeline.h):
/// workspace.projectedPoints() / projectedRingSizes().

namespace wrenium::geo::conic {

namespace detail {

/// Whether @p unwrappedLonMin/@p unwrappedLonMax (a ring/line's own
/// continuously-accumulated longitude extent around the frame's origin)
/// could possibly fall inside the @p clipLonRad half-width window --
/// shared by projectRings()/projectLines() below.
inline bool inLonWindow(float unwrappedLonMin, float unwrappedLonMax, float clipLonRad)
{
    return !(unwrappedLonMax < -clipLonRad || unwrappedLonMin > clipLonRad);
}

/// Result of testing whether stepping from a point already positioned at
/// @p x0 (its own independently-wrapped, therefore `(-kPi, kPi]`-bounded,
/// longitude difference from the frame's origin) by the short geographic
/// delta to the next point would cross the wedge's own open edge (real
/// longitude `originLon +- 180`). Deliberately duplicated from
/// cylindrical_pipeline.h's own identical-shaped
/// `BoundaryStep`/`stepAcrossBoundary` rather than shared -- this file's
/// own overview comment on why each projection family owns its own
/// helpers even when the underlying idea (and, here, the exact boundary
/// constant) is the same. This operates on raw longitude difference --
/// `theta` (that difference times `n`) only enters after scaling; the
/// crossing itself happens in real geography, at `+-kPi`, regardless of
/// how far `theta` then scales it.
struct LonBoundaryStep
{
    float candidate;      ///< Where the next point would land if simply added to x0.
    bool crossesPositive; // candidate > kPi
    bool crossesNegative; // candidate < -kPi
};

/// See LonBoundaryStep's own comment; identical tolerance-snapping
/// reasoning as cylindrical_pipeline.h's own stepAcrossBoundary (a real
/// geographic ring closing exactly at +-180 degrees is a common data
/// convention, and floating-point rounding across the two wrapPi() calls
/// that typically feed x0/deltaRad can push a mathematically-exact
/// boundary point a hair past it).
inline LonBoundaryStep stepAcrossLonBoundary(float x0, float deltaRad)
{
    float candidate = x0 + deltaRad;
    constexpr float kBoundaryToleranceRad = 1e-4f;
    if (candidate > kPi && candidate <= kPi + kBoundaryToleranceRad) {
        candidate = kPi;
    } else if (candidate < -kPi && candidate >= -kPi - kBoundaryToleranceRad) {
        candidate = -kPi;
    }
    return LonBoundaryStep{candidate, candidate > kPi, candidate < -kPi};
}

} // namespace detail

/// Projects @p input's closed coastline-style rings under the Lambert
/// conformal conic projection, splitting any ring that crosses the
/// wedge's own open edge into multiple output pieces (see this file's own
/// overview comment for the design and its d3-geo reference).
///
/// A ring crosses this boundary an even number of times unless it winds
/// around a pole -- the same topological fact
/// cylindrical_pipeline.h's own identical check relies on (see that
/// file's own doc comment). A pole-encircling ring is closed by routing
/// through the cone's own apex instead of a plain line; see the comment
/// inside this function, at the point where such a ring is detected, and
/// this file's own overview comment for why closing through the apex is
/// correct here, with no detour corner pair needed.
///
/// For a non-pole-encircling ring's own N (even) crossings: this produces
/// N fragments, each with a boundary point where it entered and where it
/// exited (`+n*kPi` or `-n*kPi` theta, i.e. one of the wedge's own two
/// straight edges). When both are the same edge -- the common case -- the
/// existing implicit "Z" closing draws a correct, edge-hugging segment
/// with no further work (both points sit on the same ray from the apex).
/// When they differ, one point (the apex) is inserted to route the
/// closing edge through it instead of cutting across the wedge's
/// interior.
///
/// @p clipLatRad/@p clipLonRad let a ring that provably can't be visible
/// skip the walk below entirely -- see this file's own overview comment
/// for the exact (coarse, conservative) contract, matching
/// cylindrical_pipeline.h's identical parameters.
/// @param workspace The Workspace to project into.
/// @param input The loaded ring geometry, from loadInputGeometry() (input_format.h).
/// @param frame A frame built by makeLambertConformalConicFrame() (detail/conic/lambert_conformal.h).
/// @param scale Output units per kilometer.
/// @param clipLatRad Half-height of the visible window, radians of
/// latitude around @p frame's own origin latitude. A ring entirely
/// outside `[originLat - clipLatRad, originLat + clipLatRad]` is skipped.
/// @param clipLonRad Half-width of the visible window, radians of
/// longitude around @p frame's own central meridian. A ring entirely
/// outside `[-clipLonRad, clipLonRad]` (center-relative, continuously
/// accumulated around the ring) is skipped.
/// @return Error::Ok on success, or Error::CapacityExceeded if the result
/// doesn't fit in @p workspace.
// scale/clipLatRad/clipLonRad are documented and always passed in this
// order across every call site in this codebase; reordering one alone
// would be its own hazard.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectRings(
    Workspace<MaxPoints, MaxRings, MaxRingPoints> &workspace,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const LambertConformalConicFrame &frame,
    float scale,
    float clipLatRad = kPi,
    float clipLonRad = kPi)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const Buffer<GeoPoint, InputMaxPoints> &inputPoints = input.points;
    const Buffer<std::size_t, InputMaxRings> &inputRingSizes = input.ringSizes;
    const Buffer<float, InputMaxRings> &inputRingMinLat = input.ringMinLat;
    const Buffer<float, InputMaxRings> &inputRingMaxLat = input.ringMaxLat;

    workspace.stageB.clear();
    workspace.ringSizesB.clear();

    const float visLatMin = frame.originLatRad - clipLatRad;
    const float visLatMax = frame.originLatRad + clipLatRad;
    // The wedge's own apex: rho == 0 there regardless of theta, so both
    // boundary edges meet at this one point -- see this file's own
    // overview comment.
    const Point apex{0.0f, -frame.rho0 * scale};

    std::size_t inputOffset = 0;
    for (std::size_t r = 0; r < inputRingSizes.size(); ++r) {
        const std::size_t ringSize = inputRingSizes[r];
        const GeoPoint *ring = &inputPoints[inputOffset];
        inputOffset += ringSize;

        if (ringSize < 3) {
            continue;
        }

        // Cheap latitude pre-check using this ring's own bounds, already
        // computed once by loadInputGeometry() -- see
        // cylindrical_pipeline.h's identical check for the reasoning.
        if (inputRingMaxLat[r] < visLatMin || inputRingMinLat[r] > visLatMax) {
            continue;
        }

        // First pass: total winding (detects pole-encircling -- the same
        // topological test cylindrical_pipeline.h's own identical pass
        // uses, see that file's own doc comment for why half a turn is a
        // safe threshold), which pole it encircles (by summed latitude
        // sign), and this ring's own first center-relative
        // longitude-boundary crossing (purely to pick a *rotation point*
        // so the walk below never needs to special-case wrapping through
        // the array's own start/end).
        std::size_t firstCrossing = ringSize; // sentinel: none found
        bool firstCrossingPositive = false;   // meaningful only if firstCrossing != ringSize
        float totalWindingRad = 0.0f;
        float latSum = 0.0f;
        for (std::size_t i = 0; i < ringSize; ++i) {
            const GeoPoint &p0 = ring[i];
            const GeoPoint &p1 = ring[(i + 1) % ringSize];
            const float delta = wrenium::geo::detail::wrapPi(p1.lonRad - p0.lonRad);
            totalWindingRad += delta;
            latSum += p0.latRad;
            if (firstCrossing == ringSize) {
                const float x0 = wrenium::geo::detail::wrapPi(p0.lonRad - frame.originLonRad);
                const detail::LonBoundaryStep step = detail::stepAcrossLonBoundary(x0, delta);
                if (step.crossesPositive || step.crossesNegative) {
                    firstCrossing = i;
                    firstCrossingPositive = step.crossesPositive;
                }
            }
        }
        const bool poleEncircling = (totalWindingRad > kPi) || (totalWindingRad < -kPi);

        if (poleEncircling) {
            // Only this frame's own finite pole (north for n >= 0, south
            // for n < 0 -- see makeLambertConformalConicFrame()'s own
            // comment) can be routed through; the opposite pole recedes
            // to infinity under this projection, so a ring encircling it
            // can't be rendered correctly here -- skip rather than draw
            // nonsense (this file's own overview comment).
            const bool ringIsNorth = (latSum >= 0.0f);
            const bool frameIsNorth = (frame.n >= 0.0f);
            if (ringIsNorth != frameIsNorth) {
                continue;
            }

            const std::size_t startIdx = (firstCrossing == ringSize) ? 0 : (firstCrossing + 1) % ringSize;

            // Walk the whole ring as one continuous piece, accumulating
            // theta itself, unwrapped, so it sweeps smoothly around the
            // apex across the whole ring instead of jumping at each
            // point the way project()'s own per-point wrapped value
            // would -- the same idea as cylindrical_pipeline.h's own
            // accumulated-x technique for its pole-encircling case, with
            // theta as the swept quantity here, since that's what
            // actually determines a conic point's own angular position
            // around the apex.
            float unwrappedLonRad = wrenium::geo::detail::wrapPi(ring[startIdx].lonRad - frame.originLonRad);
            std::size_t pieceSize = 0;
            for (std::size_t step = 0; step < ringSize; ++step) {
                const std::size_t idx = (startIdx + step) % ringSize;
                const GeoPoint &current = ring[idx];
                if (step > 0) {
                    const std::size_t prevIdx = (startIdx + step - 1) % ringSize;
                    unwrappedLonRad += wrenium::geo::detail::wrapPi(current.lonRad - ring[prevIdx].lonRad);
                }

                const Point projected = projectAtTheta(current.latRad, frame.n * unwrappedLonRad, frame, scale);
                wrenium::geo::detail::PointStorage storage;
                storage = projected;
                const Error err = workspace.stageB.pushBack(storage);
                if (err != Error::Ok) {
                    return err;
                }
                ++pieceSize;
            }

            // Close through the apex -- both wedge edges meet there, so
            // one inserted point correctly routes the closing edge
            // through the cone's own apex instead of a straight chord
            // cutting through its interior (this file's own overview
            // comment).
            wrenium::geo::detail::PointStorage apexStorage;
            apexStorage = apex;
            const Error apexErr = workspace.stageB.pushBack(apexStorage);
            if (apexErr != Error::Ok) {
                return apexErr;
            }
            ++pieceSize;

            const Error ringErr = workspace.ringSizesB.pushBack(pieceSize);
            if (ringErr != Error::Ok) {
                return ringErr;
            }
            continue;
        }

        // Non-pole-encircling: coarse, conservative whole-ring longitude
        // window cull (this file's own overview comment) using this
        // ring's own continuously-accumulated longitude extent.
        //
        // ring[0] itself (this accumulation's seed) can sit exactly on
        // the wrap boundary (a real, common data convention; the
        // checked-in dataset's own Eurasia+Africa ring does), where
        // wrapPi's `(-kPi, kPi]` convention can pick either side --
        // shifting this whole range by a full turn from where a
        // differently-seeded walk would put it, and this ring is
        // genuinely, provably visible either way. Check the range
        // shifted by a full turn in both directions as well, exactly
        // mirroring cylindrical_pipeline.h's own identical fix for the
        // identical failure mode (that file's own doc comment covers the
        // reasoning in more depth) -- this cull check's only job is
        // "could this ring possibly be visible", where conservative
        // (never wrongly excludes) is all that's required.
        float unwrappedLon = wrenium::geo::detail::wrapPi(ring[0].lonRad - frame.originLonRad);
        float lonMin = unwrappedLon;
        float lonMax = unwrappedLon;
        for (std::size_t i = 1; i < ringSize; ++i) {
            unwrappedLon += wrenium::geo::detail::wrapPi(ring[i].lonRad - ring[i - 1].lonRad);
            if (unwrappedLon < lonMin) {
                lonMin = unwrappedLon;
            } else if (unwrappedLon > lonMax) {
                lonMax = unwrappedLon;
            }
        }
        const bool inWindow = detail::inLonWindow(lonMin, lonMax, clipLonRad);
        const bool inWindowShiftedDown = detail::inLonWindow(lonMin - 2.0f * kPi, lonMax - 2.0f * kPi, clipLonRad);
        const bool inWindowShiftedUp = detail::inLonWindow(lonMin + 2.0f * kPi, lonMax + 2.0f * kPi, clipLonRad);
        if (!inWindow && !inWindowShiftedDown && !inWindowShiftedUp) {
            continue;
        }

        // Rotate to start right after this ring's own first crossing (if
        // any -- startIdx already defaults to 0 otherwise) purely to
        // avoid an artificial split at the ring's own arbitrary index 0,
        // then walk it, cutting a real boundary point into the output at
        // every crossing instead of jumping straight across the map. The
        // very last edge checked (from this ring's own last point,
        // wrapping back to startIdx) is exactly the rotation edge
        // itself, so it's always detected as a crossing too when
        // hasCrossing is true -- closing the final piece without needing
        // any separate handling after the loop (same structure as
        // cylindrical_pipeline.h's identical walk).
        const bool hasCrossing = (firstCrossing != ringSize);
        const std::size_t startIdx = hasCrossing ? (firstCrossing + 1) % ringSize : 0;

        float x = wrenium::geo::detail::wrapPi(ring[startIdx].lonRad - frame.originLonRad);
        bool pieceEnteredPositive = !firstCrossingPositive;

        std::size_t pieceSize = 0;
        for (std::size_t step = 0; step < ringSize; ++step) {
            const std::size_t idx = (startIdx + step) % ringSize;
            const GeoPoint &current = ring[idx];

            wrenium::geo::detail::PointStorage storage;
            storage = project(current, frame, scale);
            Error err = workspace.stageB.pushBack(storage);
            if (err != Error::Ok) {
                return err;
            }
            ++pieceSize;

            const std::size_t nextIdx = (startIdx + step + 1) % ringSize;
            const GeoPoint &next = ring[nextIdx];
            const float delta = wrenium::geo::detail::wrapPi(next.lonRad - current.lonRad);
            const detail::LonBoundaryStep boundaryStep = detail::stepAcrossLonBoundary(x, delta);

            if (!boundaryStep.crossesPositive && !boundaryStep.crossesNegative) {
                x = boundaryStep.candidate;
                continue;
            }

            // Crossing: interpolate the latitude at the exact boundary
            // (linear in the raw points, matching
            // cylindrical_pipeline.h's identical technique) and project
            // it directly at the wedge's own exact edge angle via
            // projectAtTheta() -- see that function's own comment for
            // why (project()'s own wrapPi(-kPi) == +kPi asymmetry rules
            // out reconstructing this via a synthetic longitude).
            const float boundary = boundaryStep.crossesPositive ? kPi : -kPi;
            const float t = (boundary - x) / delta;
            const float crossLat = current.latRad + t * (next.latRad - current.latRad);
            const Point exitPoint = projectAtTheta(crossLat, frame.n * boundary, frame, scale);

            wrenium::geo::detail::PointStorage exitStorage;
            exitStorage = exitPoint;
            Error exitErr = workspace.stageB.pushBack(exitStorage);
            if (exitErr != Error::Ok) {
                return exitErr;
            }
            ++pieceSize;

            // Close this piece. Same edge in vs. out (the common case)
            // needs nothing further: the piece's own entry and this exit
            // point already sit on the same ray from the apex (theta ==
            // frame.n * boundary for both), so the implicit "Z" close
            // draws a correct, boundary-hugging segment. Different edges
            // need routing through the apex instead of a chord cutting
            // across the wedge's interior -- see this file's own
            // overview comment.
            const bool exitedPositive = boundaryStep.crossesPositive;
            if (pieceEnteredPositive != exitedPositive) {
                wrenium::geo::detail::PointStorage apexStorage;
                apexStorage = apex;
                Error apexErr = workspace.stageB.pushBack(apexStorage);
                if (apexErr != Error::Ok) {
                    return apexErr;
                }
                ++pieceSize;
            }

            if (pieceSize >= 2) {
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

            const float entryBoundary = -boundary;
            const Point entryPoint = projectAtTheta(crossLat, frame.n * entryBoundary, frame, scale);
            wrenium::geo::detail::PointStorage entryStorage;
            entryStorage = entryPoint;
            const Error entryErr = workspace.stageB.pushBack(entryStorage);
            if (entryErr != Error::Ok) {
                return entryErr;
            }
            pieceSize = 1;
            pieceEnteredPositive = !exitedPositive;
            x = wrenium::geo::detail::wrapPi(next.lonRad - frame.originLonRad);
        }

        // A ring that never crossed at all closes on itself -- needs 3+
        // points to be a real shape. Otherwise (hasCrossing) every piece
        // is normally already finalized above; this is a safety net for
        // a long, many-point ring where floating-point rounding can make
        // the walk's own last edge not re-trigger a crossing it
        // algebraically should (see cylindrical_pipeline.h's identical
        // trailing-piece comment for the same reasoning).
        if (pieceSize > 0) {
            const std::size_t minTrailingSize = hasCrossing ? 2 : 3;
            if (pieceSize >= minTrailingSize) {
                const Error ringErr = workspace.ringSizesB.pushBack(pieceSize);
                if (ringErr != Error::Ok) {
                    return ringErr;
                }
            } else {
                workspace.stageB.truncate(workspace.stageB.size() - pieceSize);
            }
        }
    }

    return Error::Ok;
}

/// Border-line counterpart to @ref projectRings(), for *open* polyline
/// data -- same wedge-boundary handling (see this file's own overview
/// comment for the design and its d3-geo reference), but every piece
/// (including a run that never crosses at all) is open, matching
/// cylindrical::projectLines(). No pole-encircling square-off and no
/// same-edge-vs-different-edge closing distinction: those exist to fix an
/// *implicit closing edge* a filled ring gets and an open line never
/// does, so a line has nothing to close at all -- every crossing simply
/// ends one run and starts the next, with a real boundary point inserted
/// at each (projected at the wedge's own exact edge angle, same as
/// projectRings()) rather than a jump across the map.
///
/// @p clipLatRad/@p clipLonRad: same visibility pre-check as
/// @ref projectRings() (see its own doc comment for the details).
/// @param workspace The Workspace to project into.
/// @param input The loaded line geometry, from loadInputGeometry() (input_format.h).
/// @param frame See @ref projectRings()'s identical parameter.
/// @param scale Output units per kilometer.
/// @param clipLatRad See @ref projectRings()'s identical parameter.
/// @param clipLonRad See @ref projectRings()'s identical parameter.
/// @return Error::Ok on success, or Error::CapacityExceeded if the result
/// doesn't fit in @p workspace.
// See projectRings's identical parameter pair and NOLINT rationale above.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectLines(
    Workspace<MaxPoints, MaxRings, MaxRingPoints> &workspace,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const LambertConformalConicFrame &frame,
    float scale,
    float clipLatRad = kPi,
    float clipLonRad = kPi)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const Buffer<GeoPoint, InputMaxPoints> &inputPoints = input.points;
    const Buffer<std::size_t, InputMaxRings> &inputRingSizes = input.ringSizes;
    const Buffer<float, InputMaxRings> &inputRingMinLat = input.ringMinLat;
    const Buffer<float, InputMaxRings> &inputRingMaxLat = input.ringMaxLat;

    workspace.stageB.clear();
    workspace.ringSizesB.clear();

    const float visLatMin = frame.originLatRad - clipLatRad;
    const float visLatMax = frame.originLatRad + clipLatRad;

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

        // See projectRings()'s identical pass and its own comment on why
        // the shifted-window checks below are needed (a run's own first
        // point sitting exactly on the wrap boundary).
        float unwrappedLon = wrenium::geo::detail::wrapPi(line[0].lonRad - frame.originLonRad);
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
        const bool inWindow = detail::inLonWindow(lonMin, lonMax, clipLonRad);
        const bool inWindowShiftedDown = detail::inLonWindow(lonMin - 2.0f * kPi, lonMax - 2.0f * kPi, clipLonRad);
        const bool inWindowShiftedUp = detail::inLonWindow(lonMin + 2.0f * kPi, lonMax + 2.0f * kPi, clipLonRad);
        if (!inWindow && !inWindowShiftedDown && !inWindowShiftedUp) {
            continue;
        }

        // No rotation needed -- an open line has no "array wraparound" to
        // avoid splitting across the way a closed ring does, so it
        // simply walks from its own first point.
        float x = wrenium::geo::detail::wrapPi(line[0].lonRad - frame.originLonRad);
        std::size_t pieceSize = 1;

        wrenium::geo::detail::PointStorage firstStorage;
        firstStorage = project(line[0], frame, scale);
        Error err = workspace.stageB.pushBack(firstStorage);
        if (err != Error::Ok) {
            return err;
        }

        for (std::size_t i = 0; i + 1 < lineSize; ++i) {
            const GeoPoint &current = line[i];
            const GeoPoint &next = line[i + 1];
            const float delta = wrenium::geo::detail::wrapPi(next.lonRad - current.lonRad);
            const detail::LonBoundaryStep step = detail::stepAcrossLonBoundary(x, delta);

            if (!step.crossesPositive && !step.crossesNegative) {
                x = step.candidate;
                wrenium::geo::detail::PointStorage storage;
                storage = project(next, frame, scale);
                const Error pushErr = workspace.stageB.pushBack(storage);
                if (pushErr != Error::Ok) {
                    return pushErr;
                }
                ++pieceSize;
                continue;
            }

            // See projectRings()'s identical crossing/interpolation
            // comment.
            const float boundary = step.crossesPositive ? kPi : -kPi;
            const float t = (boundary - x) / delta;
            const float crossLat = current.latRad + t * (next.latRad - current.latRad);
            const Point exitPoint = projectAtTheta(crossLat, frame.n * boundary, frame, scale);

            wrenium::geo::detail::PointStorage exitStorage;
            exitStorage = exitPoint;
            Error exitErr = workspace.stageB.pushBack(exitStorage);
            if (exitErr != Error::Ok) {
                return exitErr;
            }
            ++pieceSize;

            if (pieceSize >= 2) {
                const Error runErr = workspace.ringSizesB.pushBack(pieceSize);
                if (runErr != Error::Ok) {
                    return runErr;
                }
            } else {
                workspace.stageB.truncate(workspace.stageB.size() - pieceSize);
            }

            const float entryBoundary = -boundary;
            const Point entryPoint = projectAtTheta(crossLat, frame.n * entryBoundary, frame, scale);
            wrenium::geo::detail::PointStorage entryStorage;
            entryStorage = entryPoint;
            const Error entryErr = workspace.stageB.pushBack(entryStorage);
            if (entryErr != Error::Ok) {
                return entryErr;
            }
            pieceSize = 1;

            // The entry point above sits exactly at the boundary;
            // `next`'s own true position is slightly further along --
            // push that too, same as the non-crossing branch above would
            // if this edge hadn't crossed (see cylindrical_pipeline.h's
            // identical comment on why: every other point in this walk
            // is pushed exactly once, by whichever step first reaches
            // it, and `next` would otherwise be silently skipped).
            x = wrenium::geo::detail::wrapPi(next.lonRad - frame.originLonRad);
            wrenium::geo::detail::PointStorage afterEntry;
            afterEntry = project(next, frame, scale);
            const Error afterEntryErr = workspace.stageB.pushBack(afterEntry);
            if (afterEntryErr != Error::Ok) {
                return afterEntryErr;
            }
            ++pieceSize;
        }

        if (pieceSize >= 2) {
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

} // namespace wrenium::geo::conic
