// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/contains.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"
#include "wrenium/geo/projection.h"

using namespace wrenium::geo;

namespace {

// Standard forward-geodesic ("destination point given start, bearing,
// distance") formula -- an independent reference oracle, not
// destinationPoint() (spherical.h) itself, so a bug shared with the real
// implementation can't hide a test failure.
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

// An arbitrary, non-degenerate center -- not the pole and not the
// equator/prime-meridian special cases.
constexpr float kTestCenterLat = 0.3f;
constexpr float kTestCenterLon = 0.5f;
GeoPoint testCenter()
{
    return GeoPoint{kTestCenterLat, kTestCenterLon};
}

// Builds a single-ring InputGeometry from a flat list of points.
template <std::size_t MaxPoints>
InputGeometry<MaxPoints, 1> makeSingleRing(const GeoPoint *points, std::size_t count)
{
    InputGeometry<MaxPoints, 1> geometry;
    for (std::size_t i = 0; i < count; ++i) {
        geometry.points.pushBack(points[i]);
    }
    geometry.ringSizes.pushBack(count);
    return geometry;
}

} // namespace

TEST_CASE("contains reports a point inside a surrounding ring")
{
    // A square ring surrounding `center` at 10 degrees -- center itself is
    // nowhere near the ring's own vertices/edges, so this exercises the
    // "well inside" case, not a boundary-adjacent edge case.
    const GeoPoint center = testCenter();
    const float radiusRad = 10.0f * kPi / 180.0f;
    const GeoPoint square[4] = {
        destinationPoint(center, radiusRad, 0.0f),
        destinationPoint(center, radiusRad, kHalfPi),
        destinationPoint(center, radiusRad, kPi),
        destinationPoint(center, radiusRad, -kHalfPi),
    };
    const auto geometry = makeSingleRing<8>(square, 4);

    CHECK(contains(geometry, center));
}

TEST_CASE("contains reports a point outside a distant ring")
{
    // A square ring far from `center` (55 degrees away) -- center is
    // nowhere near it at all. Deliberately not 80 degrees: testCenter()
    // sits at 17.19N, and 17.19+80+10(ring radius) wraps past the north
    // pole, producing a ring that (by construction accident, not intent)
    // encloses the pole itself -- out of scope, no real coastline ring
    // ever passes through a pole. 55 degrees keeps every ring vertex
    // safely under 90 degrees latitude.
    const GeoPoint center = testCenter();
    const GeoPoint farAway = destinationPoint(center, 55.0f * kPi / 180.0f, 0.0f);
    const float radiusRad = 10.0f * kPi / 180.0f;
    const GeoPoint square[4] = {
        destinationPoint(farAway, radiusRad, 0.0f),
        destinationPoint(farAway, radiusRad, kHalfPi),
        destinationPoint(farAway, radiusRad, kPi),
        destinationPoint(farAway, radiusRad, -kHalfPi),
    };
    const auto geometry = makeSingleRing<8>(square, 4);

    CHECK_FALSE(contains(geometry, center));
}

// A ring spanning more than half the globe's longitude needs the same
// per-edge meridian-crossing test as any other ring -- a shortest-
// angular-distance shortcut gets "which arc is the interior" backwards
// for a ring this wide. This rectangle exercises exactly that property,
// with every edge kept safely short; its expected answer is independently
// verified against a textbook from-scratch ray-cast implementation.
TEST_CASE("contains correctly classifies a ring spanning most of the globe's longitude")
{
    const float degToRad = kPi / 180.0f;

    GeoPoint ring[10];
    std::size_t n = 0;
    // Top edge, west to east: lon -100 -> 100, five points, ~50 deg/edge.
    for (float lonDeg : {-100.0f, -50.0f, 0.0f, 50.0f, 100.0f}) {
        ring[n++] = GeoPoint{10.0f * degToRad, lonDeg * degToRad};
    }
    // Bottom edge, east to west: lon 100 -> -100, five points.
    for (float lonDeg : {100.0f, 50.0f, 0.0f, -50.0f, -100.0f}) {
        ring[n++] = GeoPoint{-10.0f * degToRad, lonDeg * degToRad};
    }
    // Ring closes implicitly back to its own first point (matching every
    // other ring in this codebase's convention -- no duplicated closing
    // vertex).
    const auto geometry = makeSingleRing<16>(ring, n);

    // Inside: comfortably within the -100..100 longitude band.
    CHECK(contains(geometry, GeoPoint{0.0f, 0.0f}));
    CHECK(contains(geometry, GeoPoint{5.0f * degToRad, 80.0f * degToRad}));
    CHECK(contains(geometry, GeoPoint{-5.0f * degToRad, -80.0f * degToRad}));

    // Outside: in the ~160-degree exterior gap on the *other* side of the
    // globe (100 to -100 the short way, through +-180) -- exactly the
    // configuration ("is a point in the minority arc, or the majority
    // arc") a shortest-angular-distance heuristic gets backwards for a
    // ring spanning more than half the globe.
    CHECK_FALSE(contains(geometry, GeoPoint{0.0f, 170.0f * degToRad}));
    CHECK_FALSE(contains(geometry, GeoPoint{0.0f, -150.0f * degToRad}));
    CHECK_FALSE(contains(geometry, GeoPoint{0.0f, 180.0f * degToRad}));

    // Well outside in latitude too (above/below the ring's own band), at a
    // longitude comfortably inside its span -- should never be inside
    // regardless of longitude.
    CHECK_FALSE(contains(geometry, GeoPoint{50.0f * degToRad, 0.0f}));
}

// A ring whose first point sits at the antimeridian's edge and whose
// path drifts past it: a query's meridian offset from the ring's first
// point can land outside the ring's own drift range even though the
// query is genuinely inside the covered area, which is why the per-edge
// test (not a whole-ring embedding) is needed.
TEST_CASE("contains correctly classifies a ring whose first point sits at the antimeridian and drifts past it")
{
    const float degToRad = kPi / 180.0f;

    GeoPoint ring[14];
    std::size_t n = 0;
    // Top edge: 0 -> -35 -> -70 -> -105 -> -140 -> -175 -> 150, each edge a
    // short (35 degree) local step; the last edge crosses the antimeridian,
    // continuing the same consistent direction rather than re-normalizing.
    for (float lonDeg : {0.0f, -35.0f, -70.0f, -105.0f, -140.0f, -175.0f, 150.0f}) {
        ring[n++] = GeoPoint{10.0f * degToRad, lonDeg * degToRad};
    }
    // Bottom edge: the same longitudes, retraced in reverse, closing the
    // ring back to its own first point (0, 10 degrees).
    for (float lonDeg : {150.0f, -175.0f, -140.0f, -105.0f, -70.0f, -35.0f, 0.0f}) {
        ring[n++] = GeoPoint{-10.0f * degToRad, lonDeg * degToRad};
    }
    const auto geometry = makeSingleRing<16>(ring, n);

    // Inside: comfortably within the covered arc (0 back through -175 to
    // 150, i.e. everything except the ~150-degree gap from 0 to 150 going
    // the other, direct way).
    CHECK(contains(geometry, GeoPoint{0.0f, -90.0f * degToRad}));
    // Inside, *far side* of the ring's own first point -- exactly the
    // configuration that broke: the query's wrapPi'd offset from the
    // ring's first point (150 degrees) is undefined relative to that
    // point's own cumulative range without crossing the seam again.
    CHECK(contains(geometry, GeoPoint{0.0f, 155.0f * degToRad}));
    CHECK(contains(geometry, GeoPoint{0.0f, -178.0f * degToRad}));

    // Outside: the ~150-degree gap between 0 and 150 the direct way.
    CHECK_FALSE(contains(geometry, GeoPoint{0.0f, 75.0f * degToRad}));
    CHECK_FALSE(contains(geometry, GeoPoint{0.0f, 10.0f * degToRad}));
    CHECK_FALSE(contains(geometry, GeoPoint{0.0f, 140.0f * degToRad}));
}

// A ring vertex sitting exactly on the query's own meridian needs the
// half-open crossing interval to be attributed to exactly one of its two
// adjacent edges -- never both (silently cancels out) or neither (missed
// crossing, wrong parity).
TEST_CASE("contains handles a query meridian passing exactly through a ring vertex")
{
    const float degToRad = kPi / 180.0f;

    const GeoPoint ring[6] = {
        GeoPoint{10.0f * degToRad, -50.0f * degToRad},
        GeoPoint{10.0f * degToRad, 0.0f * degToRad}, // vertex exactly on the tested meridian
        GeoPoint{10.0f * degToRad, 50.0f * degToRad},
        GeoPoint{-10.0f * degToRad, 50.0f * degToRad},
        GeoPoint{-10.0f * degToRad, 0.0f * degToRad}, // vertex exactly on the tested meridian
        GeoPoint{-10.0f * degToRad, -50.0f * degToRad},
    };
    const auto geometry = makeSingleRing<8>(ring, 6);

    CHECK(contains(geometry, GeoPoint{0.0f, 0.0f}));
}

// A ring vertex and the query can sit on the exact same meridian while
// being *different* float values (e.g. a vertex at exactly -180 degrees,
// a query at exactly +180 degrees) -- the crossing test must still
// attribute the vertex to exactly one adjacent edge in that case, not
// let both (or neither) claim it.
TEST_CASE("contains handles a ring vertex and query on the same meridian via different float representations")
{
    const float degToRad = kPi / 180.0f;

    // Same symmetric-hexagon shape as the previous test (vertex on both
    // top and bottom edges, so the tested meridian is genuinely interior
    // to the ring), shifted so the shared vertex sits at exactly -180
    // degrees.
    const GeoPoint ring[6] = {
        GeoPoint{10.0f * degToRad, 130.0f * degToRad},
        GeoPoint{10.0f * degToRad, -kPi}, // exactly -180 degrees
        GeoPoint{10.0f * degToRad, -130.0f * degToRad},
        GeoPoint{-10.0f * degToRad, -130.0f * degToRad},
        GeoPoint{-10.0f * degToRad, -kPi}, // exactly -180 degrees
        GeoPoint{-10.0f * degToRad, 130.0f * degToRad},
    };
    const auto geometry = makeSingleRing<6>(ring, 6);

    // Query at exactly +180 degrees -- the same meridian as the ring's own
    // -180-degree vertices, via the opposite float representation. Must
    // agree with immediately neighboring queries, not flip only at this
    // exact value.
    CHECK(contains(geometry, GeoPoint{0.0f, kPi}));
    CHECK(contains(geometry, GeoPoint{0.0f, kPi - 0.001f}));
    CHECK(contains(geometry, GeoPoint{0.0f, -kPi + 0.001f}));
}
