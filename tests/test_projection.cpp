// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/detail/azimuthal/equidistant.h"
#include "wrenium/geo/detail/azimuthal/rotation.h"
#include "wrenium/geo/projection.h"

#include "fixtures/projection_golden.h"

using namespace wrenium::geo;
using namespace wrenium::geo::azimuthal;
using namespace wrenium_geo_tests;

namespace {

// This library's trig (wrenium-f32math) trades precision for speed --
// its atan2 carries up to ~6e-4 rad of error, dominating the rotate()/
// project() error budget here (float rounding alone would be several
// orders tighter). At the ~10,000-20,000 km magnitudes these cases
// produce, that's up to ~6e-4 * kEarthRadiusKm =~ 3.8 km -- 5 km is a
// margin over that, generous enough to absorb the approximation while
// still catching a real formula regression, which would be off by a
// meaningful fraction of the whole distance, not an approximation-sized
// sliver.
constexpr float kToleranceKm = 5.0f;

bool approxEqual(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

} // namespace

TEST_CASE("rotate + project match hand-derived golden values")
{
    std::size_t count = 0;
    const ProjectionGoldenCase *cases = projectionGoldenCases(count);

    for (std::size_t i = 0; i < count; ++i) {
        const ProjectionGoldenCase &c = cases[i];
        CAPTURE(c.name);

        GeoPoint point{c.pointLatRad, c.pointLonRad};
        GeoPoint center{c.centerLatRad, c.centerLonRad};

        const GeoPoint rotated = rotate(point, center);
        const float centralAngle = kHalfPi - rotated.latRad;
        const float distanceKm = centralAngle * kEarthRadiusKm;

        CHECK(approxEqual(distanceKm, c.expectedDistanceKm, kToleranceKm));

        if (c.checkBearingAndXY) {
            const Point projected = project(rotated, 1.0f);
            CHECK(approxEqual(projected.x, c.expectedX, kToleranceKm));
            CHECK(approxEqual(projected.y, c.expectedY, kToleranceKm));
        }
    }
}

TEST_CASE("center == point projects to the origin regardless of scale")
{
    GeoPoint center{0.9f, -1.4f};
    const GeoPoint rotated = rotate(center, center);
    const Point projected = project(rotated, 42.0f);
    CHECK(approxEqual(projected.x, 0.0f, 0.001f));
    CHECK(approxEqual(projected.y, 0.0f, 0.001f));
}

TEST_CASE("projected radius is exactly distanceKm * scale (azimuthal equidistant's defining property)")
{
    GeoPoint center{0.1f, 0.2f};
    GeoPoint point{0.35f, -0.6f};
    const GeoPoint rotated = rotate(point, center);
    const float centralAngle = kHalfPi - rotated.latRad;
    const float distanceKm = centralAngle * kEarthRadiusKm;

    const float scales[] = {0.01f, 1.0f, 4.5f, 100.0f};
    for (float scale : scales) {
        const Point projected = project(rotated, scale);
        const float radius = std::sqrt(projected.x * projected.x + projected.y * projected.y);
        const float expectedRadius = distanceKm * scale;
        // Tolerance scales with the output magnitude since `scale` can
        // stretch the same underlying distance by orders of magnitude.
        CHECK(approxEqual(radius, expectedRadius, kToleranceKm * (scale > 1.0f ? scale : 1.0f)));
    }
}
