// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cmath>

#include "wrenium/f32math/atan2.h"
#include "wrenium/f32math/exp.h"
#include "wrenium/f32math/log.h"
#include "wrenium/f32math/trig.h"
#include "wrenium/geo/detail/angle.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/projection.h"

/// @file
/// Lambert conformal conic, spherical -- the same kEarthRadiusKm
/// convention as the rest of this library. The cone's own axis already
/// passes through the poles, so a point only needs its latitude and its
/// longitude offset from the central meridian -- no rotate()/
/// RotationFrame step here (the azimuthal family, detail/azimuthal/,
/// needs that step, to re-express a point relative to an arbitrary
/// center). There's no whole-world antimeridian-crossing support either
/// -- conic_pipeline.h's own top comment covers why (a deliberate domain
/// restriction); the cylindrical family, detail/cylindrical/, has that.
///
/// Two standard parallels fix the cone: it touches the sphere exactly
/// along both, is a secant cone between them, and its scale factor
/// diverges outside them (coordinates stay well-defined throughout) --
/// the same shape a real paper cone wrapped around the globe and cut
/// open would have. `n` (the cone constant) is the ratio of the cone's
/// own unrolled angle to a true 360 degrees of longitude; every point's
/// projected angle `theta = n * (lon - originLon)` is scaled by it,
/// which is *why* the cone unrolls into a wedge narrower than a full
/// circle, rather than the closed ring a cylindrical projection's x
/// forms.
///
/// The cone constant and scale factor both need a fractional power
/// (`tan(...)^n`, with `n` typically fractional) -- computed as
/// `exp(n * log(tan(...)))` via wrenium-f32math's log()/exp() (log.h/
/// exp.h in that library); every other projection in this codebase only
/// ever needs sin/cos/atan2/asin. That also means this file's own
/// project()/unproject() can't be `constexpr` the way the azimuthal
/// family's radial-distance formulas are (log()/exp()'s own IEEE-754
/// range reduction isn't constexpr-usable in C++17) --
/// irrelevant to this library's constexpr SVG generation
/// (azimuthal_constexpr_svg.h/cylindrical_constexpr_svg.h), which only
/// the azimuthal/cylindrical families participate in.
///
/// unproject()'s own latitude recovery is `2*atan(t) - pi/2` (Snyder's
/// own standard substitution) -- doubling whatever error the one
/// atan2() call inside it carries, which dominates this projection's
/// own round-trip accuracy at roughly 1.2e-3 rad (~7 km at Earth's
/// scale), well above every other projection's own error tier in this
/// library. `n`'s own frame computation (a ratio of two logs) is the
/// other real accuracy-sensitive spot -- computing each side as a
/// single log() of a ratio (see makeLambertConformalConicFrame()'s own
/// comment) avoids most of a real, measured error two separately-
/// subtracted log() calls carry when the two standard parallels sit
/// close together.

namespace wrenium::geo::conic {

/// The two standard parallels and the origin (reference latitude/central
/// meridian) that fix a Lambert conformal conic projection -- the raw
/// parameters a caller chooses; see makeLambertConformalConicFrame() for
/// the precomputed form project()/unproject() actually use.
struct LambertConformalConic
{
    float standardParallel1Rad; ///< First standard parallel, radians.
    float standardParallel2Rad; ///< Second standard parallel, radians -- may equal the first, giving a tangent cone (touching the sphere along a single circle).
    float originLatRad;         ///< Reference latitude: maps to y == 0.
    float originLonRad;         ///< Central meridian: maps to x == 0.
};

/// Precomputed once-per-LambertConformalConic quantities project()/
/// unproject() need on every call -- see makeLambertConformalConicFrame().
struct LambertConformalConicFrame
{
    float n;            ///< Cone constant.
    float rhoScale;     ///< kEarthRadiusKm * F (the projection's own scale factor, combined with Earth's radius).
    float rho0;         ///< Radius at the origin latitude -- y == rho0 - rho*cos(theta), so this is the offset that makes the origin itself map to y == 0.
    float originLatRad; ///< Reference latitude, copied from LambertConformalConic for conic_pipeline.h's own clipLatRad pre-check (project()/unproject() itself only need rho0, which already bakes it in).
    float originLonRad; ///< Central meridian, copied from LambertConformalConic for project()/unproject()'s own use.
};

namespace detail {

/// tan(pi/4 + latRad/2) -- the one sub-expression every quantity in this
/// file is built from (Snyder's own "Lambert Conformal Conic" formula uses
/// this same substitution throughout). Computed via sincos rather than a
/// direct tan() (wrenium-f32math has no standalone tan -- see gnomonic.h's
/// identical reasoning).
inline float conicT(float latRad)
{
    float s = 0.0f, c = 0.0f;
    f32math::sincos(kHalfPi * 0.5f + latRad * 0.5f, s, c);
    return s / c;
}

} // namespace detail

/// Builds the once-per-LambertConformalConic LambertConformalConicFrame
/// project()/unproject() need. Degenerates cleanly to a tangent cone
/// (`n = sin(standardParallel1Rad)`) when the two standard parallels are
/// equal (or within float precision of it) -- the general two-parallel
/// formula for `n` is a 0/0 indeterminate form there.
/// @param params The two standard parallels and the origin point.
inline LambertConformalConicFrame makeLambertConformalConicFrame(const LambertConformalConic &params)
{
    const float phi1 = params.standardParallel1Rad;
    const float phi2 = params.standardParallel2Rad;

    float sinPhi1 = 0.0f, cosPhi1 = 0.0f;
    f32math::sincos(phi1, sinPhi1, cosPhi1);

    float n;
    if (std::fabs(phi1 - phi2) < 1e-6f) {
        n = sinPhi1;
    } else {
        float sinPhi2 = 0.0f, cosPhi2 = 0.0f;
        f32math::sincos(phi2, sinPhi2, cosPhi2);
        // log() of each ratio directly: the two standard parallels are
        // often close together in practice, where cosPhi1/cosPhi2 and
        // T2/T1 sit close to 1. Subtracting two independently-
        // approximated logs near the same magnitude would lose most of
        // their shared error to cancellation in the numerator/
        // denominator that follows; log() of the ratio itself only ever
        // carries its own single approximation error (measured: two
        // standard parallels 5 degrees apart had n's own error fall by
        // ~400x with this formulation).
        const float numerator = f32math::log(cosPhi1 / cosPhi2);
        const float denominator = f32math::log(detail::conicT(phi2) / detail::conicT(phi1));
        n = numerator / denominator;
    }

    const float lnT1 = f32math::log(detail::conicT(phi1));
    const float F = cosPhi1 * f32math::exp(n * lnT1) / n;
    const float rhoScale = kEarthRadiusKm * F;

    const float lnT0 = f32math::log(detail::conicT(params.originLatRad));
    const float rho0 = rhoScale * f32math::exp(-n * lnT0);

    LambertConformalConicFrame frame;
    frame.n = n;
    frame.rhoScale = rhoScale;
    frame.rho0 = rho0;
    frame.originLatRad = params.originLatRad;
    frame.originLonRad = params.originLonRad;
    return frame;
}

namespace detail {

/// `rho(latRad)` -- the radial distance from the cone's own apex, shared
/// by project() and projectAtTheta() below. Independent of longitude/
/// theta entirely (see this file's own overview comment: only latitude
/// and the origin's longitude offset feed the projection at all).
inline float rho(float latRad, const LambertConformalConicFrame &frame)
{
    const float lnT = f32math::log(conicT(latRad));
    return frame.rhoScale * f32math::exp(-frame.n * lnT);
}

} // namespace detail

/// Forward spherical Lambert conformal conic projection.
/// @param point The point to project (raw sphere-space).
/// @param frame A frame built by makeLambertConformalConicFrame().
/// @param scale Output units per kilometer.
/// @return The planar (x, y) projection -- (0, 0) at the frame's own
/// origin point. North-up, screen convention (y increases downward,
/// matching every other projection in this library, where Snyder's own
/// formula uses traditional map-space convention: y increases
/// northward) -- so this is `rho*cos(theta) - rho0`, the sign-flipped
/// form of Snyder's own `rho0 - rho*cos(theta)`.
inline Point project(const GeoPoint &point, const LambertConformalConicFrame &frame, float scale)
{
    const float rho = detail::rho(point.latRad, frame);
    const float theta = frame.n * wrenium::geo::detail::wrapPi(point.lonRad - frame.originLonRad);

    float sinTheta = 0.0f, cosTheta = 0.0f;
    f32math::sincos(theta, sinTheta, cosTheta);

    Point projected;
    projected.x = rho * sinTheta * scale;
    projected.y = (rho * cosTheta - frame.rho0) * scale;
    return projected;
}

/// Projects directly from (latRad, theta), theta already the exact final
/// wedge angle -- bypasses project()'s own `lon - originLon` subtraction
/// and wrapPi() call. Needed at a longitude-boundary crossing
/// (conic_pipeline.h's own antimeridian-relative-to-origin clipping): the
/// desired theta there is exactly `frame.n * (+-kPi)` (the wedge's own
/// two straight edges), but wrapPi's `(-kPi, kPi]` convention maps a
/// synthetic `originLonRad - kPi` to `+kPi`, silently collapsing the
/// negative edge onto the positive one. Taking theta directly sidesteps
/// that asymmetry entirely.
/// @param latRad Latitude to project (raw sphere-space).
/// @param theta The wedge angle to project at, typically `frame.n * kPi`
/// or `frame.n * -kPi` (this function does not wrap or clamp it).
/// @param frame See project()'s identical parameter.
/// @param scale See project()'s identical parameter.
/// @return The planar (x, y) projection, same convention as project().
// latRad/theta are documented and always passed in this order across
// every call site (conic_pipeline.h) -- their value ranges don't overlap
// in practice (a latitude in [-pi/2, pi/2] vs. an already-n-scaled wedge
// angle), so a swap would misbehave obviously rather than silently.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline Point projectAtTheta(float latRad, float theta, const LambertConformalConicFrame &frame, float scale)
{
    const float rho = detail::rho(latRad, frame);

    float sinTheta = 0.0f, cosTheta = 0.0f;
    f32math::sincos(theta, sinTheta, cosTheta);

    Point projected;
    projected.x = rho * sinTheta * scale;
    projected.y = (rho * cosTheta - frame.rho0) * scale;
    return projected;
}

/// Inverse of project(): given a planar output point and the same frame/
/// scale it was projected with, recovers the geo point.
/// @param point A planar point in project()'s own output space.
/// @param frame See project()'s identical parameter -- must match.
/// @param scale See project()'s identical parameter -- must match.
/// @return The geo point that project() would map to @p point.
inline GeoPoint unproject(const Point &point, const LambertConformalConicFrame &frame, float scale) // NOLINT(bugprone-easily-swappable-parameters)
{
    const float px = point.x / scale;
    const float py = point.y / scale;
    const float signN = (frame.n >= 0.0f) ? 1.0f : -1.0f;

    // rho*cos(theta) -- see project()'s own comment for why this is
    // `py + rho0`, the exact inverse of its own `rho*cosTheta - rho0`.
    const float dy = py + frame.rho0;
    const float rho = signN * std::sqrt(px * px + dy * dy);
    const float theta = f32math::atan2(signN * px, signN * dy);

    const float lon = wrenium::geo::detail::wrapPi(frame.originLonRad + theta / frame.n);
    const float lnT = f32math::log(frame.rhoScale / rho) / frame.n;
    const float t = f32math::exp(lnT);
    const float lat = 2.0f * f32math::atan2(t, 1.0f) - kHalfPi;

    return GeoPoint{lat, lon};
}

/// Clamps @p latRad so a frame built from it (same standard parallels as
/// @p frame, @p latRad as the origin) never puts the cone's own apex --
/// where the wedge's two open edges meet, rho == 0, this frame's finite
/// pole (makeLambertConformalConicFrame()'s own comment) -- close enough
/// to the origin that a @p halfWidthKm-wide viewport risks exposing the
/// wedge's own real angular gap (conic_pipeline.h's own overview comment:
/// the cone unrolls into a wedge narrower than a full circle whenever `n
/// != 1`, so a viewport wide enough to surround the apex shows that gap
/// as a visible notch -- a genuine domain limit, worth guarding an
/// interactive viewer against, the way clampCenterLatForViewport
/// (detail/cylindrical/mercator.h) guards Mercator's own pole singularity).
///
/// Uses the same rho-from-latitude inversion unproject() does (matching
/// its signN convention so this also works for a southern-aspect frame,
/// n < 0), solving for the latitude at which `|rho0|` exactly equals a
/// safety-margined fraction of the viewport's own corner distance, then
/// clamping @p latRad toward the equator if it's past that point.
/// Approximate: the exact worst-case geometry also depends on where real
/// coastline data sits relative to the gap, but keeping the apex itself
/// comfortably outside the viewport is sufficient regardless of what
/// happens to be near it.
/// @param latRad Candidate origin latitude (raw sphere-space).
/// @param frame A frame built by makeLambertConformalConicFrame() -- only
/// its own n/rhoScale (fixed by the standard parallels, independent of
/// origin) are used; originLatRad/originLonRad/rho0 are ignored.
/// @param halfWidthKm The same half-width project()'s own scale is
/// calibrated from.
/// @param viewportWidthPx Viewport width in pixels, for its aspect ratio
/// only (any positive unit works the same, since only the width/height
/// ratio matters) -- pass 0 (with @p viewportHeightPx) if unknown yet, to
/// fall back to a square-viewport assumption.
/// @param viewportHeightPx Viewport height in pixels; see @p viewportWidthPx.
/// @return @p latRad, moved toward the equator if it was close enough to
/// this frame's own finite pole to risk exposing the wedge's gap.
// halfWidthKm/viewportWidthPx/viewportHeightPx are documented above and
// always passed in this order -- reordering one alone would be its own
// hazard.
inline float clampOriginLatForApexSafety(
    float latRad, const LambertConformalConicFrame &frame, float halfWidthKm, float viewportWidthPx, float viewportHeightPx) // NOLINT(bugprone-easily-swappable-parameters)
{
    if (halfWidthKm <= 0.0f) {
        return latRad;
    }

    // The viewport's own corners reach further than halfWidthKm alone,
    // by a factor set by its actual aspect ratio -- a square viewport's
    // corners reach sqrt(2) times further, a taller one further still.
    // Falls back to a square assumption if the viewport dimensions
    // aren't known yet.
    const float aspect = (viewportWidthPx > 0.0f && viewportHeightPx > 0.0f) ? (viewportHeightPx / viewportWidthPx) : 1.0f;
    const float cornerReachKm = halfWidthKm * std::sqrt(1.0f + aspect * aspect);

    // 1.0 would be the exact geometric floor -- apex kept just outside
    // the viewport's own true corner, computed above from its actual
    // aspect ratio, with no extra slack. Empirically tuned looser than
    // that instead (interactively, against lccmap's own window): 2.0
    // leaves ordinary interactive use (moderate latitude, moderate zoom)
    // fully uncorrected, while still pulling the apex a real distance
    // off-screen at the extreme end (very high latitude combined with a
    // very wide viewport).
    constexpr float kSafetyMargin = 2.0f;
    const float targetRho0Mag = cornerReachKm / kSafetyMargin;
    const float signN = (frame.n >= 0.0f) ? 1.0f : -1.0f;

    const float lnT = f32math::log(frame.rhoScale / (signN * targetRho0Mag)) / frame.n;
    const float t = f32math::exp(lnT);
    const float limitLatRad = 2.0f * f32math::atan2(t, 1.0f) - kHalfPi;

    if (frame.n >= 0.0f) {
        return (latRad > limitLatRad) ? limitLatRad : latRad;
    }
    return (latRad < limitLatRad) ? limitLatRad : latRad;
}

} // namespace wrenium::geo::conic
