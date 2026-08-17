// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/detail/azimuthal/gnomonic.h"
#include "wrenium/geo/detail/azimuthal/rotation.h"
#include "wrenium/geo/projection.h"

using namespace wrenium::geo;
using namespace wrenium::geo::azimuthal;

namespace {

// Same trig-approximation error budget as test_projection.cpp's
// kToleranceKm -- see that file's comment for the derivation.
constexpr float kToleranceKm = 5.0f;

bool approxEqual(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

} // namespace

TEST_CASE("gnomonic: center == point projects to the origin regardless of scale")
{
    GeoPoint center{0.9f, -1.4f};
    const GeoPoint rotated = rotate(center, center);
    const Point projected = projectGnomonic(rotated, 42.0f);
    CHECK(approxEqual(projected.x, 0.0f, 0.001f));
    CHECK(approxEqual(projected.y, 0.0f, 0.001f));
}

TEST_CASE("gnomonic: projected radius is kEarthRadiusKm * tan(centralAngle) * scale")
{
    GeoPoint center{0.1f, 0.2f};
    GeoPoint point{0.35f, -0.6f};
    const GeoPoint rotated = rotate(point, center);
    const float centralAngle = kHalfPi - rotated.latRad;
    const float expectedRadius = kEarthRadiusKm * std::tan(centralAngle);

    const float scales[] = {0.01f, 1.0f, 4.5f, 100.0f};
    for (float scale : scales) {
        const Point projected = projectGnomonic(rotated, scale);
        const float radius = std::sqrt(projected.x * projected.x + projected.y * projected.y);
        CHECK(approxEqual(radius, expectedRadius * scale, kToleranceKm * (scale > 1.0f ? scale : 1.0f)));
    }
}

TEST_CASE("gnomonic: a point 45 degrees from center projects to radius kEarthRadiusKm * scale")
{
    // tan(45 deg) == 1, so this is the one angle with a radius that doesn't
    // need std::tan() to state as a clean closed form -- unlike
    // orthographic's own horizon test, gnomonic has no finite radius at
    // the horizon itself (tan diverges), so this is the furthest point
    // this test can check without approaching that divergence.
    GeoPoint rotated{kHalfPi - kPi / 4.0f, kHalfPi};
    const Point projected = projectGnomonic(rotated, 1.0f);
    const float radius = std::sqrt(projected.x * projected.x + projected.y * projected.y);
    CHECK(approxEqual(radius, kEarthRadiusKm, kToleranceKm));
}

TEST_CASE("gnomonic and equidistant agree near the center (both linearize for small angles)")
{
    // For small centralAngle, tan(centralAngle) =~ centralAngle, so the two
    // radial formulas should nearly coincide -- unlike further out, where
    // gnomonic's tan() grows faster than equidistant's linear centralAngle
    // and diverges to infinity at the horizon.
    GeoPoint center{0.4f, 1.1f};
    GeoPoint nearbyPoint{0.405f, 1.105f}; // a few hundred km away
    const GeoPoint rotated = rotate(nearbyPoint, center);

    const float centralAngle = kHalfPi - rotated.latRad;
    const float equidistantRadius = centralAngle * kEarthRadiusKm;

    const Point gnomonicProjected = projectGnomonic(rotated, 1.0f);
    const float gnomonicRadius = std::sqrt(gnomonicProjected.x * gnomonicProjected.x + gnomonicProjected.y * gnomonicProjected.y);

    CHECK(approxEqual(gnomonicRadius, equidistantRadius, 1.0f));
}
