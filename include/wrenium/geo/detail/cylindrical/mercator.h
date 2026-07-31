// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "wrenium/f32math/asin.h"
#include "wrenium/f32math/atanh.h"
#include "wrenium/f32math/tanh.h"
#include "wrenium/f32math/trig.h"
#include "wrenium/geo/detail/angle.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/projection.h"

/// @file
/// Web Mercator (spherical, not ellipsoidal -- treats lat/lon as if on a
/// perfect sphere, kEarthRadiusKm, same as the rest of this library, not
/// WGS84). Unlike the azimuthal family
/// (detail/azimuthal/), there's no rotate()/RotationFrame step: `center`
/// is a true 2D recenter point (always maps to (0, 0)), but it's applied
/// as two independent offsets (a longitude subtraction for x, a
/// projected-y subtraction for y) rather than a rotation -- a cylindrical
/// projection isn't center-relative the way an azimuthal one is. Takes a
/// raw GeoPoint directly.

namespace wrenium::geo::cylindrical {

/// The standard "Web Mercator" pole-latitude limit (~85.0511 degrees) --
/// the actual point where `atanh(sin(lat)) == kPi`, making the projected
/// map exactly as tall as it is wide at world scale. Not a number chosen
/// for this project: every web map tile scheme uses it, since Mercator's
/// own y diverges to infinity at the true poles.
constexpr float kMercatorMaxLatRad = 1.4844222297452172f;

namespace detail {

/// The y half of the projection, shared by #project (single-point use)
/// and cylindrical_pipeline.h's ring/run walk (which computes x itself,
/// via longitude accumulated along a piece rather than a fresh per-point
/// wrap -- see that file's own comment for why). Latitude is silently
/// clamped to +-kMercatorMaxLatRad first (see #kMercatorMaxLatRad) --
/// Mercator's own y has no finite value at the true poles.
inline float projectY(float latRad, float scale) // NOLINT(bugprone-easily-swappable-parameters)
{
    float clampedLat = latRad;
    if (clampedLat > kMercatorMaxLatRad) {
        clampedLat = kMercatorMaxLatRad;
    } else if (clampedLat < -kMercatorMaxLatRad) {
        clampedLat = -kMercatorMaxLatRad;
    }
    return -f32math::atanh(f32math::sin(clampedLat)) * kEarthRadiusKm * scale;
}

} // namespace detail

/// Forward spherical Mercator projection, for a single isolated point
/// (e.g. a marker) -- not used internally by cylindrical_pipeline.h's own
/// ring/run walk, which needs longitude accumulated across a whole piece
/// rather than wrapped independently per point (see that file's comment).
/// @param point The point to project (raw sphere-space, not rotated).
/// @param center The recenter point -- `project(center, center, scale)`
/// is always `(0, 0)`.
/// @param scale Output units per kilometer.
/// @return The planar (x, y) projection. See #detail::projectY for the
/// pole-latitude clamping this applies (to both @p point and @p center).
inline Point project(const GeoPoint &point, const GeoPoint &center, float scale)
{
    const float lonDelta = wrenium::geo::detail::wrapPi(point.lonRad - center.lonRad);

    Point projected;
    projected.x = lonDelta * kEarthRadiusKm * scale;
    projected.y = detail::projectY(point.latRad, scale) - detail::projectY(center.latRad, scale);
    return projected;
}

/// Inverse of #project: given a planar output point and the same
/// (center, scale) #project() was called with, recovers the geo point.
/// x-inversion is exact (linear in longitude); y-inversion composes
/// f32math::asin() with f32math::tanh() (tanh.h), the latter's fitted
/// domain being exactly `[-kPi, kPi]` -- clamped to that range here
/// first, so a @p point far outside the valid map area (e.g. a click
/// past the rendered edge) saturates towards the nearest pole instead of
/// hitting tanh()'s undefined behavior outside its fitted domain.
/// @param point The planar point to invert (same coordinate space
/// #project()'s return value uses).
/// @param center See #project()'s identical parameter.
/// @param scale Output units per kilometer.
/// @return The geo point that #project() would map to @p point.
inline GeoPoint unproject(const Point &point, const GeoPoint &center, float scale)
{
    const float lonRad = wrenium::geo::detail::wrapPi(center.lonRad + point.x / (kEarthRadiusKm * scale));

    const float centerY = detail::projectY(center.latRad, scale);
    float yArg = -(point.y + centerY) / (kEarthRadiusKm * scale);
    if (yArg > kPi) {
        yArg = kPi;
    } else if (yArg < -kPi) {
        yArg = -kPi;
    }
    const float latRad = f32math::asin(f32math::tanh(yArg));

    return GeoPoint{latRad, lonRad};
}

/// Clamps a candidate center latitude so the *whole current viewport*
/// stays within Mercator's valid y range (+-#kMercatorMaxLatRad's own
/// projected y), rather than clamping the center's own latitude to that
/// fixed bound regardless of zoom. A flat +-kMercatorMaxLatRad clamp on
/// the center alone is only correct once the viewport is much shorter
/// than the map's full valid height; at a wide zoom (a large
/// @p viewportHeightPx relative to @p scale) it lets the center get
/// dragged so close to a pole that half the viewport shows nothing but
/// that pole's own dead zone (#detail::projectY's clamp collapses
/// everything past +-kMercatorMaxLatRad to the same y), with real
/// content -- e.g. Antarctica's coastline -- pulled to the vertical
/// middle of the screen instead of stopping near the edge like a normal
/// "you've panned as far as the map goes" limit. Confirmed by direct
/// measurement: at halfWidthKm 16000 (a wide zoom), dragging the center
/// to the old flat clamp put Antarctica's coastline at the exact
/// vertical center of the viewport, with the entire bottom half showing
/// solid dead space.
/// @param latRad Candidate center latitude, radians.
/// @param scale Output units per kilometer, as passed to #project.
/// @param viewportHeightPx The current viewport's full height, in the
/// same output units #project()/#unproject() use (i.e. screen pixels).
/// @return @p latRad, clamped so the viewport's own top/bottom edges
/// (at `+-viewportHeightPx/2` from center) never extend past
/// `+-kMercatorMaxLatRad`'s own projected y. If the viewport is taller
/// than the map's entire valid height, returns exactly 0 (the equator)
/// -- there's no latitude that avoids dead space on both edges at once,
/// so centering is the least-bad choice, matching how standard web map
/// libraries lock vertical panning entirely once zoomed out that far.
inline float clampCenterLatForViewport(float latRad, float scale, float viewportHeightPx) // NOLINT(bugprone-easily-swappable-parameters)
{
    const float halfHeightPx = viewportHeightPx * 0.5f;
    const float maxY = -detail::projectY(kMercatorMaxLatRad, scale); // always positive
    const float maxCenterY = maxY - halfHeightPx;
    if (maxCenterY <= 0.0f) {
        return 0.0f;
    }

    float centerY = detail::projectY(latRad, scale);
    if (centerY > maxCenterY) {
        centerY = maxCenterY;
    } else if (centerY < -maxCenterY) {
        centerY = -maxCenterY;
    }

    const float yArg = -centerY / (kEarthRadiusKm * scale);
    return f32math::asin(f32math::tanh(yArg));
}

} // namespace wrenium::geo::cylindrical
