// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstdint>
#include <cstring>

// Internal helper (wrenium::geo::detail -- not public API). Reinterprets a float's
// bit pattern as a uint32_t and back via memcpy, the strict-aliasing-safe,
// portable way to do this in C++17 (no std::bit_cast until C++20, and a
// reinterpret_cast through pointers would violate strict aliasing). Used by
// detail/byte_stream.h's readFloatLE()/writeFloatLE(), shared by
// binary_emitter.h and input_format.h's loadInputGeometry() so the
// float<->bits step isn't duplicated between them. Byte order itself is a
// separate concern, handled by plain bit-shift arithmetic in
// detail/byte_stream.h.

namespace wrenium::geo {
namespace detail {

inline std::uint32_t floatToBits(float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float bitsToFloat(std::uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace detail
} // namespace wrenium::geo
