// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"
#include "wrenium/geo/pipeline.h"
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

// projectRings (pipeline.h) takes each ring's precomputed [minLat, maxLat]
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
    // fallback" in pipeline.h), not an empty result.
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

    const Error err = projectRings(workspace, input, center, clipRadiusRad, 1.0f);
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

    const Error err = projectRings(workspace, input, center, clipRadiusRad, 1.0f);
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

    const ProjectedPoint result = projectPoint(center, center, clipRadiusRad, 1.0f);

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

    const ProjectedPoint result = projectPoint(markerRaw, center, clipRadiusRad, scale);

    REQUIRE(result.visible);
    const Point expected = project(rotate(markerRaw, center), scale);
    CHECK(result.point.x == doctest::Approx(expected.x).epsilon(1e-4));
    CHECK(result.point.y == doctest::Approx(expected.y).epsilon(1e-4));
}

TEST_CASE("projectPoint: a point outside the clip radius is reported not visible, at the default origin")
{
    const GeoPoint center{0.3f, 0.5f};
    const float clipRadiusRad = 10.0f * kPi / 180.0f;
    const GeoPoint markerRaw = destinationPoint(center, 50.0f * kPi / 180.0f, 0.0f);

    const ProjectedPoint result = projectPoint(markerRaw, center, clipRadiusRad, 1.0f);

    CHECK_FALSE(result.visible);
    CHECK(result.point.x == 0.0f);
    CHECK(result.point.y == 0.0f);
}

TEST_CASE("projectPoint: a point exactly on the clip boundary counts as visible")
{
    const GeoPoint center{0.3f, 0.5f};
    const float clipRadiusRad = 15.0f * kPi / 180.0f;
    const GeoPoint markerRaw = destinationPoint(center, clipRadiusRad, 1.2f);

    const ProjectedPoint result = projectPoint(markerRaw, center, clipRadiusRad, 1.0f);

    CHECK(result.visible);
}

TEST_CASE("projectPoint: an explicit ProjectFn selects a different radial-distance formula than the default")
{
    const GeoPoint center{0.3f, 0.5f};
    const float clipRadiusRad = 20.0f * kPi / 180.0f;
    const float scale = 2.0f;
    const GeoPoint markerRaw = destinationPoint(center, 10.0f * kPi / 180.0f, 0.7f);

    const ProjectedPoint equidistantResult = projectPoint(markerRaw, center, clipRadiusRad, scale);
    const ProjectedPoint orthographicResult = projectPoint<azimuthal::projectOrthographic>(markerRaw, center, clipRadiusRad, scale);

    REQUIRE(equidistantResult.visible);
    REQUIRE(orthographicResult.visible);
    // Both formulas agree at the center (already covered above) but diverge
    // away from it -- confirms the template parameter actually took effect
    // rather than silently falling back to the default.
    CHECK(orthographicResult.point.x != doctest::Approx(equidistantResult.point.x).epsilon(1e-4));
}
