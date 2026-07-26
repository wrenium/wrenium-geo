// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <string>

#include "doctest/doctest.h"

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/svg_emitter.h"

// svg_emitter.h has always been exercised indirectly via
// test_binary_roundtrip.cpp (comparing it against the binary
// encode/decode path), but never tested directly for its own documented
// contract: rings/runs with fewer than 2 points are skipped, output-buffer
// capacity is enforced cleanly, and emitSvgLinePath never emits a
// trailing "Z".

using namespace wrenium::geo;

TEST_CASE("emitSvgPath writes M/L .../Z with the expected token spacing for one ring")
{
    const Point points[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
    };
    const std::size_t ringSizes[] = {3};

    Buffer<char, 256> out;
    REQUIRE(emitSvgPath(points, ringSizes, 1, out) == Error::Ok);

    const std::string text(out.data(), out.size());
    CHECK(text == "M 0.000,0.000 L 1.000,0.000 1.000,1.000 Z ");
}

TEST_CASE("emitSvgPath skips rings with fewer than 2 points but still advances past their points")
{
    const Point points[] = {
        {9.0f, 9.0f}, // a lone point in a 1-point ring -- must be skipped
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
    };
    const std::size_t ringSizes[] = {1, 3};

    Buffer<char, 256> out;
    REQUIRE(emitSvgPath(points, ringSizes, 2, out) == Error::Ok);

    const std::string text(out.data(), out.size());
    // Only the second (3-point) ring should appear; the skipped 1-point
    // ring's coordinate (9.0, 9.0) must not leak into the output.
    CHECK(text == "M 0.000,0.000 L 1.000,0.000 1.000,1.000 Z ");
}

TEST_CASE("emitSvgPath treats a zero-point ring as a no-op, not an out-of-bounds read")
{
    const Point points[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
    };
    const std::size_t ringSizes[] = {0, 3};

    Buffer<char, 256> out;
    REQUIRE(emitSvgPath(points, ringSizes, 2, out) == Error::Ok);

    const std::string text(out.data(), out.size());
    CHECK(text == "M 0.000,0.000 L 1.000,0.000 1.000,1.000 Z ");
}

TEST_CASE("emitSvgPath reports Error::CapacityExceeded cleanly when the output buffer is too small")
{
    const Point points[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
    };
    const std::size_t ringSizes[] = {3};

    Buffer<char, 4> tinyOut; // not even enough for "M 0.0"
    const Error err = emitSvgPath(points, ringSizes, 1, tinyOut);
    CHECK(err == Error::CapacityExceeded);
}

TEST_CASE("emitSvgLinePath writes M/L ... with no trailing Z, across multiple runs")
{
    const Point points[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {5.0f, 5.0f},
        {6.0f, 5.0f},
    };
    const std::size_t runSizes[] = {2, 2};

    Buffer<char, 256> out;
    REQUIRE(emitSvgLinePath(points, runSizes, 2, out) == Error::Ok);

    const std::string text(out.data(), out.size());
    CHECK(text == "M 0.000,0.000 L 1.000,0.000 M 5.000,5.000 L 6.000,5.000 ");
    CHECK(text.find('Z') == std::string::npos);
}

TEST_CASE("emitSvgLinePath writes only one L per run, relying on SVG's implicit command repeat for later points")
{
    const Point points[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {2.0f, 0.0f},
    };
    const std::size_t runSizes[] = {3};

    Buffer<char, 256> out;
    REQUIRE(emitSvgLinePath(points, runSizes, 1, out) == Error::Ok);

    const std::string text(out.data(), out.size());
    CHECK(text == "M 0.000,0.000 L 1.000,0.000 2.000,0.000 ");
}

TEST_CASE("emitSvgLinePath skips a degenerate single-point run")
{
    const Point points[] = {
        {9.0f, 9.0f}, // lone point in a 1-point run -- skipped
        {0.0f, 0.0f},
        {1.0f, 0.0f},
    };
    const std::size_t runSizes[] = {1, 2};

    Buffer<char, 256> out;
    REQUIRE(emitSvgLinePath(points, runSizes, 2, out) == Error::Ok);

    const std::string text(out.data(), out.size());
    CHECK(text == "M 0.000,0.000 L 1.000,0.000 ");
}

TEST_CASE("emitSvgLinePath reports Error::CapacityExceeded cleanly when the output buffer is too small")
{
    const Point points[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
    };
    const std::size_t runSizes[] = {2};

    Buffer<char, 4> tinyOut;
    const Error err = emitSvgLinePath(points, runSizes, 1, tinyOut);
    CHECK(err == Error::CapacityExceeded);
}
