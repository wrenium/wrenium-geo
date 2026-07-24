// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/geo_point.h"

using namespace wrenium::geo;

TEST_CASE("makeGeoPoint converts known degree values to the expected radians")
{
    const GeoPoint p = makeGeoPoint(90.0f, 180.0f);
    CHECK(p.latRad == doctest::Approx(kHalfPi));
    CHECK(p.lonRad == doctest::Approx(kPi));
}

TEST_CASE("makeGeoPoint at zero degrees is the origin")
{
    const GeoPoint p = makeGeoPoint(0.0f, 0.0f);
    CHECK(p.latRad == 0.0f);
    CHECK(p.lonRad == 0.0f);
}

TEST_CASE("makeGeoPoint handles negative degrees")
{
    const GeoPoint p = makeGeoPoint(-45.0f, -90.0f);
    CHECK(p.latRad == doctest::Approx(-kHalfPi * 0.5f));
    CHECK(p.lonRad == doctest::Approx(-kHalfPi));
}
