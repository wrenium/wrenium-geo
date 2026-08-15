// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "doctest/doctest.h"

#include "wrenium/geo/float_format.h"
#include "wrenium/geo/workspace_sizing.h"

using namespace wrenium::geo;

namespace {

// Mirrors topojson2bin's own generated Info struct shape (pointCount/
// ringCount/maxRingPointCount) -- sharedWorkspaceSizeFor() only reads the
// first two, but a real generated header always has all three.
struct DatasetInfo
{
    std::size_t pointCount;
    std::size_t ringCount;
    std::size_t maxRingPointCount;
};

} // namespace

TEST_CASE("sharedWorkspaceSizeFor takes the larger of each dimension across both datasets")
{
    // Ring dataset: fewer rings, but its default point margin (1000) makes
    // its own point total the bigger one.
    constexpr DatasetInfo kRingInfo{4997, 126, 1319};
    // Line dataset: more rings/runs, but its smaller default point margin
    // (200) keeps its own point total well below the ring dataset's.
    constexpr DatasetInfo kLineInfo{2974, 326, 72};
    constexpr float kMaxViewportPx = 2500.0f;

    constexpr SharedWorkspaceSize sizing = sharedWorkspaceSizeFor(kRingInfo, kLineInfo, kMaxViewportPx);

    CHECK(sizing.maxPoints == 4997 + 1000);
    CHECK(sizing.maxRings == 326 + 50);

    // outputCharCapacity must be enough for whichever pass -- ring or
    // line -- produces the longer text at this maxPoints/maxRings.
    constexpr std::size_t kRingChars = svgOutputCharCapacityForRings(sizing.maxPoints, sizing.maxRings, kMaxViewportPx);
    constexpr std::size_t kLineChars = svgOutputCharCapacityForLines(sizing.maxPoints, sizing.maxRings, kMaxViewportPx);
    CHECK(sizing.outputCharCapacity == (kRingChars > kLineChars ? kRingChars : kLineChars));
}

TEST_CASE("sharedWorkspaceSizeFor honors overridden margins")
{
    constexpr DatasetInfo kRingInfo{100, 10, 20};
    constexpr DatasetInfo kLineInfo{50, 5, 10};
    constexpr float kMaxViewportPx = 1000.0f;

    constexpr SharedWorkspaceSize sizing = sharedWorkspaceSizeFor(kRingInfo, kLineInfo, kMaxViewportPx, 5, 2, 3);

    CHECK(sizing.maxPoints == 100 + 5); // ring: 105 vs line: 50 + 3 = 53
    CHECK(sizing.maxRings == 10 + 2);   // ring: 12 vs line: 5 + 2 = 7
}

TEST_CASE("sharedWorkspaceSizeFor picks up the line dataset's own bigger dimension when it's larger")
{
    constexpr DatasetInfo kRingInfo{10, 5, 8};
    constexpr DatasetInfo kLineInfo{5000, 200, 20};
    constexpr float kMaxViewportPx = 1000.0f;

    constexpr SharedWorkspaceSize sizing = sharedWorkspaceSizeFor(kRingInfo, kLineInfo, kMaxViewportPx);

    CHECK(sizing.maxPoints == 5000 + 200); // line: 5200 vs ring: 10 + 1000 (default) = 1010
    CHECK(sizing.maxRings == 200 + 50);
}
