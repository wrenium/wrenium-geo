// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>
#include <cstdint>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/error.h"

/// @file
/// A fast, fixed-precision float -> ASCII formatter for path coordinates.
/// Deliberately not sprintf/%f -- naive float formatting is a likely
/// bigger runtime cost than the projection math itself on a constrained
/// target. Used by svg_emitter.h, and available here for any other
/// consumer producing its own text output from projected coordinates
/// (e.g. a hand-written binary-to-SVG decoder).

namespace wrenium::geo {

/// Decimal places appendFixedFloat() always writes -- sub-pixel precision
/// at any realistic display size.
constexpr int kFloatFormatDecimalPlaces = 3;

/// @cond WRENIUM_GEO_INTERNAL
namespace detail {

// The fraction part is always exactly [0, 999], so every possible
// 3-digit-ASCII output is precomputed once at compile time and looked up
// by index, instead of computed by repeated division on every call. Baked
// directly into the binary's read-only data -- no runtime init cost.
struct FractionDigitTable
{
    char digits[1000][kFloatFormatDecimalPlaces];
};

constexpr FractionDigitTable makeFractionDigitTable()
{
    FractionDigitTable table{};
    for (int value = 0; value < 1000; ++value) {
        table.digits[value][0] = static_cast<char>('0' + (value / 100) % 10);
        table.digits[value][1] = static_cast<char>('0' + (value / 10) % 10);
        table.digits[value][2] = static_cast<char>('0' + value % 10);
    }
    return table;
}

constexpr FractionDigitTable kFractionDigitTable = makeFractionDigitTable();

} // namespace detail
/// @endcond

/// Appends @p value to @p out as fixed-precision ASCII text (see
/// #kFloatFormatDecimalPlaces), e.g. `-12.500`.
/// @param out Buffer to append to; existing content is left untouched.
/// @param value The value to format.
/// @return Error::Ok on success, or Error::CapacityExceeded if @p out ran
/// out of room partway through.
template <std::size_t Capacity>
inline Error appendFixedFloat(Buffer<char, Capacity> &out, float value)
{
    constexpr std::uint32_t kScale = 1000; // 10 ^ kFloatFormatDecimalPlaces

    const bool negative = value < 0.0f;
    const float absValue = negative ? -value : value;

    // Round to the nearest thousandth, expressed as an integer count of
    // thousandths -- avoids ever formatting via repeated float division.
    const std::uint32_t scaledTotal = static_cast<std::uint32_t>(absValue * static_cast<float>(kScale) + 0.5f);

    const std::uint32_t integerPart = scaledTotal / kScale;
    const std::uint32_t fractionPart = scaledTotal % kScale;

    Error err = Error::Ok;

    if (negative && scaledTotal != 0) {
        err = out.pushBack('-');
        if (err != Error::Ok) {
            return err;
        }
    }

    // Integer-part digits, most-significant first. 10 digits is enough for
    // any std::uint32_t value.
    char digits[10];
    int digitCount = 0;
    std::uint32_t remaining = integerPart;
    do {
        digits[digitCount] = static_cast<char>('0' + (remaining % 10));
        ++digitCount;
        remaining /= 10;
    } while (remaining != 0 && digitCount < 10);

    for (int i = digitCount - 1; i >= 0; --i) {
        err = out.pushBack(digits[i]);
        if (err != Error::Ok) {
            return err;
        }
    }

    err = out.pushBack('.');
    if (err != Error::Ok) {
        return err;
    }

    // Fraction-part digits, zero-padded to kFloatFormatDecimalPlaces --
    // one table lookup instead of 3 divisions (fractionPart is always
    // < kScale == 1000, so it's always a valid index).
    const char *fractionDigits = detail::kFractionDigitTable.digits[fractionPart];
    for (int i = 0; i < kFloatFormatDecimalPlaces; ++i) {
        err = out.pushBack(fractionDigits[i]);
        if (err != Error::Ok) {
            return err;
        }
    }

    return Error::Ok;
}

} // namespace wrenium::geo
