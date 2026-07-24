// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#ifndef WRENIUM_GEO_TESTS_PROJECTION_GOLDEN_H
#define WRENIUM_GEO_TESTS_PROJECTION_GOLDEN_H

#include <cstddef>

// Hand-derived golden values for the rotate()/project() pipeline
// (wrenium/geo/detail/azimuthal/rotation.h + detail/azimuthal/equidistant.h),
// used by tests/test_projection.cpp.
//
// No Node.js/d3-geo output is available in this environment, so these are
// analytically hand-derived spherical-trig values instead, computed
// independently of the C++ implementation (double-precision Python, not
// the library itself) and then cross-checked against known geometric
// facts about the azimuthal equidistant projection. TODO: swap these in
// for real d3-geo-generated fixture values later (the case list/
// derivation-comment structure below is deliberately kept simple to make
// that swap a data change, not a restructuring) -- a deferred
// nice-to-have, not a blocker.
//
// Derivations:
//
//  1. "center == point": trivially, the distance from a point to itself is
//     0 along any great circle, so centralAngle == 0, and therefore
//     projected (x, y) == (0, 0) regardless of scale.
//
//  2/3/4/5. "90 degrees along a cardinal direction": for center == (0, 0)
//     (the equator/prime-meridian intersection) and a point exactly 90
//     degrees away along a meridian or the equator, the great-circle
//     distance is exactly (pi/2) radians of arc -- one quarter of the
//     sphere's circumference -- independent of *which* cardinal direction,
//     since all four points are equally 90 degrees of arc from the origin
//     along the sphere's own axes. In kilometers that is
//     (pi/2) * kEarthRadiusKm = (pi/2) * 6371 = 10007.5433... km. Bearing
//     is exactly north/east/south/west (0, pi/2, pi, -pi/2 rad
//     respectively) by construction, since the point lies exactly on the
//     corresponding cardinal axis from the center. Azimuthal equidistant's
//     defining property (projected radius == true distance * scale) then
//     gives (x, y) directly from (distanceKm * scale, bearing) via
//     x = r*sin(bearing), y = -r*cos(bearing) (north-up, screen y-down).
//
//  6. "antipodal": center == (0, 0), point == (0, pi) (halfway around the
//     equator). The great-circle distance between antipodal points is
//     exactly half the sphere's circumference: pi radians of arc, i.e.
//     pi * kEarthRadiusKm = 20015.0868... km -- the maximum possible
//     distance the projection can ever produce. The *bearing* at an exact
//     antipode is mathematically undefined (every direction is a valid
//     shortest path), so this case only checks the distance/radius
//     magnitude, not a specific (x, y) -- asserting an exact bearing here
//     would be asserting floating-point noise, not geometry (confirmed
//     empirically: the reference double-precision computation and this
//     library's float implementation pick different, equally "valid"
//     bearings at this exact point due to sub-ULP noise in sin(pi)).
//
//  7. "general off-axis": a non-cardinal, non-degenerate case (center at
//     roughly 28.6N 17.2E, point at roughly 11.5N 5.7W) included so the
//     fixture isn't exclusively testing the easy symmetric cases. Expected
//     values computed with double-precision Python
//     (math.sin/cos/atan2/hypot, the same formulas detail/azimuthal/rotation.h
//     implements in single precision) as an independent numerical
//     reference, not by running this library's own code.

namespace wrenium_geo_tests {

struct ProjectionGoldenCase
{
    const char *name;
    float pointLatRad;
    float pointLonRad;
    float centerLatRad;
    float centerLonRad;
    float expectedDistanceKm;
    bool checkBearingAndXY; // false only for the antipodal case (see above)
    float expectedX;        // at scale = 1.0
    float expectedY;        // at scale = 1.0
};

inline const ProjectionGoldenCase *projectionGoldenCases(std::size_t &count)
{
    static const ProjectionGoldenCase cases[] = {
        // name, pointLat, pointLon, centerLat, centerLon, distKm, checkXY, x, y
        {"center == point", 0.4f, -0.7f, 0.4f, -0.7f, 0.0f, true, 0.0f, 0.0f},
        {"90 deg north (meridian)", 1.5707963268f, 0.0f, 0.0f, 0.0f, 10007.543398f, true, 0.0f, -10007.543398f},
        {"90 deg east (equator)", 0.0f, 1.5707963268f, 0.0f, 0.0f, 10007.543398f, true, 10007.543398f, 0.0f},
        {"90 deg west (equator)", 0.0f, -1.5707963268f, 0.0f, 0.0f, 10007.543398f, true, -10007.543398f, 0.0f},
        {"90 deg south (meridian)", -1.5707963268f, 0.0f, 0.0f, 0.0f, 10007.543398f, true, 0.0f, 10007.543398f},
        {"antipodal", 0.0f, 3.14159265359f, 0.0f, 0.0f, 20015.086796f, false, 0.0f, 0.0f},
        {"general off-axis", 0.2f, -0.1f, 0.5f, 0.3f, 3051.907588f, true, -2527.075249f, 1711.148915f},
    };
    count = sizeof(cases) / sizeof(cases[0]);
    return cases;
}

} // namespace wrenium_geo_tests

#endif // WRENIUM_GEO_TESTS_PROJECTION_GOLDEN_H
