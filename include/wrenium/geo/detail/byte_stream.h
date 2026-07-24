// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#ifndef WRENIUM_GEO_DETAIL_BYTE_STREAM_H
#define WRENIUM_GEO_DETAIL_BYTE_STREAM_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/bit_cast.h"
#include "wrenium/geo/error.h"

// Internal helpers (wrenium::geo::detail) for reading/writing the
// little-endian wire formats (input_format.h, binary_format.h). Assumes a
// little-endian host -- true for every real target (desktop x86/ARM64, MCU
// Cortex-M) -- so this is a plain memcpy, not endian-agnostic bit shifting.

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "wrenium-geo's binary wire format assumes a little-endian host");
#endif

namespace wrenium::geo {
namespace detail {

template <std::size_t Capacity>
inline Error writeU32LE(Buffer<std::uint8_t, Capacity> &out, std::uint32_t value)
{
    std::uint8_t bytes[4];
    std::memcpy(bytes, &value, sizeof(bytes));
    for (std::uint8_t b : bytes) {
        const Error err = out.pushBack(b);
        if (err != Error::Ok) {
            return err;
        }
    }
    return Error::Ok;
}

template <std::size_t Capacity>
inline Error writeFloatLE(Buffer<std::uint8_t, Capacity> &out, float value)
{
    return writeU32LE(out, floatToBits(value));
}

inline std::uint32_t readU32LE(const std::uint8_t *bytes)
{
    std::uint32_t value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

inline float readFloatLE(const std::uint8_t *bytes)
{
    return bitsToFloat(readU32LE(bytes));
}

} // namespace detail
} // namespace wrenium::geo

#endif // WRENIUM_GEO_DETAIL_BYTE_STREAM_H
