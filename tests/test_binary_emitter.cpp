// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "doctest/doctest.h"

#include "wrenium/geo/binary_emitter.h"
#include "wrenium/geo/binary_format.h"
#include "wrenium/geo/buffer.h"
#include "wrenium/geo/point.h"

using namespace wrenium::geo;

TEST_CASE("binary emitter reports Error::CapacityExceeded cleanly when the output buffer is too small")
{
    const Point points[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
    };
    const std::size_t ringSizes[] = {3};

    // 12-byte header alone leaves no room for any of the payload floats.
    Buffer<std::uint8_t, 12> tinyOut;
    const Error err = BinaryPathEmitter<>::encode(points, ringSizes, 1, tinyOut);
    CHECK(err == Error::CapacityExceeded);
}
