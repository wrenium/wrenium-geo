// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/cylindrical_pipeline.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"
#include "wrenium/geo/projection.h"
#include "wrenium/geo/workspace.h"

using namespace wrenium::geo;
using namespace wrenium::geo::cylindrical;

namespace {

// Independent oracle for project()'s expected output: plain <cmath>
// std::atanh, not wrenium-f32math's own approximation -- a real check
// against a different implementation, not just self-consistency.
float expectedMercatorY(float latRad, float scale) // NOLINT(bugprone-easily-swappable-parameters)
{
    float lat = latRad;
    if (lat > kMercatorMaxLatRad) {
        lat = kMercatorMaxLatRad;
    } else if (lat < -kMercatorMaxLatRad) {
        lat = -kMercatorMaxLatRad;
    }
    return static_cast<float>(-std::atanh(static_cast<double>(std::sin(lat))) * kEarthRadiusKm * scale);
}

Point expectedMercator(const GeoPoint &point, const GeoPoint &center, float scale)
{
    float lonDelta = point.lonRad - center.lonRad;
    while (lonDelta > kPi) {
        lonDelta -= 2.0f * kPi;
    }
    while (lonDelta <= -kPi) {
        lonDelta += 2.0f * kPi;
    }
    const float x = lonDelta * kEarthRadiusKm * scale;
    const float y = expectedMercatorY(point.latRad, scale) - expectedMercatorY(center.latRad, scale);
    return Point{x, y};
}

bool approxEqual(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

// Pushes one ring/run's points plus its ringSizes/ringMinLat/ringMaxLat
// entries -- what loadInputGeometry() (input_format.h) computes automatically
// from the real binary wire format, but these tests build InputGeometry
// by hand. Needed for any test that exercises a non-default clipLatRad/
// clipLonRad: the culling pre-check reads ringMinLat/ringMaxLat directly,
// so a ring pushed without this would cull (or fail to cull) based on
// whatever was left in those buffers, not real data.
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

TEST_CASE("Mercator: center is a true 2D recenter point -- always projects to (0, 0)")
{
    // Unlike a plain "fix the reference meridian" scheme, center's own
    // latitude is subtracted too (project()'s own doc comment), so
    // center always lands exactly on the origin, matching the azimuthal
    // family's own "center maps to (0, 0)" convention -- just achieved
    // via two independent offsets (longitude for x, projected-y for y)
    // rather than a rotation.
    const GeoPoint center = makeGeoPoint(50.0f, 10.0f);
    const Point centerProjected = project(center, center, 1.0f);
    CHECK(approxEqual(centerProjected.x, 0.0f, 0.5f));
    CHECK(approxEqual(centerProjected.y, 0.0f, 0.5f));

    // A point sharing center's meridian but not its latitude still lands
    // on x == 0 (only longitude affects x), but *not* on y == 0 anymore
    // -- y == 0 is reserved for center's own latitude now, not the
    // equator specifically.
    const GeoPoint onEquatorSameMeridian = makeGeoPoint(0.0f, 10.0f);
    const Point equatorProjected = project(onEquatorSameMeridian, center, 1.0f);
    CHECK(approxEqual(equatorProjected.x, 0.0f, 0.5f));
    CHECK(approxEqual(equatorProjected.y, expectedMercator(onEquatorSameMeridian, center, 1.0f).y, 0.5f));
    // ...and that y is *not* 0 -- the equator is well away from center's
    // own 50 deg latitude, so this actually exercises the new behavior
    // rather than coincidentally passing at y == 0 too.
    CHECK(std::fabs(equatorProjected.y) > 100.0f);
}

TEST_CASE("Mercator: project() at center == equator/prime-meridian is unchanged by the 2D-recenter fix")
{
    // Regression check: every existing whole-world visual verification
    // and test used center == (0, 0), where centerY == 0 exactly
    // (atanh(sin(0)) == 0), so the 2D-recenter subtraction is a no-op --
    // confirm that directly rather than assuming it from the algebra.
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const GeoPoint points[] = {
        makeGeoPoint(0.0f, 0.0f),
        makeGeoPoint(45.0f, 45.0f),
        makeGeoPoint(-60.0f, -170.0f),
    };
    for (const GeoPoint &p : points) {
        CAPTURE(p.latRad);
        const Point got = project(p, center, 1.0f);
        CHECK(approxEqual(got.y, expectedMercatorY(p.latRad, 1.0f), 0.1f));
    }
}

TEST_CASE("Mercator: matches an independent <cmath> oracle at several points")
{
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const GeoPoint points[] = {
        makeGeoPoint(0.0f, 0.0f),
        makeGeoPoint(45.0f, 45.0f),
        makeGeoPoint(-30.0f, 100.0f),
        makeGeoPoint(60.0f, -120.0f),
        makeGeoPoint(-60.0f, -170.0f),
    };
    for (const GeoPoint &p : points) {
        CAPTURE(p.latRad);
        CAPTURE(p.lonRad);
        const Point got = project(p, center, 1.0f);
        const Point want = expectedMercator(p, center, 1.0f);
        CHECK(approxEqual(got.x, want.x, 2.0f));
        CHECK(approxEqual(got.y, want.y, 2.0f));
    }
}

TEST_CASE("Mercator: latitude beyond the pole limit clamps instead of diverging")
{
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const Point atLimit = project(makeGeoPoint(85.0511287798f, 0.0f), center, 1.0f);
    const Point pastLimit = project(makeGeoPoint(89.9f, 0.0f), center, 1.0f);
    const Point wayPastLimit = project(makeGeoPoint(90.0f, 0.0f), center, 1.0f);

    CHECK(approxEqual(atLimit.y, pastLimit.y, 1.0f));
    CHECK(approxEqual(atLimit.y, wayPastLimit.y, 1.0f));
    // pi * kEarthRadiusKm -- the exact y at the clamp limit (see
    // mercator.h). Tolerance matches f32math::atanh's own measured
    // worst-case error right at this domain edge (~1.2e-3 rad, see
    // wrenium-f32math's atanh.h) scaled by kEarthRadiusKm, not a
    // tighter bound that happens to pass.
    CHECK(approxEqual(-atLimit.y, kPi * kEarthRadiusKm, 10.0f));
}

TEST_CASE("Mercator: clampCenterLatForViewport keeps the whole viewport in the valid range, not just the center point")
{
    // A wide-zoom-style scale/viewport -- the real scenario the bug this
    // function fixes was found in (mercatormap's own drag-to-pan): a flat
    // +-kMercatorMaxLatRad clamp on the center alone let a pole's own
    // coastline land at the vertical *middle* of the viewport instead of
    // near the edge, since it never accounted for how much of the map the
    // viewport itself covers at this zoom.
    const float scale = 835.0f / 2.0f / 16000.0f;
    const float viewportHeightPx = 620.0f;
    const float maxY = -expectedMercatorY(kMercatorMaxLatRad, scale);

    const float southClamped = clampCenterLatForViewport(-89.0f * kPi / 180.0f, scale, viewportHeightPx);
    // The old flat clamp would allow all the way to -kMercatorMaxLatRad;
    // this must stop well short of it at this zoom.
    CHECK(southClamped > -kMercatorMaxLatRad);
    CHECK(southClamped < 0.0f);
    // The viewport's own far (south) edge must not cross the map's valid
    // y range -- south is positive y (see mercator.h). Tolerance covers
    // f32math's own round-trip approximation error, not a loose bound
    // that happens to pass.
    const float southCenterY = expectedMercatorY(southClamped, scale);
    CHECK(southCenterY + viewportHeightPx / 2.0f <= maxY + 5.0f);

    const float northClamped = clampCenterLatForViewport(89.0f * kPi / 180.0f, scale, viewportHeightPx);
    CHECK(northClamped < kMercatorMaxLatRad);
    CHECK(northClamped > 0.0f);
    const float northCenterY = expectedMercatorY(northClamped, scale);
    CHECK(-northCenterY + viewportHeightPx / 2.0f <= maxY + 5.0f);
}

TEST_CASE("Mercator: clampCenterLatForViewport leaves a candidate well within bounds unchanged")
{
    // A tight zoom -- the viewport covers only a small fraction of the
    // map's full valid height, so a mid-latitude candidate has plenty of
    // headroom and should pass through untouched.
    const float scale = 835.0f / 2.0f / 500.0f;
    const float viewportHeightPx = 620.0f;
    const float candidate = -45.0f * kPi / 180.0f;

    // Passes through the library's own projectY -> asin(tanh()) round
    // trip even when nothing needs clamping, so the tolerance is that
    // round trip's own measured error at this (non-edge) latitude
    // (~1.8e-4 rad), not exact equality.
    const float clamped = clampCenterLatForViewport(candidate, scale, viewportHeightPx);
    CHECK(approxEqual(clamped, candidate, 3e-4f));
}

TEST_CASE("Mercator: clampCenterLatForViewport locks to the equator once the viewport is taller than the whole valid map")
{
    // Deliberately extreme: at this scale/height the viewport's own half
    // height already exceeds the map's entire valid y range, so no
    // latitude avoids dead space on both edges -- centering on the
    // equator is the only sensible answer, matching how standard web map
    // libraries lock vertical panning entirely once zoomed out that far.
    const float scale = 0.01f;
    const float viewportHeightPx = 1.0e9f;

    CHECK(clampCenterLatForViewport(0.5f, scale, viewportHeightPx) == 0.0f);
    CHECK(clampCenterLatForViewport(-0.5f, scale, viewportHeightPx) == 0.0f);
    CHECK(clampCenterLatForViewport(kMercatorMaxLatRad, scale, viewportHeightPx) == 0.0f);
}

TEST_CASE("Mercator pipeline: a ring that doesn't cross the antimeridian stays one closed piece")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    const GeoPoint square[] = {
        makeGeoPoint(10.0f, 10.0f),
        makeGeoPoint(10.0f, 20.0f),
        makeGeoPoint(20.0f, 20.0f),
        makeGeoPoint(20.0f, 10.0f),
    };
    for (const GeoPoint &p : square) {
        input.points.pushBack(p);
    }
    input.ringSizes.pushBack(4);

    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const Error err = projectRings(workspace, input, center, 1.0f);

    REQUIRE(err == Error::Ok);
    REQUIRE(workspace.projectedRingSizes().size() == 1);
    CHECK(workspace.projectedRingSizes()[0] == 4);
}

TEST_CASE("Mercator pipeline: a ring crossing the antimeridian splits into two pieces via boundary-hugging cuts, not a line across the map")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    // Straddles +-180 degrees: 170 -> -170 -> -170 -> 170.
    const GeoPoint ring[] = {
        makeGeoPoint(10.0f, 170.0f),
        makeGeoPoint(10.0f, -170.0f),
        makeGeoPoint(20.0f, -170.0f),
        makeGeoPoint(20.0f, 170.0f),
    };
    for (const GeoPoint &p : ring) {
        input.points.pushBack(p);
    }
    input.ringSizes.pushBack(4);

    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const Error err = projectRings(workspace, input, center, 1.0f);

    REQUIRE(err == Error::Ok);
    REQUIRE(workspace.projectedRingSizes().size() == 2);

    // Every input point survives, plus a real interpolated boundary point
    // at each crossing (see cylindrical_pipeline.h's own overview comment)
    // instead of jumping straight across the map: 4 original points, +2
    // for the mid-walk crossing's own exit+entry pair, +1 for the
    // rotation crossing's own exit closing the loop (no entry -- no new
    // piece starts there).
    std::size_t totalPoints = 0;
    for (std::size_t i = 0; i < workspace.projectedRingSizes().size(); ++i) {
        totalPoints += workspace.projectedRingSizes()[i];
    }
    CHECK(totalPoints == 7);

    // Each piece stays on its own side of the map (x <= 0 throughout one,
    // x >= 0 throughout the other, meeting only exactly at the boundary)
    // rather than drawing a line across the whole width -- the actual
    // property this splitting exists to guarantee. The boundary points
    // themselves land exactly at +-halfWorldWidth.
    const float halfWorldWidth = kPi * kEarthRadiusKm;
    const Point *pts = workspace.projectedPoints();
    std::size_t idx = 0;
    for (std::size_t r = 0; r < workspace.projectedRingSizes().size(); ++r) {
        const std::size_t size = workspace.projectedRingSizes()[r];
        const bool negativeSide = pts[idx].x < 0.0f;
        for (std::size_t i = 0; i < size; ++i) {
            CAPTURE(r);
            CAPTURE(i);
            CHECK((negativeSide ? pts[idx + i].x <= 0.0f : pts[idx + i].x >= 0.0f));
        }
        // Last point of each piece is its own boundary cut.
        CHECK(approxEqual(std::fabs(pts[idx + size - 1].x), halfWorldWidth, 1.0f));
        idx += size;
    }
}

TEST_CASE("Mercator pipeline: a line crossing the antimeridian splits into two open runs")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    // Two points on each side of the crossing, so both split-off pieces
    // are genuinely drawable (>=2 points) -- a single-point leftover
    // piece is correctly dropped (mirrors clipLineToSink's own runSize <
    // 2 rejection), which is *not* what this test is checking.
    const GeoPoint line[] = {
        makeGeoPoint(5.0f, 165.0f),
        makeGeoPoint(10.0f, 175.0f),
        makeGeoPoint(15.0f, -175.0f),
        makeGeoPoint(20.0f, -170.0f),
    };
    for (const GeoPoint &p : line) {
        input.points.pushBack(p);
    }
    input.ringSizes.pushBack(4);

    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const Error err = projectLines(workspace, input, center, 1.0f);

    REQUIRE(err == Error::Ok);
    REQUIRE(workspace.projectedRingSizes().size() == 2);
    // Each original point survives, plus a real interpolated boundary
    // point cut into each run at the crossing (see
    // cylindrical_pipeline.h's own overview comment) instead of jumping
    // straight across the map: 2 original points + 1 boundary exit for
    // the first run, 1 boundary entry + 2 original points for the second.
    CHECK(workspace.projectedRingSizes()[0] == 3);
    CHECK(workspace.projectedRingSizes()[1] == 3);

    // Both runs stay on their own side of the map, meeting only exactly
    // at the boundary -- the same property projectRings()'s own
    // identical test checks. The first run (165, 175 degrees) stays
    // positive; the second (-175, -170 degrees) stays negative.
    const float halfWorldWidth = kPi * kEarthRadiusKm;
    const Point *pts = workspace.projectedPoints();
    CHECK(pts[0].x > 0.0f);
    CHECK(pts[1].x > 0.0f);
    CHECK(approxEqual(pts[2].x, halfWorldWidth, 1.0f));
    CHECK(approxEqual(pts[3].x, -halfWorldWidth, 1.0f));
    CHECK(pts[4].x < 0.0f);
    CHECK(pts[5].x < 0.0f);
}

TEST_CASE("Mercator: unproject is the exact inverse of project")
{
    const GeoPoint centers[] = {
        makeGeoPoint(0.0f, 0.0f),
        makeGeoPoint(50.0f, 10.0f), // non-zero latitude -- exercises the 2D recenter fix
        makeGeoPoint(-30.0f, -100.0f),
    };
    const GeoPoint points[] = {
        makeGeoPoint(0.0f, 0.0f),
        makeGeoPoint(45.0f, 45.0f),
        makeGeoPoint(-60.0f, -170.0f),
        makeGeoPoint(80.0f, 5.0f),
        makeGeoPoint(-84.9f, 179.9f),
    };
    const float scales[] = {0.02f, 1.0f, 5.0f};

    for (const GeoPoint &center : centers) {
        for (const GeoPoint &p : points) {
            for (const float scale : scales) {
                CAPTURE(center.latRad);
                CAPTURE(p.latRad);
                CAPTURE(p.lonRad);
                CAPTURE(scale);
                const Point projected = project(p, center, scale);
                const GeoPoint recovered = unproject(projected, center, scale);
                CHECK(approxEqual(recovered.latRad, p.latRad, 0.01f));
                CHECK(approxEqual(recovered.lonRad, p.lonRad, 0.01f));
            }
        }
    }
}

TEST_CASE("Mercator: unproject saturates towards a pole for y far outside the valid map")
{
    // A click well above/below the rendered map (e.g. a resized window
    // that's taller than the current zoom level's natural extent) must
    // not hit tanh()'s undefined behavior outside its fitted domain --
    // it should saturate towards the nearest pole instead. Tolerance
    // matches asin(tanh(...))'s own measured worst-case error right at
    // this domain edge (~3.8e-3 rad -- asin's derivative grows steeply
    // as its argument approaches +-1, amplifying tanh's own small error
    // there, see wrenium-f32math's tanh.h), not a tighter bound that
    // happens to pass.
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const GeoPoint north = unproject(Point{0.0f, -1.0e6f}, center, 1.0f);
    const GeoPoint south = unproject(Point{0.0f, 1.0e6f}, center, 1.0f);
    CHECK(approxEqual(north.latRad, kMercatorMaxLatRad, 0.01f));
    CHECK(approxEqual(south.latRad, -kMercatorMaxLatRad, 0.01f));
}

TEST_CASE("Mercator: unproject wraps longitude for x many world-widths away")
{
    // A wild click far outside a single map copy still returns a sane,
    // bounded longitude via wrapPi rather than an ever-growing value.
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const float halfWorldWidth = kPi * kEarthRadiusKm;
    const GeoPoint far = unproject(Point{3.0f * halfWorldWidth, 0.0f}, center, 1.0f);
    CHECK(far.lonRad <= kPi);
    CHECK(far.lonRad > -kPi);
    CHECK(approxEqual(std::fabs(far.lonRad), kPi, 0.01f));
}

TEST_CASE("Mercator: unproject stays well-defined for longitude exactly at the antimeridian boundary")
{
    // The pipeline's own antimeridian-splitting logic (cylindrical_pipeline.h)
    // has to special-case points whose raw longitude sits exactly at the
    // wrap boundary; unproject() has no "neighboring point" to stay
    // consistent with (it's a single-point inverse), so either sign at
    // the boundary is a correct answer here -- just confirm it's a real,
    // finite, in-range result, not NaN or a wild value.
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const float halfWorldWidth = kPi * kEarthRadiusKm;
    const GeoPoint atEdge = unproject(Point{halfWorldWidth, 0.0f}, center, 1.0f);
    CHECK(std::isfinite(atEdge.lonRad));
    CHECK(approxEqual(std::fabs(atEdge.lonRad), kPi, 0.01f));

    const GeoPoint centerAtEdge = makeGeoPoint(0.0f, 180.0f);
    const GeoPoint stillAtEdge = unproject(Point{0.0f, 0.0f}, centerAtEdge, 1.0f);
    CHECK(std::isfinite(stillAtEdge.lonRad));
    CHECK(approxEqual(std::fabs(stillAtEdge.lonRad), kPi, 0.01f));
}

TEST_CASE("Mercator: the point<->center swap identity used for pan/zoom-to-cursor holds")
{
    // WreniumGeoBridge's recenterKeepingPointFixed() (the shared building
    // block behind both drag-to-pan and scroll-to-zoom-toward-cursor in
    // examples/mercatormap) computes "what center puts anchor exactly at
    // desiredOutput" as unproject(-desiredOutput, anchor, scale) -- relying
    // on project()'s x and y both being antisymmetric under swapping
    // point<->center. Verify that identity directly at the core-library
    // level, across several anchors/outputs/scales, rather than trusting
    // the hand derivation.
    const GeoPoint anchors[] = {
        makeGeoPoint(10.0f, 20.0f),
        makeGeoPoint(-45.0f, 100.0f),
        makeGeoPoint(60.0f, -150.0f),
    };
    const Point desiredOutputs[] = {
        Point{0.0f, 0.0f},
        Point{200.0f, -150.0f},
        Point{-500.0f, 300.0f},
    };
    const float scale = 0.05f;

    for (const GeoPoint &anchor : anchors) {
        for (const Point &desired : desiredOutputs) {
            CAPTURE(anchor.latRad);
            CAPTURE(anchor.lonRad);
            CAPTURE(desired.x);
            CAPTURE(desired.y);
            const GeoPoint newCenter = unproject(Point{-desired.x, -desired.y}, anchor, scale);
            const Point got = project(anchor, newCenter, scale);
            CHECK(approxEqual(got.x, desired.x, 2.0f));
            CHECK(approxEqual(got.y, desired.y, 2.0f));
        }
    }
}

TEST_CASE("Mercator: a ring entirely outside the clip window is culled")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    const GeoPoint square[] = {
        makeGeoPoint(10.0f, 10.0f),
        makeGeoPoint(10.0f, 20.0f),
        makeGeoPoint(20.0f, 20.0f),
        makeGeoPoint(20.0f, 10.0f),
    };
    pushRing(input, square);

    // Window centered on the north pole -- nowhere near this ring's
    // 10-20 deg latitude band.
    const GeoPoint center = makeGeoPoint(89.0f, 0.0f);
    const Error err = projectRings(workspace, input, center, 1.0f, 0.05f, 0.05f);

    REQUIRE(err == Error::Ok);
    CHECK(workspace.projectedRingSizes().size() == 0);
}

TEST_CASE("Mercator: a ring only partially inside the clip window is kept in full, not trimmed")
{
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    const GeoPoint square[] = {
        makeGeoPoint(5.0f, 10.0f),
        makeGeoPoint(5.0f, 20.0f),
        makeGeoPoint(25.0f, 20.0f),
        makeGeoPoint(25.0f, 10.0f),
    };
    pushRing(input, square);

    // Window covers only the ring's lower half (lat 5-25, window -5..15) --
    // the culling pre-check is ring-granularity, not point-granularity, so
    // every point should still come through.
    const GeoPoint center = makeGeoPoint(5.0f, 15.0f);
    const Error err = projectRings(workspace, input, center, 1.0f, 0.1745f, 0.1745f); // ~10 deg

    REQUIRE(err == Error::Ok);
    REQUIRE(workspace.projectedRingSizes().size() == 1);
    CHECK(workspace.projectedRingSizes()[0] == 4);
}

TEST_CASE("Mercator: a window overlapping a ring's own point never wrongly culls it")
{
    // Centering directly on one of a ring's own points guarantees overlap
    // by construction -- a robust way to test "never wrongly excludes"
    // without hand-deriving the ring's full accumulated longitude bounds.
    // Used here with a synthetic ring whose longitude span is wide
    // (~170 degrees) but that does *not* encircle a pole -- the same
    // shape of case as the real checked-in dataset's own combined
    // Eurasia+Africa ring. A ring this wide necessarily has a real
    // antimeridian crossing in its raw (stored, +-180-bounded) data --
    // that's not something culling introduces, it's the pre-existing,
    // separately-tested splitting logic -- so the *culling* property this
    // test checks is "no points silently disappear", not "stays one
    // piece" (see the antimeridian-splitting tests above for that).
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    const GeoPoint wideRing[] = {
        makeGeoPoint(5.0f, 170.0f),
        makeGeoPoint(5.0f, -170.0f),
        makeGeoPoint(15.0f, -20.0f),
        makeGeoPoint(15.0f, 170.0f),
    };
    pushRing(input, wideRing);

    const GeoPoint center = makeGeoPoint(5.0f, 170.0f); // == wideRing[0]
    const Error err = projectRings(workspace, input, center, 1.0f, 0.05f, 0.05f);

    REQUIRE(err == Error::Ok);
    std::size_t totalPoints = 0;
    for (std::size_t i = 0; i < workspace.projectedRingSizes().size(); ++i) {
        totalPoints += workspace.projectedRingSizes()[i];
    }
    CHECK(totalPoints == 4);
}

TEST_CASE("Mercator: a pole-encircling ring is culled by latitude, never by longitude")
{
    // A synthetic ring at a constant high (southern) latitude, spanning
    // all four longitude quadrants -- nets a full turn of winding, same
    // as the real dataset's own Antarctica ring.
    const GeoPoint poleRing[] = {
        makeGeoPoint(-80.0f, -90.0f),
        makeGeoPoint(-80.0f, 0.0f),
        makeGeoPoint(-80.0f, 90.0f),
        makeGeoPoint(-80.0f, 180.0f),
    };

    {
        // A window nowhere near -80 deg latitude, but at every longitude
        // (clipLonRad == kPi) -- should still be culled, by latitude
        // alone. Confirms this ring gets no free pass from the
        // longitude check when it's genuinely not visible.
        static Workspace<64, 8> workspace;
        static InputGeometry<64, 8> input;
        pushRing(input, poleRing);
        const GeoPoint farCenter = makeGeoPoint(0.0f, 0.0f);
        const Error err = projectRings(workspace, input, farCenter, 1.0f, 0.1f, kPi);
        REQUIRE(err == Error::Ok);
        CHECK(workspace.projectedRingSizes().size() == 0);
    }
    {
        // A window near -80 deg latitude, but with a *narrow* longitude
        // half-range -- should still be kept, since a pole-encircling
        // ring's own accumulated longitude range always spans a full
        // turn (see cylindrical_pipeline.h's own comment on this).
        static Workspace<64, 8> workspace;
        static InputGeometry<64, 8> input;
        pushRing(input, poleRing);
        const GeoPoint nearCenter = makeGeoPoint(-80.0f, 0.0f);
        const Error err = projectRings(workspace, input, nearCenter, 1.0f, 0.1f, 0.01f);
        REQUIRE(err == Error::Ok);
        REQUIRE(workspace.projectedRingSizes().size() == 1);
        // +2 for the pole square-off corners this ring's own encircling
        // logic adds -- see projectRings()'s own comment.
        CHECK(workspace.projectedRingSizes()[0] == 6);
    }
}

TEST_CASE("Mercator pipeline: a wide (>180 degree) ring stays correctly positioned for a center far from its own raw wrap point")
{
    // Reproduces a real, live-reported bug: the checked-in dataset's own
    // combined Eurasia+Africa ring (spanning ~197 degrees of true
    // longitude) rendered entirely blank for a center near Gibraltar
    // (lat=36.3752, lon=-3.1838) -- traced (in the pipeline's earlier,
    // seeded-accumulator design) to this ring's walk landing a full turn
    // (360 degrees) away from where it belongs. That design and its own
    // later patches are gone (see cylindrical_pipeline.h's own overview
    // comment for why); this test now exercises the same underlying
    // shape directly against the current per-edge design, which has no
    // seed to land wrong in the first place.
    //
    // This synthetic ring mirrors that shape: a >180-degree sweep with two
    // raw antimeridian crossings netting zero total winding, same as the
    // real data (not pole-encircling). Center (11 degrees) is far from
    // the ring's own raw wrap point near +-180.
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    const GeoPoint wideRing[] = {
        makeGeoPoint(10.0f, 179.0f),
        makeGeoPoint(10.0f, -179.0f),
        makeGeoPoint(10.0f, -140.0f),
        makeGeoPoint(10.0f, -100.0f),
        makeGeoPoint(10.0f, -60.0f),
        makeGeoPoint(10.0f, -20.0f),
    };
    pushRing(input, wideRing);

    const GeoPoint center = makeGeoPoint(10.0f, 11.0f);
    const Error err = projectRings(workspace, input, center, 1.0f);

    REQUIRE(err == Error::Ok);
    // Two real raw crossings (179 -> -179, and the ring's own closing edge
    // -20 -> 179) split this into two pieces: wideRing[2..5] (the bulk),
    // and wideRing[0..1] (the two points either side of the ring's own
    // raw wrap point).
    REQUIRE(workspace.projectedRingSizes().size() == 2);
    REQUIRE(workspace.projectedRingSizes()[0] == 5); // wideRing[2..5] + 1 boundary exit
    REQUIRE(workspace.projectedRingSizes()[1] == 4); // 1 boundary entry + wideRing[0..1] + 1 boundary exit closing

    // Expected x, converted back to degrees relative to center -- matches
    // each real point's own raw longitude minus center's 11 degrees
    // exactly, confirming every point stays in one consistent,
    // correctly-centered frame (not off by a spurious +-360). Boundary
    // points (exactly +-180) are skipped here -- checked separately below.
    const float expectedPiece0[] = {-151.0f, -111.0f, -71.0f, -31.0f}; // wideRing[2..5]
    for (std::size_t i = 0; i < 4; ++i) {
        const float actualDeg = workspace.projectedPoint(i).x / kEarthRadiusKm * (180.0f / kPi);
        CHECK(approxEqual(actualDeg, expectedPiece0[i], 0.5f));
    }
    const float expectedPiece1[] = {168.0f, 170.0f}; // wideRing[0], wideRing[1]
    for (std::size_t i = 0; i < 2; ++i) {
        const float actualDeg = workspace.projectedPoint(6 + i).x / kEarthRadiusKm * (180.0f / kPi);
        CHECK(approxEqual(actualDeg, expectedPiece1[i], 0.5f));
    }

    // Every boundary point (piece 0's last, piece 1's first and last)
    // lands exactly at +-halfWorldWidth.
    const float halfWorldWidth = kPi * kEarthRadiusKm;
    CHECK(approxEqual(workspace.projectedPoint(4).x, -halfWorldWidth, 1.0f));
    CHECK(approxEqual(workspace.projectedPoint(5).x, halfWorldWidth, 1.0f));
    CHECK(approxEqual(workspace.projectedPoint(8).x, halfWorldWidth, 1.0f));
}

TEST_CASE("Mercator pipeline: a ring straddling the center's own map-edge seam splits into two pieces, one per screen edge")
{
    // Reproduces a second real, live-reported bug at whole-world zoom: a
    // ring whose *raw* data never crosses +-180 at all (so the existing
    // antimeridian-crossing split never triggers) can still straddle a
    // given center's own antipodal meridian -- e.g. the real dataset's own
    // Australia ring (raw longitude 113..154 degrees), reported missing
    // its western or eastern portion (whichever fell past one screen edge)
    // at whole-world zoom for centers whose antipodal meridian falls
    // inside Australia's own span (confirmed for lon=-46.8506 and
    // lon=-47.3739, half-width=20015km, both centered at lat=17.2532).
    //
    // This synthetic ring mirrors that shape: a plain span whose raw data
    // never crosses +-180 at all, with @p clipLonRad set to kPi (a
    // whole-world view, same as the real report) so the piece's own extent
    // genuinely straddles the seam.
    static Workspace<64, 8> workspace;
    static InputGeometry<64, 8> input;

    const GeoPoint straddlingRing[] = {
        makeGeoPoint(0.0f, 100.0f),
        makeGeoPoint(5.0f, 110.0f),
        makeGeoPoint(10.0f, 130.0f),
        makeGeoPoint(5.0f, 140.0f),
        makeGeoPoint(-5.0f, 135.0f),
        makeGeoPoint(-10.0f, 115.0f),
    };
    pushRing(input, straddlingRing);

    // Every consecutive raw longitude delta above (10, 20, 10, -5, -20, and
    // -15 for the closing edge) stays well under 180 degrees, so this
    // ring's raw data never crosses +-180 at all (mirrors Australia's own
    // real data). Center's own antipodal meridian (-60 + 180 = 120
    // degrees) falls right in the middle of this ring's 100..140 degree
    // span, so it genuinely straddles this center's own map-edge seam.
    const GeoPoint center = makeGeoPoint(0.0f, -60.0f);
    const Error err = projectRings(workspace, input, center, 1.0f, kPi, kPi);

    REQUIRE(err == Error::Ok);
    // Split into (at least) two pieces -- one per screen edge -- instead
    // of one piece silently anchored to a single side.
    REQUIRE(workspace.projectedRingSizes().size() >= 2);

    // No point should land more than one full turn (plus this ring's own
    // modest extent) from center -- the failure mode this guards against
    // put an entire piece a spurious extra +-360 degrees away.
    const std::size_t totalPoints = [&] {
        std::size_t n = 0;
        for (std::size_t r = 0; r < workspace.projectedRingSizes().size(); ++r) {
            n += workspace.projectedRingSizes()[r];
        }
        return n;
    }();
    for (std::size_t i = 0; i < totalPoints; ++i) {
        const float xDeg = workspace.projectedPoint(i).x / kEarthRadiusKm * (180.0f / kPi);
        CHECK(std::fabs(xDeg) < 200.0f);
    }
}

TEST_CASE("Mercator: default clip window matches the unconditional (no-clip) behavior")
{
    static Workspace<64, 8> workspaceDefault;
    static Workspace<64, 8> workspaceExplicit;
    static InputGeometry<64, 8> input;

    const GeoPoint square[] = {
        makeGeoPoint(10.0f, 10.0f),
        makeGeoPoint(10.0f, 20.0f),
        makeGeoPoint(20.0f, 20.0f),
        makeGeoPoint(20.0f, 10.0f),
    };
    pushRing(input, square);

    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const Error errDefault = projectRings(workspaceDefault, input, center, 1.0f);
    const Error errExplicit = projectRings(workspaceExplicit, input, center, 1.0f, kPi, kPi);

    REQUIRE(errDefault == Error::Ok);
    REQUIRE(errExplicit == Error::Ok);
    REQUIRE(workspaceDefault.projectedRingSizes().size() == workspaceExplicit.projectedRingSizes().size());
    REQUIRE(workspaceDefault.projectedRingSizes().size() == 1);
    CHECK(workspaceDefault.projectedRingSizes()[0] == 4);
}

TEST_CASE("Mercator: projectLines culls an out-of-window run and keeps a partially-visible one in full")
{
    {
        static Workspace<64, 8> workspace;
        static InputGeometry<64, 8> input;
        const GeoPoint line[] = {
            makeGeoPoint(10.0f, 10.0f),
            makeGeoPoint(10.0f, 20.0f),
        };
        pushRing(input, line);
        const GeoPoint farCenter = makeGeoPoint(-80.0f, 0.0f);
        const Error err = projectLines(workspace, input, farCenter, 1.0f, 0.05f, 0.05f);
        REQUIRE(err == Error::Ok);
        CHECK(workspace.projectedRingSizes().size() == 0);
    }
    {
        static Workspace<64, 8> workspace;
        static InputGeometry<64, 8> input;
        const GeoPoint line[] = {
            makeGeoPoint(5.0f, 10.0f),
            makeGeoPoint(25.0f, 10.0f),
        };
        pushRing(input, line);
        const GeoPoint center = makeGeoPoint(5.0f, 10.0f); // == line[0]
        const Error err = projectLines(workspace, input, center, 1.0f, 0.05f, 0.05f);
        REQUIRE(err == Error::Ok);
        REQUIRE(workspace.projectedRingSizes().size() == 1);
        CHECK(workspace.projectedRingSizes()[0] == 2);
    }
}

TEST_CASE("generateGraticule rejects a step outside (0, 180)")
{
    static InputGeometry<256, 64> out;
    CHECK(generateGraticule(out, 0.0f) == Error::InvalidParameter);
    CHECK(generateGraticule(out, -10.0f) == Error::InvalidParameter);
    CHECK(generateGraticule(out, 180.0f) == Error::InvalidParameter);
    CHECK(generateGraticule(out, 200.0f) == Error::InvalidParameter);
}

TEST_CASE("generateGraticule produces the expected line count and sizes for a simple step")
{
    // 90-degree step: meridians at -180, -90, 0, 90 (4, not including the
    // duplicate 180/-180 line); one parallel, at 0 degrees (180 / 90 - 1
    // == 1, skipping both poles).
    static InputGeometry<256, 64> out;
    const Error err = generateGraticule(out, 90.0f);
    REQUIRE(err == Error::Ok);
    REQUIRE(out.ringSizes.size() == 5);
    CHECK(out.ringSizes[0] == 2); // meridian
    CHECK(out.ringSizes[1] == 2);
    CHECK(out.ringSizes[2] == 2);
    CHECK(out.ringSizes[3] == 2);
    CHECK(out.ringSizes[4] == 37); // parallel: 36 segments, 37 points
}

TEST_CASE("generateGraticule clears out's own prior content before regenerating")
{
    static InputGeometry<256, 64> out;
    const GeoPoint stale[] = {makeGeoPoint(1.0f, 1.0f), makeGeoPoint(2.0f, 2.0f)};
    pushRing(out, stale);
    REQUIRE(out.ringSizes.size() == 1);

    const Error err = generateGraticule(out, 90.0f);
    REQUIRE(err == Error::Ok);
    CHECK(out.ringSizes.size() == 5); // the stale ring is gone, not appended to
}

TEST_CASE("generateGraticule reports CapacityExceeded instead of silently truncating")
{
    static InputGeometry<4, 1> tooSmall;
    CHECK(generateGraticule(tooSmall, 90.0f) == Error::CapacityExceeded);
}

TEST_CASE("generateGraticule's meridians project to straight vertical lines under Mercator")
{
    static InputGeometry<256, 64> graticule;
    REQUIRE(generateGraticule(graticule, 90.0f) == Error::Ok);

    static Workspace<256, 64> workspace;
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const Error err = projectLines(workspace, graticule, center, 1.0f);
    REQUIRE(err == Error::Ok);
    // 4 meridian pieces, plus 2 more from the one parallel's own single
    // antimeridian crossing (see the parallel-focused test below).
    REQUIRE(workspace.projectedRingSizes().size() == 6);

    // Each of the first 4 pieces is a meridian (2 points) -- both must
    // share the same projected x, the defining property of a straight
    // vertical line.
    const Point *pts = workspace.projectedPoints();
    std::size_t offset = 0;
    for (std::size_t r = 0; r < 4; ++r) {
        const std::size_t size = workspace.projectedRingSizes()[r];
        REQUIRE(size == 2);
        CHECK(approxEqual(pts[offset].x, pts[offset + 1].x, 0.5f));
        offset += size;
    }
}

TEST_CASE("generateGraticule's parallel projects to a straight horizontal line spanning the full map width")
{
    static InputGeometry<256, 64> graticule;
    REQUIRE(generateGraticule(graticule, 90.0f) == Error::Ok);

    static Workspace<256, 64> workspace;
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    const Error err = projectLines(workspace, graticule, center, 1.0f);
    REQUIRE(err == Error::Ok);

    // The one parallel spans the full globe, so it crosses projectLines()'s
    // own map boundary exactly once for this center (generateGraticule()'s
    // own doc comment): 4 meridian pieces plus 2 real pieces from that
    // one crossing, the same way any other antimeridian-crossing line
    // already splits.
    REQUIRE(workspace.projectedRingSizes().size() == 6);

    std::size_t offset = 0;
    for (std::size_t r = 0; r < 4; ++r) {
        offset += workspace.projectedRingSizes()[r];
    }
    const std::size_t firstPieceSize = workspace.projectedRingSizes()[4];
    const std::size_t secondPieceSize = workspace.projectedRingSizes()[5];
    REQUIRE(firstPieceSize + secondPieceSize == 39); // 37 real points + 2 inserted boundary points

    const Point *pts = workspace.projectedPoints();
    const float halfWorldWidth = kPi * kEarthRadiusKm;
    const std::size_t totalParallelPoints = firstPieceSize + secondPieceSize;
    float minX = pts[offset].x;
    float maxX = pts[offset].x;
    for (std::size_t i = 0; i < totalParallelPoints; ++i) {
        // Every point of both pieces together -- a straight horizontal
        // line's defining property, regardless of which piece it landed
        // in.
        CHECK(approxEqual(pts[offset + i].y, pts[offset].y, 0.5f));
        if (pts[offset + i].x < minX) {
            minX = pts[offset + i].x;
        } else if (pts[offset + i].x > maxX) {
            maxX = pts[offset + i].x;
        }
    }
    // Together, both pieces reach both edges of the map -- no missing gap
    // at the crossing.
    CHECK(approxEqual(minX, -halfWorldWidth, 1.0f));
    CHECK(approxEqual(maxX, halfWorldWidth, 1.0f));
}
