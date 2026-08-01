// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

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
            CHECK(approxEqual(bearingRad(from, to), expectedBearing, kToleranceRad));
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
