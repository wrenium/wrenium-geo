// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "doctest/doctest.h"

#include "wrenium/geo/float_format.h"
#include "wrenium/geo/point.h"
#include "wrenium/geo/svg_emitter.h"

using namespace wrenium::geo;

TEST_CASE("svgCharsForMaxCoordinate matches appendFixedFloat's actual output length")
{
    // "0.000", plus 1 always-budgeted sign byte: 1 (sign) + 1 (integer
    // digit) + 1 ('.') + 3 (fraction) = 6.
    CHECK(svgCharsForMaxCoordinate(0.0f) == 6);
    // "9.999" -- still 1 integer digit.
    CHECK(svgCharsForMaxCoordinate(9.999f) == 6);
    // Rounds up into a second integer digit: appendFixedFloat itself
    // would emit "10.000" here, not "9.9995" truncated to one digit.
    CHECK(svgCharsForMaxCoordinate(9.9996f) == 7);
    CHECK(svgCharsForMaxCoordinate(99.0f) == 7);
    CHECK(svgCharsForMaxCoordinate(999.0f) == 8);
    CHECK(svgCharsForMaxCoordinate(9999.0f) == 9);
    // Sign always budgeted even for a positive input -- same result either sign.
    CHECK(svgCharsForMaxCoordinate(-9999.0f) == 9);
}

TEST_CASE("svgOutputCharCapacityForRings is exactly enough for its own stated worst case, not one byte more")
{
    constexpr float kMaxCoordinate = 400.0f;
    constexpr std::size_t kMaxPoints = 4;
    constexpr std::size_t kMaxRings = 1;
    constexpr std::size_t kCapacity = svgOutputCharCapacityForRings(kMaxPoints, kMaxRings, kMaxCoordinate);

    // Every coordinate negative, at the exact stated magnitude: the
    // formula budgets a sign byte per coordinate regardless (see
    // svgCharsForMaxCoordinate's own comment), so only an all-negative
    // worst case actually uses every budgeted byte -- a mixed-sign case
    // would emit shorter text than the formula assumes and wouldn't test
    // tightness at all.
    const Point points[] = {
        {-kMaxCoordinate, -kMaxCoordinate},
        {-kMaxCoordinate, -kMaxCoordinate},
        {-kMaxCoordinate, -kMaxCoordinate},
        {-kMaxCoordinate, -kMaxCoordinate},
    };
    const std::size_t ringSizes[] = {kMaxPoints};

    Buffer<char, kCapacity> exact;
    CHECK(emitSvgPath(points, ringSizes, kMaxRings, exact) == Error::Ok);
    CHECK(exact.size() == kCapacity);

    Buffer<char, kCapacity - 1> oneByteShort;
    CHECK(emitSvgPath(points, ringSizes, kMaxRings, oneByteShort) == Error::CapacityExceeded);
}

TEST_CASE("svgOutputCharCapacityForLines is exactly enough for its own stated worst case, not one byte more")
{
    constexpr float kMaxCoordinate = 12345.0f;
    constexpr std::size_t kMaxPoints = 3;
    constexpr std::size_t kMaxRuns = 1;
    constexpr std::size_t kCapacity = svgOutputCharCapacityForLines(kMaxPoints, kMaxRuns, kMaxCoordinate);

    // See svgOutputCharCapacityForRings's identical test for why every
    // coordinate must be negative to actually exercise the full budget.
    const Point points[] = {
        {-kMaxCoordinate, -kMaxCoordinate},
        {-kMaxCoordinate, -kMaxCoordinate},
        {-kMaxCoordinate, -kMaxCoordinate},
    };
    const std::size_t runSizes[] = {kMaxPoints};

    Buffer<char, kCapacity> exact;
    CHECK(emitSvgLinePath(points, runSizes, kMaxRuns, exact) == Error::Ok);
    CHECK(exact.size() == kCapacity);

    Buffer<char, kCapacity - 1> oneByteShort;
    CHECK(emitSvgLinePath(points, runSizes, kMaxRuns, oneByteShort) == Error::CapacityExceeded);
}
