// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/detail/azimuthal/orthographic.h"
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

TEST_CASE("orthographic: center == point projects to the origin regardless of scale")
{
    GeoPoint center{0.9f, -1.4f};
    const GeoPoint rotated = rotate(center, center);
    const Point projected = projectOrthographic(rotated, 42.0f);
    CHECK(approxEqual(projected.x, 0.0f, 0.001f));
    CHECK(approxEqual(projected.y, 0.0f, 0.001f));
}

TEST_CASE("orthographic: projected radius is kEarthRadiusKm * sin(centralAngle) * scale")
{
    GeoPoint center{0.1f, 0.2f};
    GeoPoint point{0.35f, -0.6f};
    const GeoPoint rotated = rotate(point, center);
    const float centralAngle = kHalfPi - rotated.latRad;
    const float expectedRadius = kEarthRadiusKm * std::sin(centralAngle);

    const float scales[] = {0.01f, 1.0f, 4.5f, 100.0f};
    for (float scale : scales) {
        const Point projected = projectOrthographic(rotated, scale);
        const float radius = std::sqrt(projected.x * projected.x + projected.y * projected.y);
        CHECK(approxEqual(radius, expectedRadius * scale, kToleranceKm * (scale > 1.0f ? scale : 1.0f)));
    }
}

TEST_CASE("orthographic: a point at the horizon (centralAngle == kHalfPi) projects to radius kEarthRadiusKm * scale")
{
    // Rotated point 90 degrees away from center, due east -- rotatedLat ==
    // 0 means centralAngle == kHalfPi - 0 == kHalfPi exactly.
    GeoPoint rotated{0.0f, kHalfPi};
    const Point projected = projectOrthographic(rotated, 1.0f);
    const float radius = std::sqrt(projected.x * projected.x + projected.y * projected.y);
    CHECK(approxEqual(radius, kEarthRadiusKm, kToleranceKm));
}

TEST_CASE("orthographic and equidistant agree near the center (both linearize for small angles)")
{
    // For small centralAngle, sin(centralAngle) =~ centralAngle, so the two
    // radial formulas should nearly coincide -- unlike near the horizon,
    // where they diverge sharply (equidistant keeps growing linearly;
    // orthographic's sin() flattens out and then reverses).
    GeoPoint center{0.4f, 1.1f};
    GeoPoint nearbyPoint{0.405f, 1.105f}; // a few hundred km away
    const GeoPoint rotated = rotate(nearbyPoint, center);

    const float centralAngle = kHalfPi - rotated.latRad;
    const float equidistantRadius = centralAngle * kEarthRadiusKm;

    const Point orthoProjected = projectOrthographic(rotated, 1.0f);
    const float orthoRadius = std::sqrt(orthoProjected.x * orthoProjected.x + orthoProjected.y * orthoProjected.y);

    CHECK(approxEqual(orthoRadius, equidistantRadius, 1.0f));
}
