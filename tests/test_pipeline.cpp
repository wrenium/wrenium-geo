// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/azimuthal_pipeline.h"
#include "wrenium/geo/buffer.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"
#include "wrenium/geo/workspace.h"

using namespace wrenium::geo;
using namespace wrenium::geo::azimuthal;

namespace {

// Same forward-geodesic helper as test_clip.cpp (see its comment for why)
// -- not shared across test files to keep each one self-contained.
GeoPoint destinationPoint(const GeoPoint &center, float distanceRad, float bearingRad)
{
    const float sinLat1 = sinf(center.latRad);
    const float cosLat1 = cosf(center.latRad);
    const float sinD = sinf(distanceRad);
    const float cosD = cosf(distanceRad);

    const float sinLat2 = sinLat1 * cosD + cosLat1 * sinD * cosf(bearingRad);
    const float lat2 = asinf(sinLat2 < -1.0f ? -1.0f : (sinLat2 > 1.0f ? 1.0f : sinLat2));

    const float y = sinf(bearingRad) * sinD * cosLat1;
    const float x = cosD - sinLat1 * sinLat2;
    const float lon2 = center.lonRad + atan2f(y, x);

    return GeoPoint{lat2, lon2};
}

// projectRings (azimuthal_pipeline.h) takes each ring's precomputed [minLat, maxLat]
// instead of rescanning every point on every recompute (see its own
// comment) -- these tests build a single ring directly (not via
// loadInputGeometry, which computes this automatically), so this fills in
// the same bound by hand.
template <std::size_t N, std::size_t M>
void pushSingleRingLatBounds(const Buffer<GeoPoint, N> &points, Buffer<float, M> &outMinLat, Buffer<float, M> &outMaxLat) // NOLINT(bugprone-easily-swappable-parameters)
{
    float minLat = points[0].latRad;
    float maxLat = minLat;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (points[i].latRad < minLat) {
            minLat = points[i].latRad;
        }
        if (points[i].latRad > maxLat) {
            maxLat = points[i].latRad;
        }
    }
    outMinLat.pushBack(minLat);
    outMaxLat.pushBack(maxLat);
}

} // namespace

TEST_CASE("pipeline: center enclosed by a ring outside the clip radius synthesizes the full clip circle")
{
    // A square ring surrounding `center` at 20 degrees, but the clip radius
    // is only 5 degrees -- every ring vertex is outside the clip circle, so
    // clipping alone finds nothing. But the ring surrounds the center, so
    // the correct output is the entire clip circle (§2's "fully-enclosed
    // fallback" in azimuthal_pipeline.h), not an empty result.
    // Workspace needs room for well over a hundred points, not just the 4
    // input vertices -- the synthesized circle traces the boundary in ~3
    // degree steps (detail/azimuthal/clip.h's emitFullClipCircle).
    Workspace<256, 8> workspace;
    InputGeometry<16, 4> input;

    const GeoPoint center{0.3f, 0.5f};
    const float ringRadius = 20.0f * kPi / 180.0f;
    const float clipRadiusRad = 5.0f * kPi / 180.0f;

    input.points.pushBack(destinationPoint(center, ringRadius, 0.0f));
    input.points.pushBack(destinationPoint(center, ringRadius, kHalfPi));
    input.points.pushBack(destinationPoint(center, ringRadius, kPi));
    input.points.pushBack(destinationPoint(center, ringRadius, -kHalfPi));
    input.ringSizes.pushBack(input.points.size());

    pushSingleRingLatBounds(input.points, input.ringMinLat, input.ringMaxLat);

    const Error err = projectRings(workspace, input, center, clipRadiusRad, 1.0f, ProjectionType::Equidistant);
    REQUIRE(err == Error::Ok);

    REQUIRE(workspace.projectedRingSizes().size() == 1);
    CHECK(workspace.projectedRingSizes()[0] >= 3);

    // Every synthesized point should sit exactly on the clip circle
    // (radius == clipRadiusRad * kEarthRadiusKm, scale == 1 here).
    const float expectedRadius = clipRadiusRad * kEarthRadiusKm;
    const Point *points = workspace.projectedPoints();
    for (std::size_t i = 0; i < workspace.projectedRingSizes()[0]; ++i) {
        const float r = std::sqrt(points[i].x * points[i].x + points[i].y * points[i].y);
        CHECK(r == doctest::Approx(expectedRadius).epsilon(1e-3));
    }
}

TEST_CASE("pipeline: center outside every ring and clip radius produces no output, not a full circle")
{
    // The ring here is far from `center` (55 degrees away) and small (10
    // degrees) -- center is neither inside it nor anywhere near the clip
    // circle, so the correct output is genuinely nothing. Deliberately not
    // 80 degrees: center sits at 17.19N, and 17.19+80+10(ring radius)
    // wraps past the north pole, producing a ring that (by construction
    // accident, not intent) encloses the pole itself -- out of scope for
    // isCenterEnclosedByRings (no real coastline ring passes through a
    // pole). 55 degrees keeps every ring vertex safely under 90 degrees.
    Workspace<64, 8> workspace;
    InputGeometry<16, 4> input;

    const GeoPoint center{0.3f, 0.5f};
    const GeoPoint farAway = destinationPoint(center, 55.0f * kPi / 180.0f, 0.0f);
    const float ringRadius = 10.0f * kPi / 180.0f;
    const float clipRadiusRad = 5.0f * kPi / 180.0f;

    input.points.pushBack(destinationPoint(farAway, ringRadius, 0.0f));
    input.points.pushBack(destinationPoint(farAway, ringRadius, kHalfPi));
    input.points.pushBack(destinationPoint(farAway, ringRadius, kPi));
    input.points.pushBack(destinationPoint(farAway, ringRadius, -kHalfPi));
    input.ringSizes.pushBack(input.points.size());

    pushSingleRingLatBounds(input.points, input.ringMinLat, input.ringMaxLat);

    const Error err = projectRings(workspace, input, center, clipRadiusRad, 1.0f, ProjectionType::Equidistant);
    REQUIRE(err == Error::Ok);

    CHECK(workspace.projectedRingSizes().size() == 0);
}

// ---- projectPoint: single-point marker/annotation placement API ----
//
// Lets a caller (e.g. the demo app placing a station marker) get the
// exact same (x, y) coordinate the SVG/binary path output uses for a
// point that isn't part of the coastline/border datasets at all.

TEST_CASE("projectPoint: the center itself projects to the origin and is visible")
{
    const GeoPoint center{0.3f, 0.5f};
    const float clipRadiusRad = 20.0f * kPi / 180.0f;

    const ProjectedPoint result = projectPoint(center, center, clipRadiusRad, 1.0f, ProjectionType::Equidistant);

    CHECK(result.visible);
    CHECK(result.point.x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(result.point.y == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("projectPoint: a point within the clip radius matches a direct rotate+project call")
{
    const GeoPoint center{0.3f, 0.5f};
    const float clipRadiusRad = 20.0f * kPi / 180.0f;
    const float scale = 2.0f;
    const GeoPoint markerRaw = destinationPoint(center, 10.0f * kPi / 180.0f, 0.7f);

    const ProjectedPoint result = projectPoint(markerRaw, center, clipRadiusRad, scale, ProjectionType::Equidistant);

    REQUIRE(result.visible);
    const Point expected = projectEquidistant(rotate(markerRaw, center), scale);
    CHECK(result.point.x == doctest::Approx(expected.x).epsilon(1e-4));
    CHECK(result.point.y == doctest::Approx(expected.y).epsilon(1e-4));
}

TEST_CASE("projectPoint: a point outside the clip radius is reported not visible, at the default origin")
{
    const GeoPoint center{0.3f, 0.5f};
    const float clipRadiusRad = 10.0f * kPi / 180.0f;
    const GeoPoint markerRaw = destinationPoint(center, 50.0f * kPi / 180.0f, 0.0f);

    const ProjectedPoint result = projectPoint(markerRaw, center, clipRadiusRad, 1.0f, ProjectionType::Equidistant);

    CHECK_FALSE(result.visible);
    CHECK(result.point.x == 0.0f);
    CHECK(result.point.y == 0.0f);
}

TEST_CASE("projectPoint: a point exactly on the clip boundary counts as visible")
{
    const GeoPoint center{0.3f, 0.5f};
    const float clipRadiusRad = 15.0f * kPi / 180.0f;
    const GeoPoint markerRaw = destinationPoint(center, clipRadiusRad, 1.2f);

    const ProjectedPoint result = projectPoint(markerRaw, center, clipRadiusRad, 1.0f, ProjectionType::Equidistant);

    CHECK(result.visible);
}

TEST_CASE("projectPoint: ProjectionType::Orthographic selects a different radial-distance formula than ProjectionType::Equidistant")
{
    const GeoPoint center{0.3f, 0.5f};
    const float clipRadiusRad = 20.0f * kPi / 180.0f;
    const float scale = 2.0f;
    const GeoPoint markerRaw = destinationPoint(center, 10.0f * kPi / 180.0f, 0.7f);

    const ProjectedPoint equidistantResult = projectPoint(markerRaw, center, clipRadiusRad, scale, ProjectionType::Equidistant);
    const ProjectedPoint orthographicResult = projectPoint(markerRaw, center, clipRadiusRad, scale, ProjectionType::Orthographic);

    REQUIRE(equidistantResult.visible);
    REQUIRE(orthographicResult.visible);
    // Both formulas agree at the center (already covered above) but diverge
    // away from it -- confirms the ProjectionType argument actually took effect
    // rather than silently falling back to equidistant.
    CHECK(orthographicResult.point.x != doctest::Approx(equidistantResult.point.x).epsilon(1e-4));
}

TEST_CASE("projectPoint: ProjectionType::Gnomonic selects a different radial-distance formula than ProjectionType::Equidistant")
{
    const GeoPoint center{0.3f, 0.5f};
    const float clipRadiusRad = 20.0f * kPi / 180.0f;
    const float scale = 2.0f;
    const GeoPoint markerRaw = destinationPoint(center, 10.0f * kPi / 180.0f, 0.7f);

    const ProjectedPoint equidistantResult = projectPoint(markerRaw, center, clipRadiusRad, scale, ProjectionType::Equidistant);
    const ProjectedPoint gnomonicResult = projectPoint(markerRaw, center, clipRadiusRad, scale, ProjectionType::Gnomonic);

    REQUIRE(equidistantResult.visible);
    REQUIRE(gnomonicResult.visible);
    CHECK(gnomonicResult.point.x != doctest::Approx(equidistantResult.point.x).epsilon(1e-4));
}

TEST_CASE("unproject: exact inverse of projectEquidistant")
{
    const GeoPoint centers[] = {
        GeoPoint{0.0f, 0.0f},
        GeoPoint{0.6f, 1.0f},
        GeoPoint{-0.4f, -2.5f},
    };
    const float bearings[] = {0.0f, 0.7f, kHalfPi, kPi - 0.1f, -1.5f};
    const float distancesRad[] = {0.0f, 0.05f, 0.2f, 0.6f};
    const float scales[] = {0.5f, 1.0f, 3.0f};

    for (const GeoPoint &center : centers) {
        for (const float bearing : bearings) {
            for (const float distanceRad : distancesRad) {
                for (const float scale : scales) {
                    CAPTURE(center.latRad);
                    CAPTURE(bearing);
                    CAPTURE(distanceRad);
                    CAPTURE(scale);
                    const GeoPoint raw = destinationPoint(center, distanceRad, bearing);
                    const Point projected = projectEquidistant(rotate(raw, center), scale);
                    const GeoPoint recovered = unproject(projected, center, scale, ProjectionType::Equidistant);

                    // Skip the bearing-is-undefined case (distance 0 --
                    // recovered.lonRad can legitimately be anything).
                    if (distanceRad > 1e-6f) {
                        CHECK(recovered.lonRad == doctest::Approx(raw.lonRad).epsilon(1e-2));
                    }
                    CHECK(recovered.latRad == doctest::Approx(raw.latRad).epsilon(1e-2));
                }
            }
        }
    }
}

TEST_CASE("unproject: exact inverse of projectOrthographic")
{
    const GeoPoint center{0.3f, -0.8f};
    const float scale = 1.5f;
    const GeoPoint raw = destinationPoint(center, 30.0f * kPi / 180.0f, 1.1f);

    const Point projected = projectOrthographic(rotate(raw, center), scale);
    const GeoPoint recovered = unproject(projected, center, scale, ProjectionType::Orthographic);

    CHECK(recovered.latRad == doctest::Approx(raw.latRad).epsilon(1e-2));
    CHECK(recovered.lonRad == doctest::Approx(raw.lonRad).epsilon(1e-2));
}

TEST_CASE("unproject: orthographic saturates towards the horizon for a click past the rendered disk")
{
    // A click outside the rendered orthographic disk (radius
    // kEarthRadiusKm * scale) must not hit asin()'s undefined behavior
    // outside its domain -- unprojectOrthographic clamps sinCentralAngle
    // to 1 first (see its own comment), so this should saturate to
    // exactly the horizon (centralAngle == kHalfPi) instead of NaN.
    const GeoPoint center{0.2f, 0.4f};
    const float scale = 1.0f;
    const float wayPastHorizon = kEarthRadiusKm * scale * 10.0f;

    const GeoPoint recovered = unproject(Point{wayPastHorizon, 0.0f}, center, scale, ProjectionType::Orthographic);

    REQUIRE(std::isfinite(recovered.latRad));
    REQUIRE(std::isfinite(recovered.lonRad));
    // At the horizon, straight east of center: recovered should sit
    // exactly 90 degrees of arc from center.
    const float centralAngle = std::acos(std::sin(center.latRad) * std::sin(recovered.latRad) + std::cos(center.latRad) * std::cos(recovered.latRad) * std::cos(recovered.lonRad - center.lonRad));
    CHECK(centralAngle == doctest::Approx(kHalfPi).epsilon(1e-2));
}

TEST_CASE("unproject: exact inverse of projectGnomonic")
{
    const GeoPoint center{0.3f, -0.8f};
    const float scale = 1.5f;
    const GeoPoint raw = destinationPoint(center, 30.0f * kPi / 180.0f, 1.1f);

    const Point projected = projectGnomonic(rotate(raw, center), scale);
    const GeoPoint recovered = unproject(projected, center, scale, ProjectionType::Gnomonic);

    CHECK(recovered.latRad == doctest::Approx(raw.latRad).epsilon(1e-2));
    CHECK(recovered.lonRad == doctest::Approx(raw.lonRad).epsilon(1e-2));
}

TEST_CASE("rangeRingRadius: zero distance is the origin, regardless of projectionType")
{
    CHECK(rangeRingRadius(0.0f, 2.0f, ProjectionType::Equidistant) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(rangeRingRadius(0.0f, 2.0f, ProjectionType::Orthographic) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(rangeRingRadius(0.0f, 2.0f, ProjectionType::Gnomonic) == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("rangeRingRadius: equidistant scales linearly with distance")
{
    const float scale = 3.0f;
    const float r100 = rangeRingRadius(100.0f, scale, ProjectionType::Equidistant);
    const float r200 = rangeRingRadius(200.0f, scale, ProjectionType::Equidistant);
    const float r400 = rangeRingRadius(400.0f, scale, ProjectionType::Equidistant);

    CHECK(r200 == doctest::Approx(r100 * 2.0f).epsilon(1e-3));
    CHECK(r400 == doctest::Approx(r100 * 4.0f).epsilon(1e-3));
}

TEST_CASE("rangeRingRadius: orthographic does not scale linearly with distance")
{
    // The exact bug this function exists to prevent: assuming a naive
    // linear radius (correct for equidistant) also holds for orthographic.
    // sin() compresses radius non-linearly as distance approaches the
    // horizon, so equal distance steps do *not* produce equal radius steps.
    const float scale = 1.0f;
    const float r25 = rangeRingRadius(0.25f * kEarthRadiusKm * kHalfPi, scale, ProjectionType::Orthographic);
    const float r50 = rangeRingRadius(0.50f * kEarthRadiusKm * kHalfPi, scale, ProjectionType::Orthographic);
    const float r100 = rangeRingRadius(1.00f * kEarthRadiusKm * kHalfPi, scale, ProjectionType::Orthographic);

    // A linear assumption would put r50 at exactly 2*r25 and r100 (the
    // horizon) at exactly 4*r25 -- neither holds under the real sin()-based
    // formula.
    CHECK(r50 != doctest::Approx(r25 * 2.0f).epsilon(1e-3));
    CHECK(r100 != doctest::Approx(r25 * 4.0f).epsilon(1e-3));
    // Still monotonically increasing right up to the horizon, though.
    CHECK(r50 > r25);
    CHECK(r100 > r50);
}

TEST_CASE("rangeRingRadius: gnomonic does not scale linearly with distance")
{
    // Same reasoning as orthographic's identical test above: a naive linear
    // assumption is wrong here too, but in the opposite direction -- tan()
    // grows *faster* than linear as distance approaches the (unreachable in
    // this test) horizon, rather than orthographic's sin()-driven flattening.
    const float scale = 1.0f;
    const float r25 = rangeRingRadius(0.25f * kEarthRadiusKm * kHalfPi, scale, ProjectionType::Gnomonic);
    const float r50 = rangeRingRadius(0.50f * kEarthRadiusKm * kHalfPi, scale, ProjectionType::Gnomonic);
    const float r75 = rangeRingRadius(0.75f * kEarthRadiusKm * kHalfPi, scale, ProjectionType::Gnomonic);

    CHECK(r50 != doctest::Approx(r25 * 2.0f).epsilon(1e-3));
    CHECK(r75 != doctest::Approx(r25 * 3.0f).epsilon(1e-3));
    CHECK(r50 > r25);
    CHECK(r75 > r50);
}

TEST_CASE("rangeRingRadius agrees with projectPoint at the same distance, bearing 0")
{
    // rangeRingRadius() already knows the exact central angle from
    // distanceKm, so it skips rotate()'s own atan2-based central-angle
    // computation entirely -- projectPoint() (via rotate()) still pays
    // that ~6e-4 rad approximation cost, so this needs a wider tolerance
    // than an exact match, the same reasoning test_spherical.cpp's own
    // round-trip tests use for anything chained through rotate().
    constexpr float kRotateApproxEpsilon = 1e-2f;

    const GeoPoint center{0.3f, 0.5f};
    const float clipRadiusRad = kHalfPi;
    const float scale = 2.0f;
    const float distanceKm = 500.0f;

    for (ProjectionType projectionType : {ProjectionType::Equidistant, ProjectionType::Orthographic, ProjectionType::Gnomonic}) {
        CAPTURE(static_cast<int>(projectionType));
        const GeoPoint markerRaw = destinationPoint(center, distanceKm / kEarthRadiusKm, 0.0f);
        const ProjectedPoint marker = projectPoint(markerRaw, center, clipRadiusRad, scale, projectionType);
        REQUIRE(marker.visible);

        const float ringRadius = rangeRingRadius(distanceKm, scale, projectionType);
        CHECK(std::fabs(marker.point.x) < 1e-2f);
        CHECK(-marker.point.y == doctest::Approx(ringRadius).epsilon(kRotateApproxEpsilon));
    }
}
