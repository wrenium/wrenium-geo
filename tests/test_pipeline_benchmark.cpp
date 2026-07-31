// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <chrono>
#include <cstdio>

#include "doctest/doctest.h"

#include "wrenium/geo/azimuthal_pipeline.h"
#include "wrenium/geo/binary_emitter.h"
#include "wrenium/geo/buffer.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"
#include "wrenium/geo/svg_emitter.h"
#include "wrenium/geo/workspace.h"

using namespace wrenium::geo;
using namespace wrenium::geo::azimuthal;

// Automated host-side timing benchmark: this can't validate real embedded-
// target cycle counts -- that needs real hardware -- but it catches gross
// algorithmic regressions (e.g. an
// accidental O(n^2) creeping into the clip stage) early, on every test run,
// on host. The pass/fail threshold below is intentionally generous (two
// orders of magnitude above what a healthy O(n) implementation needs on
// typical host hardware) so it doesn't flake on slow/loaded CI machines --
// its job is to catch a structural regression, not to be a tight
// performance gate.

namespace {

constexpr std::size_t kBenchmarkPointCount = 5000;
constexpr std::size_t kBenchmarkRingCount = 50; // 100 points per ring

} // namespace

TEST_CASE("full pipeline over a few thousand points stays well clear of an O(n^2) regression")
{
    static Workspace<kBenchmarkPointCount, kBenchmarkRingCount> workspace;
    static InputGeometry<kBenchmarkPointCount, kBenchmarkRingCount> input;

    input.points.clear();
    input.ringSizes.clear();
    input.ringMinLat.clear();
    input.ringMaxLat.clear();

    const std::size_t pointsPerRing = kBenchmarkPointCount / kBenchmarkRingCount;
    for (std::size_t ring = 0; ring < kBenchmarkRingCount; ++ring) {
        // Every point in a ring shares the same latRad here (only lonRad
        // varies), so the ring's [minLat, maxLat] is just that one value
        // twice -- still going through loadInputGeometry's real per-ring
        // bound computation would touch real wire-format bytes this
        // synthetic benchmark doesn't have, so this fills in the same
        // precomputed-bound contract projectRings now expects directly.
        const float ringLat = -1.4f + 2.8f * (static_cast<float>(ring) / static_cast<float>(kBenchmarkRingCount));
        for (std::size_t i = 0; i < pointsPerRing; ++i) {
            // A dense spread of points covering a good fraction of the
            // sphere, so both the "inside" and "outside" branches of the
            // clip stage get real exercise, not just one code path.
            GeoPoint p;
            p.latRad = ringLat;
            p.lonRad = -3.0f + 6.0f * (static_cast<float>(i) / static_cast<float>(pointsPerRing));
            REQUIRE(input.points.pushBack(p) == Error::Ok);
        }
        REQUIRE(input.ringSizes.pushBack(pointsPerRing) == Error::Ok);
        REQUIRE(input.ringMinLat.pushBack(ringLat) == Error::Ok);
        REQUIRE(input.ringMaxLat.pushBack(ringLat) == Error::Ok);
    }

    GeoPoint center{0.1f, 0.2f};
    const float clipRadiusRad = 1.0f; // wide enough that plenty survives clipping

    const auto start = std::chrono::steady_clock::now();
    const Error err = projectRings(workspace, input, center, clipRadiusRad, 1.0f);
    const auto afterPipeline = std::chrono::steady_clock::now();

    REQUIRE(err == Error::Ok);

    static Buffer<char, kBenchmarkPointCount * 32> svgOut;
    const Error svgErr = emitSvgPath(workspace.projectedPoints(), workspace.projectedRingSizes().data(), workspace.projectedRingSizes().size(), svgOut);
    const auto afterSvg = std::chrono::steady_clock::now();
    REQUIRE(svgErr == Error::Ok);

    const auto pipelineMs = std::chrono::duration<double, std::milli>(afterPipeline - start).count();
    const auto svgMs = std::chrono::duration<double, std::milli>(afterSvg - afterPipeline).count();

    std::printf("[benchmark] rotate+clip+project over %zu points: %.3f ms; SVG emit: %.3f ms\n",
        kBenchmarkPointCount, pipelineMs, svgMs);

    // Generous ceilings -- a back-of-envelope (unvalidated) MCU estimate
    // was 6-15 ms for this point count on a ~480 MHz Cortex-M7; host
    // hardware here is dramatically faster, so anything
    // past 500 ms for either stage indicates an algorithmic regression
    // (e.g. O(n^2)), not just "a slower machine."
    CHECK(pipelineMs < 500.0);
    CHECK(svgMs < 500.0);
}
