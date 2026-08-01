// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/azimuthal/clip.h"
#include "wrenium/geo/detail/azimuthal/equidistant.h"
#include "wrenium/geo/detail/azimuthal/orthographic.h"
#include "wrenium/geo/error.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"
#include "wrenium/geo/projection.h"
#include "wrenium/geo/workspace.h"

/// @file
/// Orchestrates rotate -> clip -> project over a Workspace -- the pipeline
/// this projection family needs that cylindrical (cylindrical_pipeline.h)
/// doesn't: rotating the sphere so the projection center becomes the pole,
/// then clipping coastline/border data down to a configurable radius
/// around it, then projecting what's left with a closed-form
/// radial-distance formula (equidistant.h or orthographic.h).

namespace wrenium::geo::azimuthal {

/// Selects which radial-distance formula projectRings()/projectLines()/
/// projectPoint() apply after rotate/clip -- a runtime value, not a
/// template parameter: every real caller (the Qt bridge's useOrthographic
/// toggle, for example) picks this at runtime, not at compile time, so a
/// template parameter here would only add syntax without reflecting how
/// the choice is actually made. Each projectionType's own per-point work stays
/// fully templated internally (see detail::projectRings() etc. below) --
/// this only costs one branch per call, not per point.
enum class ProjectionType
{
    Equidistant,  ///< True distance/bearing preserved from center (equidistant.h).
    Orthographic, ///< Rendered as if viewed from infinity; horizon at 90 degrees (orthographic.h).
};

namespace detail {

/// The actual per-projectionType rotate -> clip -> project implementation,
/// templated on the radial-distance formula for zero-overhead inlining
/// over every point in @p input -- not meant to be called directly.
/// projectRings() (below, no `detail::`) dispatches to one instantiation
/// of this per call, based on its own runtime ProjectionType parameter; both
/// instantiations exist in the binary (one per projectionType actually used),
/// but each is exactly as tightly inlined as if ProjectFn had been
/// chosen at compile time, because from this function's own point of
/// view, it still is.
/// @param workspace The Workspace to clip/project into.
/// @param input The loaded ring geometry, from loadInputGeometry() for example.
/// @param center The projection center (lat/lon, radians).
/// @param clipRadiusRad Clip radius in radians (angular) -- convert from
/// whatever "zoom preset" unit the caller's own code uses, for example
/// `clipRadiusKm / kEarthRadiusKm`.
/// @param scale Output units per kilometer.
/// @tparam ProjectFn Radial-distance formula applied after rotate/clip.
/// @return Error::Ok on success, or Error::CapacityExceeded if the result
/// doesn't fit in @p workspace.
// clipRadiusRad/scale are documented and always passed in this order
// across every call site in this codebase (see @param above); every
// azimuthal projectX function shares it deliberately, so reordering one
// alone would be its own hazard.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <Point (*ProjectFn)(const GeoPoint &, float), std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t OutputCharCapacity, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectRings(
    Workspace<MaxPoints, MaxRings, MaxRingPoints, OutputCharCapacity> &workspace,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const GeoPoint &center,
    float clipRadiusRad,
    float scale)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const Buffer<GeoPoint, InputMaxPoints> &inputPoints = input.points;
    const Buffer<std::size_t, InputMaxRings> &inputRingSizes = input.ringSizes;
    const Buffer<float, InputMaxRings> &inputRingMinLat = input.ringMinLat;
    const Buffer<float, InputMaxRings> &inputRingMaxLat = input.ringMaxLat;

    // ---- clip (rotating lazily as needed), per ring, with a whole-ring
    // pre-filter ----
    //
    // Profiling found rotate() costs ~35x what clip()'s per-point check
    // costs (atan2-based distance+bearing vs. one comparison). A ring can
    // only contribute a surviving point if at least one of its
    // (unrotated) points has true angular
    // distance from `center` within clipRadiusRad. That true distance is
    // always >= the plain difference in latitude alone -- a provable
    // spherical-trig fact (cos(centralAngle) <= cos(latDelta) always, since
    // cos(centralAngle) - cos(latDelta) = cosLat1*cosLat2*(cos(dLon) - 1),
    // which is <= 0). So if a ring's raw latitude range doesn't overlap
    // [center.latRad - clipRadiusRad, center.latRad + clipRadiusRad] at
    // all, no point in it can possibly survive clipping, and the whole
    // ring can be skipped without rotating or clipping any of its points --
    // verified against the real world coastline dataset to skip ~94% of
    // rings at a 2,000 km clip radius, ~30% even at 8,000 km.
    //
    // Within a ring that *does* pass this filter, clipRingToSink (detail/azimuthal/clip.h)
    // now rotates individual points lazily rather than requiring the whole
    // ring pre-rotated -- most of a kept ring's points still don't survive
    // clipping at typical (non-whole-world) radii, measured 90-98% wasted
    // rotate() calls before this change.
    workspace.stageB.clear();
    workspace.ringSizesB.clear();

    const float bandMinLat = center.latRad - clipRadiusRad;
    const float bandMaxLat = center.latRad + clipRadiusRad;

    // At a large enough clip radius the [bandMinLat, bandMaxLat] band
    // already covers every possible latitude, so every ring is guaranteed
    // to pass the check below -- skip the scan itself in that case (it
    // measured ~3% pure overhead at a whole-world clip radius, since
    // nothing ever gets filtered there).
    const bool wholeWorldVisible = (bandMinLat <= -kHalfPi && bandMaxLat >= kHalfPi);

    std::size_t inputOffset = 0;
    for (std::size_t r = 0; r < inputRingSizes.size(); ++r) {
        const std::size_t ringSize = inputRingSizes[r];

        if (!wholeWorldVisible) {
            // minLat/maxLat are precomputed once (loadInputGeometry) rather
            // than rescanned here on every recompute -- the input geometry
            // itself never changes between recomputes, only center/
            // clipRadiusRad do, so re-deriving a static fact from scratch on
            // every single call was pure wasted work (measured ~7.8us over
            // the real dataset's point count -- see loadInputGeometry's own
            // comment).
            const float minLat = inputRingMinLat[r];
            const float maxLat = inputRingMaxLat[r];

            if (maxLat < bandMinLat || minLat > bandMaxLat) {
                // Guaranteed: no point in this ring can be inside the clip
                // circle. Skip rotate()/clip() for it entirely -- matches
                // the same "ring fully outside" outcome clip() would have
                // reached anyway, just without paying for it.
                inputOffset += ringSize;
                continue;
            }
        }

        // ringRotatedCache (workspace.h) must hold at least this ring's own
        // point count -- clipRingToSink writes into it indexed by ring
        // position while rotating lazily below.
        if (ringSize > MaxRingPoints) {
            return Error::CapacityExceeded;
        }

        // ---- clip this ring directly from the raw input points: input ->
        // stageB, rotating lazily inside clipRingToSink. A single input
        // ring can clip into more than one topologically disjoint output
        // cycle (e.g. two widely-separated capes of the same landmass both
        // poking into an otherwise-distant view) -- clipRingToSink reports
        // each one via the ring-boundary callback below, and each becomes
        // its own separate entry in ringSizesB, so the emitters draw them
        // as independent closed subpaths instead of one incorrect loop
        // with a spurious connecting edge between two separate pieces. ----
        std::size_t outSize = 0;
        const Error err = azimuthal::clipRingToSink(
            &inputPoints[inputOffset], ringSize, center, clipRadiusRad, workspace.ringRotatedCache,
            [&workspace](const GeoPoint &p) { return workspace.stageB.pushBack(wrenium::geo::detail::PointStorage(p)); },
            [&workspace](std::size_t cycleSize) -> Error {
                if (cycleSize < 3) {
                    // Not a usable closed shape -- drop just this cycle's
                    // points without disturbing any others already kept.
                    workspace.stageB.truncate(workspace.stageB.size() - cycleSize);
                    return Error::Ok;
                }
                return workspace.ringSizesB.pushBack(cycleSize);
            },
            outSize);
        if (err != Error::Ok) {
            return err;
        }

        inputOffset += ringSize;
    }

    // ---- fully-enclosed fallback ----
    //
    // If no ring produced any surviving output, that's ambiguous: it means
    // either the whole visible clip circle is outside every input ring (the
    // common case -- nothing to draw is correct), or the whole clip circle
    // sits inside one of them (e.g. the center is deep inside a landmass at
    // a clip radius too small to reach any coastline) -- in which case the
    // correct visible shape is the *entire* clip circle, not nothing. Only
    // paid for in this rare zero-output case; the common case (some ring
    // did produce output) never runs this O(total input points) fallback
    // scan at all.
    if (workspace.ringSizesB.size() == 0) {
        if (azimuthal::isCenterEnclosedByRings(inputPoints.data(), inputRingSizes, center)) {
            std::size_t outSize = 0;
            const Error err = azimuthal::detail::emitFullClipCircle(
                clipRadiusRad,
                [&workspace](const GeoPoint &p) { return workspace.stageB.pushBack(wrenium::geo::detail::PointStorage(p)); },
                outSize);
            if (err != Error::Ok) {
                return err;
            }
            if (outSize >= 3) {
                const Error ringErr = workspace.ringSizesB.pushBack(outSize);
                if (ringErr != Error::Ok) {
                    return ringErr;
                }
            }
        }
    }

    // ---- project: stageB in place (GeoPoint .geo -> Point .point) ----
    for (std::size_t i = 0; i < workspace.stageB.size(); ++i) {
        const GeoPoint rotatedClipped = workspace.stageB[i].geo;
        workspace.stageB[i].point = ProjectFn(rotatedClipped, scale);
    }

    return Error::Ok;
}

/// Border-line counterpart to @ref projectRings(): see its own comment --
/// not meant to be called directly.
// See detail::projectRings's identical parameter pair and NOLINT rationale above.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <Point (*ProjectFn)(const GeoPoint &, float), std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t OutputCharCapacity, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectLines(
    Workspace<MaxPoints, MaxRings, MaxRingPoints, OutputCharCapacity> &workspace,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const GeoPoint &center,
    float clipRadiusRad,
    float scale)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const Buffer<GeoPoint, InputMaxPoints> &inputPoints = input.points;
    const Buffer<std::size_t, InputMaxRings> &inputRingSizes = input.ringSizes;
    const Buffer<float, InputMaxRings> &inputRingMinLat = input.ringMinLat;
    const Buffer<float, InputMaxRings> &inputRingMaxLat = input.ringMaxLat;

    workspace.stageB.clear();
    workspace.ringSizesB.clear();

    const float bandMinLat = center.latRad - clipRadiusRad;
    const float bandMaxLat = center.latRad + clipRadiusRad;
    const bool wholeWorldVisible = (bandMinLat <= -kHalfPi && bandMaxLat >= kHalfPi);

    std::size_t inputOffset = 0;
    for (std::size_t r = 0; r < inputRingSizes.size(); ++r) {
        const std::size_t lineSize = inputRingSizes[r];

        if (!wholeWorldVisible && lineSize > 0) {
            // Precomputed once (loadInputGeometry) rather than rescanned
            // here on every recompute -- see projectRings's identical
            // comment / loadInputGeometry's own comment for the measured
            // saving.
            const float minLat = inputRingMinLat[r];
            const float maxLat = inputRingMaxLat[r];

            if (maxLat < bandMinLat || minLat > bandMaxLat) {
                inputOffset += lineSize;
                continue;
            }
        }

        // ---- clip this polyline directly from the raw input points:
        // input -> stageB, rotating lazily inside clipLineToSink. A single
        // input line can clip into more than one surviving run (it can
        // exit and re-enter the clip circle any number of times) --
        // clipLineToSink reports each one via the run-boundary callback
        // below, and each becomes its own separate entry in ringSizesB, so
        // the emitters draw them as independent open subpaths instead of
        // one incorrect run with a spurious connecting segment between two
        // separate pieces. ----
        std::size_t outSize = 0;
        const Error err = azimuthal::clipLineToSink(
            &inputPoints[inputOffset], lineSize, center, clipRadiusRad,
            [&workspace](const GeoPoint &p) { return workspace.stageB.pushBack(wrenium::geo::detail::PointStorage(p)); },
            [&workspace](std::size_t runSize) -> Error {
                if (runSize < 2) {
                    // Not a usable drawable segment -- drop just this run's
                    // points without disturbing any others already kept.
                    workspace.stageB.truncate(workspace.stageB.size() - runSize);
                    return Error::Ok;
                }
                return workspace.ringSizesB.pushBack(runSize);
            },
            outSize);
        if (err != Error::Ok) {
            return err;
        }

        inputOffset += lineSize;
    }

    // ---- project: stageB in place (GeoPoint .geo -> Point .point) ----
    for (std::size_t i = 0; i < workspace.stageB.size(); ++i) {
        const GeoPoint rotatedClipped = workspace.stageB[i].geo;
        workspace.stageB[i].point = ProjectFn(rotatedClipped, scale);
    }

    return Error::Ok;
}

/// projectPoint()'s actual implementation -- see its own comment; not
/// meant to be called directly.
template <Point (*ProjectFn)(const GeoPoint &, float)>
inline Point projectPoint(const GeoPoint &rotated, float scale) // NOLINT(bugprone-easily-swappable-parameters)
{
    return ProjectFn(rotated, scale);
}

/// unproject()'s actual implementation -- see its own comment; not meant
/// to be called directly.
template <GeoPoint (*UnprojectFn)(const Point &, float)>
inline GeoPoint unproject(const Point &point, const GeoPoint &center, float scale)
{
    return azimuthal::unrotate(UnprojectFn(point, scale), center);
}

} // namespace detail

/// Rotates -> clips -> projects @p input's closed coastline-style rings
/// into @p workspace (already loaded via loadInputGeometry() (input_format.h),
/// for example). Read the result back via `workspace.projectedPoints()` /
/// `workspace.projectedRingSizes()` (or `workspace.projectedPoint()` for a
/// single point), for the emitters or a caller's own code to consume.
///
/// @p input's own capacities (InputMaxPoints/InputMaxRings) are
/// deliberately independent template parameters from the workspace's
/// MaxPoints/MaxRings -- a fixed input dataset (the converter's checked-in
/// world coastline data, for example) may be loaded into workspaces of
/// different budgets.
/// @param workspace The Workspace to clip/project into.
/// @param input The loaded ring geometry, from loadInputGeometry() for example.
/// @param center The projection center (lat/lon, radians).
/// @param clipRadiusRad Clip radius in radians (angular) -- convert from
/// whatever "zoom preset" unit the caller's own code uses, for example
/// `clipRadiusKm / kEarthRadiusKm`.
/// @param scale Output units per kilometer.
/// @param projectionType Radial-distance formula applied after rotate/clip.
/// Costs one branch for the whole call, not per point -- the per-point
/// work stays exactly as templated/inlined as a compile-time choice would
/// give (see detail::projectRings() above).
/// @return Error::Ok on success, or Error::CapacityExceeded if the result
/// doesn't fit in @p workspace.
// clipRadiusRad/scale/projectionType are documented and always passed in this
// order across every call site in this codebase; every azimuthal
// projectX function shares it deliberately, so reordering one alone
// would be its own hazard.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t OutputCharCapacity, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectRings(
    Workspace<MaxPoints, MaxRings, MaxRingPoints, OutputCharCapacity> &workspace,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const GeoPoint &center,
    float clipRadiusRad,
    float scale,
    ProjectionType projectionType)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    return projectionType == ProjectionType::Orthographic
        ? detail::projectRings<azimuthal::projectOrthographic>(workspace, input, center, clipRadiusRad, scale)
        : detail::projectRings<azimuthal::projectEquidistant>(workspace, input, center, clipRadiusRad, scale);
}

/// Border-line counterpart to @ref projectRings(): rotate -> clip -> project
/// over the same kind of Workspace, but for *open* polyline data (country
/// border segments, for example) rather than closed coastline rings.
/// Deliberately a separate function, not a mode flag on @ref projectRings() --
/// border data has no inside/outside concept at all.
/// Callers are expected to use a Workspace instance of their own,
/// independent from the one used for coastline data.
/// @param workspace The Workspace to clip/project into.
/// @param input The loaded line geometry, from loadInputGeometry() for example.
/// @param center The projection center (lat/lon, radians).
/// @param clipRadiusRad Clip radius in radians (angular).
/// @param scale Output units per kilometer.
/// @param projectionType See @ref projectRings()'s identical parameter.
/// @return Error::Ok on success, or Error::CapacityExceeded if the result
/// doesn't fit in @p workspace.
// See projectRings's identical parameter pair and NOLINT rationale above.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t OutputCharCapacity, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectLines(
    Workspace<MaxPoints, MaxRings, MaxRingPoints, OutputCharCapacity> &workspace,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const GeoPoint &center,
    float clipRadiusRad,
    float scale,
    ProjectionType projectionType)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    return projectionType == ProjectionType::Orthographic
        ? detail::projectLines<azimuthal::projectOrthographic>(workspace, input, center, clipRadiusRad, scale)
        : detail::projectLines<azimuthal::projectEquidistant>(workspace, input, center, clipRadiusRad, scale);
}

/// A single projected point and whether it fell inside the clip circle.
struct ProjectedPoint
{
    Point point;          ///< Valid only when #visible is true.
    bool visible = false; ///< True iff the input point lies within the clip circle.
};

/// Projects one arbitrary point (a station marker or waypoint, for
/// example, or any other caller-supplied annotation not necessarily part
/// of the coastline/border datasets) through the exact same rotate -> clip ->
/// project math @ref projectRings() / @ref projectLines() use for the map's own
/// geometry, so a marker positioned at the returned point lines up
/// exactly with the SVG/binary path output: (0, 0) at @p center, @p scale
/// output units per km.
///
/// ProjectedPoint::visible is false whenever the point falls outside the
/// clip circle; ProjectedPoint::point is left at its default (0, 0) in
/// that case rather than an extrapolated off-circle position.
/// @param rawPoint The point to project.
/// @param center The projection center (lat/lon, radians).
/// @param clipRadiusRad Clip radius in radians (angular).
/// @param scale Output units per kilometer.
/// @param projectionType Radial-distance formula applied after rotate/clip --
/// must match whatever @ref projectRings() / @ref projectLines() call was
/// used for the same map, or marker positions won't line up. See
/// @ref projectRings()'s identical parameter.
/// @return The projected point and its visibility.
// See projectRings's identical parameter pair and NOLINT rationale above.
inline ProjectedPoint projectPoint(const GeoPoint &rawPoint, const GeoPoint &center, float clipRadiusRad, float scale, ProjectionType projectionType) // NOLINT(bugprone-easily-swappable-parameters)
{
    ProjectedPoint result;
    result.point = Point{};

    if (azimuthal::detail::isCheaplyOutside(rawPoint, center, clipRadiusRad)) {
        result.visible = false;
        return result;
    }

    const GeoPoint rotated = azimuthal::rotate(rawPoint, center);
    if (!azimuthal::detail::isInsideClipCircle(rotated, clipRadiusRad)) {
        result.visible = false;
        return result;
    }

    result.visible = true;
    result.point = projectionType == ProjectionType::Orthographic
        ? detail::projectPoint<azimuthal::projectOrthographic>(rotated, scale)
        : detail::projectPoint<azimuthal::projectEquidistant>(rotated, scale);
    return result;
}

/// Inverse of #projectPoint (and, unclipped, of projectEquidistant() itself):
/// given a planar output point and the same (center, scale) it was
/// projected with, recovers the geo point -- no visibility/clip-circle
/// test, since the caller's own point (e.g. a screen click) already tells
/// them whether it's within the rendered area; this just answers "what
/// geo point is there."
///
/// Composes the radial-distance formula's own inverse with unrotate()
/// (rotation.h) -- the exact mirror of how #projectPoint composes its own
/// forward formula with rotate(). @p projectionType must match whatever
/// projectionType #projectRings() / #projectLines() / #projectPoint() was
/// called with for this same map, or the result won't correspond to what
/// was actually rendered.
/// @param point The planar point to invert (same coordinate space
/// #projectPoint()'s return value uses).
/// @param center See #projectPoint()'s identical parameter.
/// @param scale Output units per kilometer -- must match whatever the
/// point was originally projected with.
/// @param projectionType See #projectRings()'s identical parameter.
/// @return The geo point that this same @p projectionType/rotate() would map to @p point.
inline GeoPoint unproject(const Point &point, const GeoPoint &center, float scale, ProjectionType projectionType) // NOLINT(bugprone-easily-swappable-parameters)
{
    return projectionType == ProjectionType::Orthographic
        ? detail::unproject<azimuthal::unprojectOrthographic>(point, center, scale)
        : detail::unproject<azimuthal::unprojectEquidistant>(point, center, scale);
}

} // namespace wrenium::geo::azimuthal
