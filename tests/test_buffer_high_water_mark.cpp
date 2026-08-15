// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

// Only built into the separate tests_high_water_mark binary (see
// tests/CMakeLists.txt), compiled with WRENIUM_GEO_TRACK_HIGH_WATER_MARK
// defined -- the main `tests` binary deliberately never defines it, so
// Buffer's default (untracked) shape is what every other test exercises.
// See buffer.h's own comment for why this can't be a per-file #define
// mixed into the main `tests` binary instead (an ODR violation).

#include "doctest/doctest.h"

#include "wrenium/geo/buffer.h"

using namespace wrenium::geo;

TEST_CASE("Buffer::highWaterMark tracks the largest size ever reached, surviving clear()/truncate()")
{
    Buffer<int, 8> buffer;
    CHECK(buffer.highWaterMark() == 0);

    buffer.pushBack(1);
    buffer.pushBack(2);
    buffer.pushBack(3);
    CHECK(buffer.size() == 3);
    CHECK(buffer.highWaterMark() == 3);

    // Shrinking (clear/truncate) must not lower the high-water mark --
    // that's the entire point, distinguishing it from size().
    buffer.truncate(1);
    CHECK(buffer.size() == 1);
    CHECK(buffer.highWaterMark() == 3);

    buffer.clear();
    CHECK(buffer.size() == 0);
    CHECK(buffer.highWaterMark() == 3);

    // Growing again past the previous peak raises it further.
    for (int i = 0; i < 5; ++i) {
        CHECK(buffer.pushBack(i) == Error::Ok);
    }
    CHECK(buffer.size() == 5);
    CHECK(buffer.highWaterMark() == 5);

    // A rejected pushBack (already at capacity) must not move the mark.
    Buffer<int, 2> full;
    full.pushBack(1);
    full.pushBack(2);
    CHECK(full.highWaterMark() == 2);
    CHECK(full.pushBack(3) == Error::CapacityExceeded);
    CHECK(full.highWaterMark() == 2);
}
