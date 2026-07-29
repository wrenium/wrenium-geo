// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "doctest/doctest.h"

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/error.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"
#include "wrenium/geo/pipeline.h"
#include "wrenium/geo/workspace.h"

using namespace wrenium::geo;

namespace {

// projectRings (pipeline.h) takes each ring's precomputed [minLat, maxLat]
// instead of rescanning every point on every recompute (see its own
// comment) -- these tests build a single ring directly (not via
// loadInputGeometry, which computes this automatically), so this fills in
// the same bound by hand.
template <std::size_t N, std::size_t M>
void pushSingleRingLatBounds(const Buffer<GeoPoint, N> &points, Buffer<float, M> &outMinLat, Buffer<float, M> &outMaxLat) // NOLINT(bugprone-easily-swappable-parameters)
{
    float minLat = points[0].latRad;
    float maxLat = minLat;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (points[i].latRad < minLat) {
            minLat = points[i].latRad;
        }
        if (points[i].latRad > maxLat) {
            maxLat = points[i].latRad;
        }
    }
    outMinLat.pushBack(minLat);
    outMaxLat.pushBack(maxLat);
}

} // namespace

TEST_CASE("Buffer::pushBack succeeds up to capacity and fails cleanly past it")
{
    Buffer<int, 3> buf;
    CHECK(buf.pushBack(1) == Error::Ok);
    CHECK(buf.pushBack(2) == Error::Ok);
    CHECK(buf.pushBack(3) == Error::Ok);
    CHECK(buf.size() == 3);

    // One past capacity: must fail cleanly (returned Error, no
    // crash/assert/UB) and must not silently grow or corrupt what's there.
    CHECK(buf.pushBack(4) == Error::CapacityExceeded);
    CHECK(buf.size() == 3);
    CHECK(buf[0] == 1);
    CHECK(buf[1] == 2);
    CHECK(buf[2] == 3);

    // Still cleanly rejects repeated overflow attempts.
    CHECK(buf.pushBack(5) == Error::CapacityExceeded);
    CHECK(buf.size() == 3);
}

TEST_CASE("Buffer::clear/truncate reset size safely")
{
    Buffer<int, 4> buf;
    buf.pushBack(10);
    buf.pushBack(20);
    buf.clear();
    CHECK(buf.size() == 0);
    CHECK(buf.pushBack(30) == Error::Ok);
    CHECK(buf.size() == 1);

    buf.pushBack(40);
    buf.pushBack(50);
    CHECK(buf.size() == 3);

    buf.truncate(1);
    CHECK(buf.size() == 1);
    CHECK(buf[0] == 30);

    // truncate() to a size >= current size is a no-op, never grows.
    buf.truncate(100);
    CHECK(buf.size() == 1);
}

TEST_CASE("A Workspace's point capacity overflowing via the pipeline reports Error::CapacityExceeded cleanly")
{
    // A deliberately tiny workspace: room for only 4 points, 4 rings. Ring-
    // point-cache capacity explicitly widened to 8 (the third template
    // parameter, MaxRingPoints -- defaults to MaxPoints otherwise) so this
    // test actually reaches stageB's own overflow inside clipRingToSink,
    // rather than being short-circuited earlier by projectRings's own
    // ringSize > MaxRingPoints guard (a *different* capacity check, exercised
    // by its own dedicated test below).
    Workspace<4, 4, 8> workspace;

    // 8 input points -- twice what the workspace can hold -- in one ring.
    // The input's own capacity is independent of the workspace's (see
    // pipeline.h's projectRings comment), so this is legal to construct
    // and lets the test reach the workspace's real limit as detail/azimuthal/clip.h pushes
    // surviving (rotated) points into stageB.
    InputGeometry<8, 4> input;

    for (int i = 0; i < 8; ++i) {
        GeoPoint p;
        p.latRad = 0.01f * static_cast<float>(i);
        p.lonRad = 0.02f * static_cast<float>(i);
        REQUIRE(input.points.pushBack(p) == Error::Ok);
    }
    REQUIRE(input.ringSizes.pushBack(8) == Error::Ok);

    pushSingleRingLatBounds(input.points, input.ringMinLat, input.ringMaxLat);

    GeoPoint center{0.0f, 0.0f};
    const Error err = projectRings(workspace, input, center, 1.0f, 1.0f);

    CHECK(err == Error::CapacityExceeded);
    // The workspace itself must not have been left in a corrupted
    // over-full state -- stageB's size is capped at its real capacity.
    CHECK(workspace.stageB.size() <= workspace.stageB.capacity());
}

TEST_CASE("A Workspace's ring capacity overflowing reports Error::CapacityExceeded cleanly")
{
    // Room for plenty of points, but only 2 rings.
    Workspace<32, 2> workspace;

    InputGeometry<32, 8> input; // 3 rings -- more than the workspace's MaxRings=2

    for (int ring = 0; ring < 3; ++ring) {
        float minLat = 0.0f;
        float maxLat = 0.0f;
        for (int i = 0; i < 3; ++i) {
            GeoPoint p;
            p.latRad = 0.01f * static_cast<float>(ring * 3 + i);
            p.lonRad = 0.02f * static_cast<float>(ring * 3 + i);
            if (i == 0) {
                minLat = p.latRad;
                maxLat = p.latRad;
            } else if (p.latRad < minLat) {
                minLat = p.latRad;
            } else if (p.latRad > maxLat) {
                maxLat = p.latRad;
            }
            REQUIRE(input.points.pushBack(p) == Error::Ok);
        }
        REQUIRE(input.ringSizes.pushBack(3) == Error::Ok);
        REQUIRE(input.ringMinLat.pushBack(minLat) == Error::Ok);
        REQUIRE(input.ringMaxLat.pushBack(maxLat) == Error::Ok);
    }

    GeoPoint center{0.0f, 0.0f};
    const Error err = projectRings(workspace, input, center, 1.0f, 1.0f);

    CHECK(err == Error::CapacityExceeded);
    CHECK(workspace.ringSizesB.size() <= workspace.ringSizesB.capacity());
}

TEST_CASE("A ring bigger than Workspace's MaxRingPoints reports Error::CapacityExceeded cleanly")
{
    // MaxRingPoints (the third template parameter) explicitly narrowed to
    // 4 -- smaller than the 6-point ring below -- even though MaxPoints (32)
    // and MaxRings (4) both have plenty of room. This exercises
    // projectRings's own ringRotatedCache bounds check specifically (detail/azimuthal/clip.h's
    // rotated-point cache used to eliminate double-rotating kept points),
    // independent of the stageB/ringSizesB overflow checks the two tests
    // above already cover.
    Workspace<32, 4, 4> workspace;

    InputGeometry<8, 4> input;

    for (int i = 0; i < 6; ++i) {
        GeoPoint p;
        p.latRad = 0.01f * static_cast<float>(i);
        p.lonRad = 0.02f * static_cast<float>(i);
        REQUIRE(input.points.pushBack(p) == Error::Ok);
    }
    REQUIRE(input.ringSizes.pushBack(6) == Error::Ok);

    pushSingleRingLatBounds(input.points, input.ringMinLat, input.ringMaxLat);

    GeoPoint center{0.0f, 0.0f};
    const Error err = projectRings(workspace, input, center, 1.0f, 1.0f);

    CHECK(err == Error::CapacityExceeded);
}
