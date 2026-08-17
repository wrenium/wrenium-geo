// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/detail/angle.h"
#include "wrenium/geo/spherical.h"

#include "fixtures/projection_golden.h"

using namespace wrenium::geo;
using namespace wrenium_geo_tests;

namespace {

// Same atan2 error budget as test_projection.cpp's kToleranceKm -- both
// exercise the same rotate()/rotateBegin() formula this library's trig
// backend approximates.
constexpr float kToleranceKm = 5.0f;
constexpr float kToleranceRad = 1e-3f;

bool approxEqual(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

// +-pi are the same bearing (due south), so a raw subtraction can see a
// ~2*pi difference right at atan2's branch cut even when both values are
// correct; wrap the delta first, same as any other bearing/longitude
// comparison in this library.
bool approxEqualAngle(float a, float b, float tolerance)
{
    return std::fabs(wrenium::geo::detail::wrapPi(a - b)) <= tolerance;
}

} // namespace

TEST_CASE("distanceKm/bearingRad match the projection's own golden values")
{
    std::size_t count = 0;
    const ProjectionGoldenCase *cases = projectionGoldenCases(count);

    for (std::size_t i = 0; i < count; ++i) {
        const ProjectionGoldenCase &c = cases[i];
        CAPTURE(c.name);

        const GeoPoint from{c.centerLatRad, c.centerLonRad};
        const GeoPoint to{c.pointLatRad, c.pointLonRad};

        CHECK(approxEqual(distanceKm(from, to), c.expectedDistanceKm, kToleranceKm));

        // Bearing is only recoverable from the golden (x, y) away from the
        // origin -- at zero distance x = r*sin(bearing) = 0 and
        // y = -r*cos(bearing) = 0 for *every* bearing, so atan2(x, -y)
        // hits an arbitrary IEEE atan2(+-0, +-0) special case instead of
        // reconstructing anything meaningful (same reasoning the fixture's
        // own comment gives for the antipodal case's undefined bearing).
        if (c.checkBearingAndXY && c.expectedDistanceKm > 0.0f) {
            // The golden (x, y) were derived from projectEquidistant()'s own
            // x = r*sin(bearing), y = -r*cos(bearing) convention, so this
            // recovers the same expected bearing without a second,
            // independently-fudgeable golden number.
            const float expectedBearing = std::atan2(c.expectedX, -c.expectedY);
            CHECK(approxEqualAngle(bearingRad(from, to), expectedBearing, kToleranceRad));
        }
    }
}

TEST_CASE("distanceKm is symmetric")
{
    const GeoPoint a = makeGeoPoint(28.6f, 17.2f);
    const GeoPoint b = makeGeoPoint(11.5f, -5.7f);

    CHECK(approxEqual(distanceKm(a, b), distanceKm(b, a), kToleranceKm));
}

TEST_CASE("destinationPoint recovers the projection's own golden values")
{
    std::size_t count = 0;
    const ProjectionGoldenCase *cases = projectionGoldenCases(count);

    for (std::size_t i = 0; i < count; ++i) {
        const ProjectionGoldenCase &c = cases[i];
        CAPTURE(c.name);

        // Same as the bearing check above: only meaningful once a bearing
        // actually exists (away from both the coincident-point and
        // antipodal degenerate cases).
        if (!c.checkBearingAndXY || c.expectedDistanceKm <= 0.0f) {
            continue;
        }

        const GeoPoint from{c.centerLatRad, c.centerLonRad};
        const GeoPoint expectedTo{c.pointLatRad, c.pointLonRad};
        const float bearing = std::atan2(c.expectedX, -c.expectedY);

        const GeoPoint actualTo = destinationPoint(from, c.expectedDistanceKm, bearing);
        CHECK(approxEqual(actualTo.latRad, expectedTo.latRad, kToleranceRad));

        // Longitude is undefined exactly at a pole ("90 deg north/south"):
        // unrotate()'s general-path formula lands on an atan2(~0, ~0),
        // whose result depends on the sign of each near-zero operand --
        // platform/compiler-dependent (observed to differ between
        // AppleClang and GCC/Linux-Clang), not a real formula bug, same
        // class of degeneracy as bearingRad()'s at the coincident-point
        // case above.
        if (!approxEqual(std::fabs(expectedTo.latRad), kHalfPi, kToleranceRad)) {
            CHECK(approxEqual(actualTo.lonRad, expectedTo.lonRad, kToleranceRad));
        }
    }
}

TEST_CASE("destinationPoint at zero distance returns the origin regardless of bearing")
{
    const GeoPoint origin = makeGeoPoint(28.6f, 17.2f);

    CHECK(approxEqual(destinationPoint(origin, 0.0f, 0.0f).latRad, origin.latRad, kToleranceRad));
    CHECK(approxEqual(destinationPoint(origin, 0.0f, 0.0f).lonRad, origin.lonRad, kToleranceRad));
    CHECK(approxEqual(destinationPoint(origin, 0.0f, kPi).latRad, origin.latRad, kToleranceRad));
}

TEST_CASE("destinationPoint/distanceKm/bearingRad round-trip for an arbitrary point")
{
    // Chains three approximate trig calls (destinationPoint(), then
    // distanceKm()/bearingRad() on its result) rather than checking
    // destinationPoint() against an independent reference in one hop (see
    // the golden-fixture test above for that) -- so this needs a wider
    // tolerance than a single-hop comparison to absorb the compounded
    // approximation error, not because anything here is less correct.
    constexpr float kRoundTripToleranceKm = 15.0f;
    constexpr float kRoundTripToleranceRad = 3e-3f;

    const GeoPoint origin = makeGeoPoint(28.6f, 17.2f);
    constexpr float travelKm = 3051.907588f;
    constexpr float travelBearing = -2.16601f;

    const GeoPoint reached = destinationPoint(origin, travelKm, travelBearing);

    CHECK(approxEqual(distanceKm(origin, reached), travelKm, kRoundTripToleranceKm));
    CHECK(approxEqual(bearingRad(origin, reached), travelBearing, kRoundTripToleranceRad));
}

TEST_CASE("interpolate at t=0/t=1 returns the two endpoints")
{
    // t=0 costs a single destinationPoint() call at zero distance (same
    // exact case "destinationPoint at zero distance" above already
    // checks at kToleranceRad) -- but t=1 chains distanceKm()+bearingRad()
    // into that same destinationPoint() call, the same three-hop
    // composition "destinationPoint/distanceKm/bearingRad round-trip"
    // above needs its own wider tolerance for, and for the same reason.
    constexpr float kRoundTripToleranceRad = 3e-3f;

    const GeoPoint a = makeGeoPoint(28.6f, 17.2f);
    const GeoPoint b = makeGeoPoint(11.5f, -5.7f);

    CHECK(approxEqual(interpolate(a, b, 0.0f).latRad, a.latRad, kToleranceRad));
    CHECK(approxEqual(interpolate(a, b, 0.0f).lonRad, a.lonRad, kToleranceRad));
    CHECK(approxEqual(interpolate(a, b, 1.0f).latRad, b.latRad, kRoundTripToleranceRad));
    CHECK(approxEqual(interpolate(a, b, 1.0f).lonRad, b.lonRad, kRoundTripToleranceRad));
}

TEST_CASE("interpolate at t=0.5 sits at the great-circle midpoint")
{
    // The midpoint is equidistant from both endpoints, and its two
    // half-distances sum back to the full a-to-b distance -- an
    // independent property check, not just "close to some expected
    // coordinate", so it can't accidentally pass from a formula bug that
    // happens to also be self-consistent. Same wider round-trip tolerance
    // as the t=1 case above, for the same three-hop-composition reason.
    constexpr float kRoundTripToleranceKm = 15.0f;

    const GeoPoint a = makeGeoPoint(28.6f, 17.2f);
    const GeoPoint b = makeGeoPoint(-40.0f, 120.0f);
    const GeoPoint mid = interpolate(a, b, 0.5f);

    const float distAToMid = distanceKm(a, mid);
    const float distMidToB = distanceKm(mid, b);
    const float distAToB = distanceKm(a, b);

    CHECK(approxEqual(distAToMid, distMidToB, kToleranceKm));
    CHECK(approxEqual(distAToMid + distMidToB, distAToB, kRoundTripToleranceKm));
}

TEST_CASE("interpolate extrapolates past b for t>1")
{
    // t=2 should sit exactly one more a-to-b distance past b, along the
    // same great circle (same bearing from b onward as a-to-b's own).
    const GeoPoint a = makeGeoPoint(10.0f, 0.0f);
    const GeoPoint b = makeGeoPoint(30.0f, 10.0f);
    const GeoPoint extrapolated = interpolate(a, b, 2.0f);

    const float distAToB = distanceKm(a, b);
    CHECK(approxEqual(distanceKm(b, extrapolated), distAToB, kToleranceKm));
    CHECK(approxEqual(distanceKm(a, extrapolated), distAToB * 2.0f, kToleranceKm));
}

TEST_CASE("area of fewer than 3 points is zero")
{
    const GeoPoint points[2] = {makeGeoPoint(10.0f, 10.0f), makeGeoPoint(20.0f, 20.0f)};
    CHECK(area(points, 0) == 0.0f);
    CHECK(area(points, 1) == 0.0f);
    CHECK(area(points, 2) == 0.0f);
}

TEST_CASE("area of a small square matches the planar approximation")
{
    // At small scale (~1 degree here), spherical curvature is negligible,
    // so the enclosed area should closely match a flat-earth rectangle:
    // width * height, both converted from degrees to kilometers.
    const GeoPoint square[4] = {
        makeGeoPoint(0.0f, 0.0f),
        makeGeoPoint(0.0f, 1.0f),
        makeGeoPoint(1.0f, 1.0f),
        makeGeoPoint(1.0f, 0.0f),
    };
    const float degToKm = kPi / 180.0f * kEarthRadiusKm;
    const float planarApprox = degToKm * degToKm;

    CHECK(approxEqual(area(square, 4), planarApprox, planarApprox * 0.01f));
}

TEST_CASE("area of a small triangle matches an independent spherical-excess reference")
{
    // Expected value computed independently in Python via 3D unit vectors
    // and the spherical law of cosines (interior angle at each vertex =
    // angle between the two tangent-plane directions toward its
    // neighbors; area = R^2 * (sum of interior angles - pi)) -- a
    // completely different computational path from this function's own
    // line-integral formula, not just the same formula re-derived.
    const GeoPoint triangle[3] = {
        makeGeoPoint(10.0f, 10.0f),
        makeGeoPoint(13.0f, 11.0f),
        makeGeoPoint(11.0f, 14.0f),
    };
    constexpr float kExpectedAreaKm2 = 66698.99f;
    // Small (~3 degree) edges keep this function's own edge-length-
    // dependent discretization error negligible (see its own doc comment)
    // -- 1% comfortably covers that plus ordinary trig approximation.
    CHECK(approxEqual(area(triangle, 3), kExpectedAreaKm2, kExpectedAreaKm2 * 0.01f));
}

TEST_CASE("area is unchanged by shifting the same shape across the antimeridian")
{
    // Same square, once away from the antimeridian and once straddling
    // it -- area is a property of the shape alone, so both must agree.
    // Exercises the wrapPi()'d longitude accumulation directly: without
    // it, the antimeridian-straddling ring's raw longitude differences
    // would see a spurious ~360 degree jump at the crossing edge.
    const GeoPoint atZero[4] = {
        makeGeoPoint(-5.0f, -5.0f),
        makeGeoPoint(-5.0f, 5.0f),
        makeGeoPoint(5.0f, 5.0f),
        makeGeoPoint(5.0f, -5.0f),
    };
    const GeoPoint atAntimeridian[4] = {
        makeGeoPoint(-5.0f, 175.0f),
        makeGeoPoint(-5.0f, -175.0f),
        makeGeoPoint(5.0f, -175.0f),
        makeGeoPoint(5.0f, 175.0f),
    };

    const float areaAtZero = area(atZero, 4);
    const float areaAtAntimeridian = area(atAntimeridian, 4);
    CHECK(approxEqual(areaAtZero, areaAtAntimeridian, areaAtZero * 0.01f));
}

TEST_CASE("centroid of fewer than 3 points is {0, 0}")
{
    const GeoPoint points[2] = {makeGeoPoint(10.0f, 10.0f), makeGeoPoint(20.0f, 20.0f)};
    for (std::size_t n = 0; n <= 2; ++n) {
        const GeoPoint c = centroid(points, n);
        CHECK(c.latRad == 0.0f);
        CHECK(c.lonRad == 0.0f);
    }
}

TEST_CASE("centroid of a small square matches its geometric center")
{
    // At small scale (~1 degree here), spherical curvature is negligible,
    // so the centroid should closely match the square's own arithmetic
    // mean corner (0.5, 0.5) -- the same "planar approximation at small
    // scale" cross-check area()'s own equivalent test uses. Same widened,
    // chained-trig budget as interpolate()'s own round-trip test above:
    // this function chains many more approximate sin/cos/atan2 calls than
    // a single rotate(), and that chain's own accumulated error dominates
    // over the formula's tiny (sub-0.01-degree, at this scale) discretization
    // error.
    constexpr float kRoundTripToleranceRad = 3e-3f;
    const GeoPoint square[4] = {
        makeGeoPoint(0.0f, 0.0f),
        makeGeoPoint(0.0f, 1.0f),
        makeGeoPoint(1.0f, 1.0f),
        makeGeoPoint(1.0f, 0.0f),
    };
    const GeoPoint c = centroid(square, 4);
    CHECK(approxEqual(c.latRad, 0.5f * kPi / 180.0f, kRoundTripToleranceRad));
    CHECK(approxEqual(c.lonRad, 0.5f * kPi / 180.0f, kRoundTripToleranceRad));
}

TEST_CASE("centroid of a small triangle matches an independent numerically-integrated reference")
{
    // Same triangle as area()'s own independent-reference test, but this
    // reference comes from a completely different computation: a fine
    // lat/lon grid, each cell weighted by its own true differential
    // surface area (cos(lat) * dlat * dlon) and summed as a 3D vector,
    // not this function's own triangle-fan/spherical-excess formula
    // re-derived. Same widened, chained-trig tolerance as the square test
    // above.
    constexpr float kRoundTripToleranceRad = 3e-3f;
    const GeoPoint triangle[3] = {
        makeGeoPoint(10.0f, 10.0f),
        makeGeoPoint(13.0f, 11.0f),
        makeGeoPoint(11.0f, 14.0f),
    };
    constexpr float kExpectedLatRad = 11.333177f * kPi / 180.0f;
    constexpr float kExpectedLonRad = 11.666557f * kPi / 180.0f;

    const GeoPoint c = centroid(triangle, 3);
    CHECK(approxEqual(c.latRad, kExpectedLatRad, kRoundTripToleranceRad));
    CHECK(approxEqual(c.lonRad, kExpectedLonRad, kRoundTripToleranceRad));
}

TEST_CASE("centroid is unaffected by the ring's own winding order")
{
    // The triangle-fan sum's own total spherical excess flips sign with
    // winding order, same as area()'s own line-integral sum does -- unlike
    // area(), which only ever uses that total's magnitude, this function
    // needs the sum's own direction, so reversing winding without also
    // correcting for that sign flip would land on the ring's own
    // antipodal point instead of its centroid.
    constexpr float kToleranceRad3 = 3e-3f;
    const GeoPoint forward[3] = {
        makeGeoPoint(10.0f, 10.0f),
        makeGeoPoint(13.0f, 11.0f),
        makeGeoPoint(11.0f, 14.0f),
    };
    const GeoPoint reversed[3] = {
        makeGeoPoint(11.0f, 14.0f),
        makeGeoPoint(13.0f, 11.0f),
        makeGeoPoint(10.0f, 10.0f),
    };

    const GeoPoint forwardCentroid = centroid(forward, 3);
    const GeoPoint reversedCentroid = centroid(reversed, 3);
    CHECK(approxEqual(forwardCentroid.latRad, reversedCentroid.latRad, kToleranceRad3));
    CHECK(approxEqual(forwardCentroid.lonRad, reversedCentroid.lonRad, kToleranceRad3));
}

TEST_CASE("centroid is unchanged by shifting the same shape across the antimeridian")
{
    // Same square, once away from the antimeridian and once straddling
    // it -- centroid is a property of the shape alone, so both must agree
    // once the antimeridian-straddling one's own longitude is read back
    // relative to the same origin. Exercises the pure-3D-vector approach
    // directly: unlike area(), this function never takes a raw longitude
    // difference anywhere, so it needs no wrapPi() to get this right. Same
    // widened, chained-trig tolerance as the two tests above (in degrees
    // here, to compare directly against wrapLongitudeDeg()'s own output).
    constexpr float kToleranceDeg = 3e-3f * 180.0f / kPi;
    const GeoPoint atZero[4] = {
        makeGeoPoint(-5.0f, -5.0f),
        makeGeoPoint(-5.0f, 5.0f),
        makeGeoPoint(5.0f, 5.0f),
        makeGeoPoint(5.0f, -5.0f),
    };
    const GeoPoint atAntimeridian[4] = {
        makeGeoPoint(-5.0f, 175.0f),
        makeGeoPoint(-5.0f, -175.0f),
        makeGeoPoint(5.0f, -175.0f),
        makeGeoPoint(5.0f, 175.0f),
    };

    const GeoPoint centroidAtZero = centroid(atZero, 4);
    const GeoPoint centroidAtAntimeridian = centroid(atAntimeridian, 4);
    const float shiftedLonDeg = wrapLongitudeDeg(centroidAtAntimeridian.lonRad * 180.0f / kPi - 180.0f);
    CHECK(approxEqual(centroidAtZero.latRad * 180.0f / kPi, centroidAtAntimeridian.latRad * 180.0f / kPi, kToleranceDeg));
    CHECK(approxEqual(centroidAtZero.lonRad * 180.0f / kPi, shiftedLonDeg, kToleranceDeg));
}

TEST_CASE("wrapLongitudeDeg wraps to (-180, 180]")
{
    CHECK(approxEqual(wrapLongitudeDeg(0.0f), 0.0f, kToleranceRad));
    CHECK(approxEqual(wrapLongitudeDeg(180.0f), 180.0f, kToleranceRad));
    CHECK(approxEqual(wrapLongitudeDeg(-180.0f), 180.0f, kToleranceRad));
    CHECK(approxEqual(wrapLongitudeDeg(181.0f), -179.0f, kToleranceRad));
    CHECK(approxEqual(wrapLongitudeDeg(-181.0f), 179.0f, kToleranceRad));
    CHECK(approxEqual(wrapLongitudeDeg(540.0f), 180.0f, kToleranceRad));
    CHECK(approxEqual(wrapLongitudeDeg(-540.0f), 180.0f, kToleranceRad));
}

TEST_CASE("clampLatitudeDeg keeps latitude within its margin of each pole")
{
    CHECK(approxEqual(clampLatitudeDeg(0.0f, 5.0f), 0.0f, kToleranceRad));
    CHECK(approxEqual(clampLatitudeDeg(80.0f, 5.0f), 80.0f, kToleranceRad));
    CHECK(approxEqual(clampLatitudeDeg(89.9f, 5.0f), 85.0f, kToleranceRad));
    CHECK(approxEqual(clampLatitudeDeg(-89.9f, 5.0f), -85.0f, kToleranceRad));
    CHECK(approxEqual(clampLatitudeDeg(90.0f, 0.0f), 90.0f, kToleranceRad));
}

TEST_CASE("shortestAngleDeltaDeg finds the shorter way around")
{
    CHECK(approxEqual(shortestAngleDeltaDeg(0.0f, 90.0f), 90.0f, kToleranceRad));
    CHECK(approxEqual(shortestAngleDeltaDeg(0.0f, -90.0f), -90.0f, kToleranceRad));
    CHECK(approxEqual(shortestAngleDeltaDeg(350.0f, 10.0f), 20.0f, kToleranceRad));
    CHECK(approxEqual(shortestAngleDeltaDeg(10.0f, 350.0f), -20.0f, kToleranceRad));
    CHECK(approxEqual(shortestAngleDeltaDeg(0.0f, 180.0f), 180.0f, kToleranceRad));
    CHECK(approxEqual(shortestAngleDeltaDeg(45.0f, 45.0f), 0.0f, kToleranceRad));
}

TEST_CASE("length of fewer than 2 points is zero")
{
    const GeoPoint one[1] = {makeGeoPoint(28.6f, 17.2f)};
    CHECK(length(one, 0) == 0.0f);
    CHECK(length(one, 1) == 0.0f);
}

TEST_CASE("length of two points matches distanceKm directly")
{
    const GeoPoint points[2] = {makeGeoPoint(28.6f, 17.2f), makeGeoPoint(11.5f, -5.7f)};
    CHECK(approxEqual(length(points, 2), distanceKm(points[0], points[1]), kToleranceKm));
}

TEST_CASE("length of an open polyline sums each consecutive hop")
{
    const GeoPoint points[3] = {
        makeGeoPoint(28.6f, 17.2f),
        makeGeoPoint(11.5f, -5.7f),
        makeGeoPoint(-20.0f, 40.0f),
    };
    const float expected = distanceKm(points[0], points[1]) + distanceKm(points[1], points[2]);
    CHECK(approxEqual(length(points, 3), expected, kToleranceKm));
    // Doesn't include a closing edge back to points[0] unless asked.
    CHECK_FALSE(approxEqual(length(points, 3), expected + distanceKm(points[2], points[0]), kToleranceKm));
}

TEST_CASE("length with closed=true adds the closing edge back to the first point")
{
    const GeoPoint points[4] = {
        makeGeoPoint(10.0f, 10.0f),
        makeGeoPoint(10.0f, 20.0f),
        makeGeoPoint(20.0f, 20.0f),
        makeGeoPoint(20.0f, 10.0f),
    };
    const float openLength = length(points, 4, false);
    const float closedLength = length(points, 4, true);
    const float closingEdge = distanceKm(points[3], points[0]);

    CHECK(approxEqual(closedLength, openLength + closingEdge, kToleranceKm));
}
