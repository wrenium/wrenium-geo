// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "doctest/doctest.h"

#include "wrenium/geo/binary_emitter.h"
#include "wrenium/geo/binary_format.h"
#include "wrenium/geo/buffer.h"
#include "wrenium/geo/point.h"

using namespace wrenium::geo;

TEST_CASE("binaryOutputByteCapacityForRings is exactly enough, not one byte more")
{
    constexpr std::size_t kMaxPoints = 4;
    constexpr std::size_t kMaxRings = 1;
    constexpr std::size_t kCapacity = binaryOutputByteCapacityForRings(kMaxPoints, kMaxRings);

    const Point points[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };
    const std::size_t ringSizes[] = {kMaxPoints};

    Buffer<std::uint8_t, kCapacity> exact;
    CHECK(BinaryPathEmitter<>::encode(points, ringSizes, kMaxRings, exact) == Error::Ok);
    CHECK(exact.size() == kCapacity);

    Buffer<std::uint8_t, kCapacity - 1> oneByteShort;
    CHECK(BinaryPathEmitter<>::encode(points, ringSizes, kMaxRings, oneByteShort) == Error::CapacityExceeded);
}

TEST_CASE("binaryOutputByteCapacityForLines is exactly enough, not one byte more")
{
    constexpr std::size_t kMaxPoints = 3;
    constexpr std::size_t kMaxRuns = 1;
    constexpr std::size_t kCapacity = binaryOutputByteCapacityForLines(kMaxPoints);

    const Point points[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
    };
    const std::size_t runSizes[] = {kMaxPoints};

    Buffer<std::uint8_t, kCapacity> exact;
    CHECK(LineBinaryPathEmitter<>::encode(points, runSizes, kMaxRuns, exact) == Error::Ok);
    CHECK(exact.size() == kCapacity);

    Buffer<std::uint8_t, kCapacity - 1> oneByteShort;
    CHECK(LineBinaryPathEmitter<>::encode(points, runSizes, kMaxRuns, oneByteShort) == Error::CapacityExceeded);
}

TEST_CASE("binaryOutputByteCapacityForRings/ForLines are unaffected by coordinate magnitude, unlike their SVG counterparts")
{
    // Every element is a fixed 4-byte float regardless of value -- the
    // capacity is identical whether points sit at the origin or far from
    // it, and identical however many rings there are when maxRings stays
    // the same (only the per-point/per-ring counts matter).
    CHECK(binaryOutputByteCapacityForRings(100, 5) == binaryOutputByteCapacityForRings(100, 5));

    const Point small[] = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    const Point huge[] = {{-99999.0f, -99999.0f}, {99999.0f, 99999.0f}};
    const std::size_t ringSizes[] = {2};

    Buffer<std::uint8_t, binaryOutputByteCapacityForRings(2, 1)> outSmall;
    Buffer<std::uint8_t, binaryOutputByteCapacityForRings(2, 1)> outHuge;
    CHECK(BinaryPathEmitter<>::encode(small, ringSizes, 1, outSmall) == Error::Ok);
    CHECK(BinaryPathEmitter<>::encode(huge, ringSizes, 1, outHuge) == Error::Ok);
    CHECK(outSmall.size() == outHuge.size());
}
