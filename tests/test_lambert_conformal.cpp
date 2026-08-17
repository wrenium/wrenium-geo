// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/conic_pipeline.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"
#include "wrenium/geo/projection.h"
#include "wrenium/geo/workspace.h"

using namespace wrenium::geo;
using namespace wrenium::geo::conic;

namespace {

constexpr float kDeg = kPi / 180.0f;

bool approxEqual(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

// Round-trip tolerance: this projection's own dominant error source is
// atan2()'s ~6e-4 rad error, doubled by unproject()'s `2*atan(t) - pi/2`
// substitution (Snyder's own standard form -- see lambert_conformal.h's
// own comment) to ~1.2e-3 rad, measured directly -- well above every
// other projection family's own round-trip tolerance in this codebase.
constexpr float kRoundTripToleranceRad = 2e-3f;

// Same pushRing() helper as test_mercator.cpp, for the same reason: these
// tests build InputGeometry by hand rather than through
// loadInputGeometry(), so ringMinLat/ringMaxLat (what the clip pre-check
// actually reads) need filling in manually too.
template <std::size_t N, typename Geo>
void pushRing(Geo &geo, const GeoPoint (&points)[N])
{
    float minLat = points[0].latRad;
    float maxLat = points[0].latRad;
    for (const GeoPoint &p : points) {
        geo.points.pushBack(p);
        if (p.latRad < minLat) {
            minLat = p.latRad;
        } else if (p.latRad > maxLat) {
            maxLat = p.latRad;
        }
    }
    geo.ringSizes.pushBack(N);
    geo.ringMinLat.pushBack(minLat);
    geo.ringMaxLat.pushBack(maxLat);
}

} // namespace

TEST_CASE("Lambert conformal conic: the origin point projects to (0, 0)")
{
    LambertConformalConic params{44.0f * kDeg, 49.0f * kDeg, 46.5f * kDeg, 3.0f * kDeg};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const GeoPoint origin{params.originLatRad, params.originLonRad};
    const Point projected = project(origin, frame, 1.0f);
    CHECK(approxEqual(projected.x, 0.0f, 1e-2f));
    CHECK(approxEqual(projected.y, 0.0f, 1e-2f));
}

TEST_CASE("Lambert conformal conic: a tangent cone (equal standard parallels) matches n == sin(parallel)")
{
    LambertConformalConic params{40.0f * kDeg, 40.0f * kDeg, 40.0f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);
    CHECK(frame.n == doctest::Approx(std::sin(40.0f * kDeg)).epsilon(1e-4));
}

TEST_CASE("Lambert conformal conic: project/unproject round-trip across a northern-hemisphere cone")
{
    LambertConformalConic params{44.0f * kDeg, 49.0f * kDeg, 46.5f * kDeg, 3.0f * kDeg};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);
    REQUIRE(frame.n > 0.0f);

    for (float latDeg : {-70.0f, -20.0f, 0.0f, 30.0f, 46.5f, 60.0f, 79.0f}) {
        for (float lonDeg : {-30.0f, -5.0f, 3.0f, 15.0f, 35.0f}) {
            CAPTURE(latDeg);
            CAPTURE(lonDeg);
            const GeoPoint p{latDeg * kDeg, lonDeg * kDeg};
            const Point projected = project(p, frame, 2.0f);
            const GeoPoint recovered = unproject(projected, frame, 2.0f);
            CHECK(approxEqual(recovered.latRad, p.latRad, kRoundTripToleranceRad));
            CHECK(approxEqual(recovered.lonRad, p.lonRad, kRoundTripToleranceRad));
        }
    }
}

TEST_CASE("Lambert conformal conic: project/unproject round-trip across a southern-hemisphere cone (negative n)")
{
    LambertConformalConic params{-18.0f * kDeg, -36.0f * kDeg, -27.0f * kDeg, -60.0f * kDeg};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);
    REQUIRE(frame.n < 0.0f);

    for (float latDeg : {-80.0f, -50.0f, -27.0f, -10.0f, 5.0f}) {
        for (float lonDeg : {-95.0f, -75.0f, -60.0f, -45.0f, -25.0f}) {
            CAPTURE(latDeg);
            CAPTURE(lonDeg);
            const GeoPoint p{latDeg * kDeg, lonDeg * kDeg};
            const Point projected = project(p, frame, 1.5f);
            const GeoPoint recovered = unproject(projected, frame, 1.5f);
            CHECK(approxEqual(recovered.latRad, p.latRad, kRoundTripToleranceRad));
            CHECK(approxEqual(recovered.lonRad, p.lonRad, kRoundTripToleranceRad));
        }
    }
}

TEST_CASE("Lambert conformal conic: a point due east of the central meridian has x > 0, y unaffected in sign")
{
    // North-up, compass-adjacent convention (matching this library's other
    // projections' own north-up output): increasing longitude east of the
    // central meridian should move right (positive x).
    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const Point east = project(GeoPoint{37.5f * kDeg, 10.0f * kDeg}, frame, 1.0f);
    const Point west = project(GeoPoint{37.5f * kDeg, -10.0f * kDeg}, frame, 1.0f);
    CHECK(east.x > 0.0f);
    CHECK(west.x < 0.0f);
}

TEST_CASE("Lambert conformal conic: a point north of the origin has y < 0 (screen convention: y increases downward)")
{
    // Snyder's own published formula is stated in traditional map-space
    // convention (y increases northward); every projection in this
    // library uses screen/SVG convention instead (y increases downward).
    // Porting the formula without flipping the sign round-trips
    // internally consistent (project() then unproject() still recovers
    // the same point) but silently renders upside down, which the
    // round-trip tests elsewhere in this file can't catch on their own.
    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const Point north = project(GeoPoint{47.5f * kDeg, 0.0f}, frame, 1.0f);
    const Point south = project(GeoPoint{27.5f * kDeg, 0.0f}, frame, 1.0f);
    CHECK(north.y < 0.0f);
    CHECK(south.y > 0.0f);
}

TEST_CASE("Lambert conformal conic pipeline: a ring stays one closed piece with all its points, no splitting")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const GeoPoint ring[] = {
        makeGeoPoint(38.0f, 1.0f),
        makeGeoPoint(38.0f, 2.0f),
        makeGeoPoint(37.0f, 2.0f),
        makeGeoPoint(37.0f, 1.0f),
    };
    pushRing(input, ring);

    const Error err = projectRings(workspace, input, frame, 1.0f);
    REQUIRE(err == Error::Ok);
    REQUIRE(workspace.projectedRingSizes().size() == 1);
    CHECK(workspace.projectedRingSizes()[0] == 4);
    CHECK(workspace.stageB.size() == 4);
}

TEST_CASE("Lambert conformal conic pipeline: a ring entirely outside clipLatRad is culled")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const GeoPoint farRing[] = {
        makeGeoPoint(-80.0f, 1.0f),
        makeGeoPoint(-80.0f, 2.0f),
        makeGeoPoint(-81.0f, 2.0f),
        makeGeoPoint(-81.0f, 1.0f),
    };
    pushRing(input, farRing);

    const Error err = projectRings(workspace, input, frame, 1.0f, 10.0f * kDeg, kPi);
    REQUIRE(err == Error::Ok);
    CHECK(workspace.projectedRingSizes().size() == 0);
}

TEST_CASE("Lambert conformal conic pipeline: a ring entirely outside clipLonRad is culled")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const GeoPoint farRing[] = {
        makeGeoPoint(38.0f, 90.0f),
        makeGeoPoint(38.0f, 91.0f),
        makeGeoPoint(37.0f, 91.0f),
        makeGeoPoint(37.0f, 90.0f),
    };
    pushRing(input, farRing);

    const Error err = projectRings(workspace, input, frame, 1.0f, kPi, 10.0f * kDeg);
    REQUIRE(err == Error::Ok);
    CHECK(workspace.projectedRingSizes().size() == 0);
}

TEST_CASE("Lambert conformal conic pipeline: a ring inside the clip window survives it")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const GeoPoint ring[] = {
        makeGeoPoint(38.0f, 1.0f),
        makeGeoPoint(38.0f, 2.0f),
        makeGeoPoint(37.0f, 2.0f),
        makeGeoPoint(37.0f, 1.0f),
    };
    pushRing(input, ring);

    const Error err = projectRings(workspace, input, frame, 1.0f, 10.0f * kDeg, 10.0f * kDeg);
    REQUIRE(err == Error::Ok);
    REQUIRE(workspace.projectedRingSizes().size() == 1);
    CHECK(workspace.projectedRingSizes()[0] == 4);
}

TEST_CASE("Lambert conformal conic pipeline: a ring smaller than 3 points is skipped")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const GeoPoint tooSmall[] = {makeGeoPoint(38.0f, 1.0f), makeGeoPoint(37.0f, 1.0f)};
    pushRing(input, tooSmall);

    const Error err = projectRings(workspace, input, frame, 1.0f);
    REQUIRE(err == Error::Ok);
    CHECK(workspace.projectedRingSizes().size() == 0);
}

TEST_CASE("Lambert conformal conic pipeline: projectLines keeps an open run's exact point count")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const GeoPoint line[] = {
        makeGeoPoint(35.0f, -5.0f),
        makeGeoPoint(37.0f, 0.0f),
        makeGeoPoint(39.0f, 5.0f),
    };
    pushRing(input, line);

    const Error err = projectLines(workspace, input, frame, 1.0f);
    REQUIRE(err == Error::Ok);
    REQUIRE(workspace.projectedRingSizes().size() == 1);
    CHECK(workspace.projectedRingSizes()[0] == 3);
}

TEST_CASE("Lambert conformal conic pipeline: a line shorter than 2 points is skipped")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const GeoPoint tooSmall[] = {makeGeoPoint(35.0f, -5.0f)};
    pushRing(input, tooSmall);

    const Error err = projectLines(workspace, input, frame, 1.0f);
    REQUIRE(err == Error::Ok);
    CHECK(workspace.projectedRingSizes().size() == 0);
}

TEST_CASE("Lambert conformal conic pipeline: a ring crossing the wedge boundary splits into two pieces via boundary-hugging cuts")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    // Straddles the wedge's own open edge (origin lon +- 180 == +-180
    // here, origin lon == 0) -- same shape as test_mercator.cpp's
    // identical antimeridian-crossing test.
    const GeoPoint ring[] = {
        makeGeoPoint(38.0f, 170.0f),
        makeGeoPoint(38.0f, -170.0f),
        makeGeoPoint(36.0f, -170.0f),
        makeGeoPoint(36.0f, 170.0f),
    };
    pushRing(input, ring);

    const Error err = projectRings(workspace, input, frame, 1.0f);
    REQUIRE(err == Error::Ok);
    REQUIRE(workspace.projectedRingSizes().size() == 2);

    // Every input point survives, plus a real interpolated boundary point
    // at each crossing instead of jumping straight across the map: 4
    // original points, +2 for the mid-walk crossing's own exit+entry
    // pair, +1 for the rotation crossing's own exit closing the loop (no
    // entry -- no new piece starts there) -- same accounting as
    // test_mercator.cpp's identical test.
    std::size_t totalPoints = 0;
    for (std::size_t i = 0; i < workspace.projectedRingSizes().size(); ++i) {
        totalPoints += workspace.projectedRingSizes()[i];
    }
    CHECK(totalPoints == 7);

    // Each piece stays on its own side of the wedge (x <= 0 throughout
    // one, x >= 0 throughout the other) rather than a chord cutting
    // across the whole width -- the actual property this splitting
    // exists to guarantee.
    const Point *pts = workspace.projectedPoints();
    std::size_t idx = 0;
    for (std::size_t r = 0; r < workspace.projectedRingSizes().size(); ++r) {
        const std::size_t size = workspace.projectedRingSizes()[r];
        const bool negativeSide = pts[idx].x < 0.0f;
        for (std::size_t i = 0; i < size; ++i) {
            CAPTURE(r);
            CAPTURE(i);
            CHECK((negativeSide ? pts[idx + i].x <= 1e-3f : pts[idx + i].x >= -1e-3f));
        }
        idx += size;
    }
}

TEST_CASE("Lambert conformal conic pipeline: a ring encircling this frame's own finite pole closes through the apex")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);
    REQUIRE(frame.n > 0.0f); // north is this frame's own finite pole

    // Four points spanning the full 360 degrees of longitude near the
    // north pole -- a ring that genuinely encircles it.
    const GeoPoint ring[] = {
        makeGeoPoint(80.0f, 0.0f),
        makeGeoPoint(80.0f, 90.0f),
        makeGeoPoint(80.0f, 180.0f),
        makeGeoPoint(80.0f, -90.0f),
    };
    pushRing(input, ring);

    const Error err = projectRings(workspace, input, frame, 1.0f);
    REQUIRE(err == Error::Ok);
    REQUIRE(workspace.projectedRingSizes().size() == 1);
    // The 4 original points plus one apex point closing the loop through
    // the cone's own apex (this file's own overview comment).
    CHECK(workspace.projectedRingSizes()[0] == 5);

    const Point *pts = workspace.projectedPoints();
    const Point &apexPoint = pts[4];
    CHECK(approxEqual(apexPoint.x, 0.0f, 1e-3f));
    CHECK(approxEqual(apexPoint.y, -frame.rho0, 1e-3f));
}

TEST_CASE("Lambert conformal conic pipeline: a ring encircling the opposite (infinite) pole is skipped")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);
    REQUIRE(frame.n > 0.0f); // north is finite, south recedes to infinity

    // Same shape as the previous test, but encircling the *south* pole --
    // this frame (both standard parallels northern) has no finite point
    // to route a closing edge through there.
    const GeoPoint ring[] = {
        makeGeoPoint(-80.0f, 0.0f),
        makeGeoPoint(-80.0f, 90.0f),
        makeGeoPoint(-80.0f, 180.0f),
        makeGeoPoint(-80.0f, -90.0f),
    };
    pushRing(input, ring);

    const Error err = projectRings(workspace, input, frame, 1.0f);
    REQUIRE(err == Error::Ok);
    CHECK(workspace.projectedRingSizes().size() == 0);
}

TEST_CASE("Lambert conformal conic pipeline: a ring whose first point sits exactly on the wedge boundary is still detected as visible")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    // The actual bug this guards against: a ring's own first point
    // sitting exactly at the wedge's open edge (a real, common data
    // convention -- the checked-in world coastline dataset's own
    // Eurasia+Africa ring starts exactly there) seeds the coarse
    // visibility pre-check's accumulation at +-kPi, and the ring's own
    // real path can accumulate entirely on one side of that seed without
    // ever numerically re-approaching the visible window, even though
    // the ring plainly contains visible points -- see conic_pipeline.h's
    // own comment on the shifted-window checks this test exercises.
    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    const GeoPoint ring[] = {
        makeGeoPoint(38.0f, -180.0f),
        makeGeoPoint(38.0f, -10.0f),
        makeGeoPoint(36.0f, 160.0f),
        makeGeoPoint(36.0f, -8.0f),
    };
    pushRing(input, ring);

    // A narrow visible window -- wide enough to contain the ring's own
    // real (-10, -8 degree) points, narrow enough that detecting this
    // ring needs the shifted-window check specifically.
    const Error err = projectRings(workspace, input, frame, 1.0f, kPi, 20.0f * kDeg);
    REQUIRE(err == Error::Ok);
    CHECK(workspace.projectedRingSizes().size() > 0);
}

TEST_CASE("Lambert conformal conic pipeline: a line crossing the wedge boundary splits into two open runs")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    LambertConformalConic params{30.0f * kDeg, 45.0f * kDeg, 37.5f * kDeg, 0.0f};
    const LambertConformalConicFrame frame = makeLambertConformalConicFrame(params);

    // Two points on each side of the crossing, so both split-off pieces
    // are genuinely drawable (>=2 points) -- same shape as
    // test_mercator.cpp's identical test.
    const GeoPoint line[] = {
        makeGeoPoint(35.0f, 165.0f),
        makeGeoPoint(36.0f, 175.0f),
        makeGeoPoint(37.0f, -175.0f),
        makeGeoPoint(38.0f, -170.0f),
    };
    pushRing(input, line);

    const Error err = projectLines(workspace, input, frame, 1.0f);
    REQUIRE(err == Error::Ok);
    REQUIRE(workspace.projectedRingSizes().size() == 2);
    // Each original point survives, plus a real interpolated boundary
    // point cut into each run at the crossing: 2 original points + 1
    // boundary exit for the first run, 1 boundary entry + 2 original
    // points for the second.
    CHECK(workspace.projectedRingSizes()[0] == 3);
    CHECK(workspace.projectedRingSizes()[1] == 3);

    const Point *pts = workspace.projectedPoints();
    CHECK(pts[0].x > 0.0f);
    CHECK(pts[1].x > 0.0f);
    CHECK(pts[4].x < 0.0f);
    CHECK(pts[5].x < 0.0f);
}
