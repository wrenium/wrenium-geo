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
constexpr Error appendFixedFloat(Buffer<char, Capacity> &out, float value)
{
    constexpr std::uint32_t kScale = 1000; // 10 ^ kFloatFormatDecimalPlaces

    const bool negative = value < 0.0f;
    const float absValue = negative ? -value : value;

    // Round to the nearest thousandth, expressed as an integer count of
    // thousandths -- avoids ever formatting via repeated float division.
    // absValue is provably non-negative (just above), so add-0.5-then-
    // truncate is exact round-half-up; lround() would add a call in this
    // per-coordinate hot path (this is the SVG/binary emitter's float
    // formatter) for no correctness benefit over the domain this function
    // actually sees.
    // NOLINTNEXTLINE(bugprone-incorrect-roundings)
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
    char digits[10] = {};
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

/// Characters one coordinate needs from appendFixedFloat(), given the
/// largest absolute value it will ever be: sign + integer digits + '.' +
/// #kFloatFormatDecimalPlaces.
///
/// Two things this accounts for that a naive digit-count wouldn't:
/// - The sign byte is always budgeted, even if @p maxAbsValue happens to
///   be reached only by a positive coordinate -- projected output is
///   centered on (0, 0), so real data isn't one-sided.
/// - Rounding: a value that rounds up into an extra digit (e.g.
///   999.9996 -> "1000.000") is accounted for, not just the raw magnitude.
/// @param maxAbsValue The largest |coordinate| that will ever be formatted.
/// @return Characters for one coordinate -- not a full "x,y " point. See
/// #svgOutputCharCapacityForRings/#svgOutputCharCapacityForLines for a
/// ready-to-use Workspace OutputCharCapacity instead of composing this by
/// hand.
constexpr std::size_t svgCharsForMaxCoordinate(float maxAbsValue)
{
    constexpr std::uint32_t kScale = 1000;
    const float absValue = maxAbsValue < 0.0f ? -maxAbsValue : maxAbsValue;
    // Mirrors appendFixedFloat()'s own rounding exactly -- see that
    // function's identical NOLINT for why add-0.5-then-truncate is exact
    // round-half-up here, not an actual rounding bug.
    // NOLINTNEXTLINE(bugprone-incorrect-roundings)
    const std::uint32_t scaledTotal = static_cast<std::uint32_t>(absValue * static_cast<float>(kScale) + 0.5f);
    std::uint32_t integerPart = scaledTotal / kScale;

    std::size_t intDigits = 1; // appendFixedFloat's own digit loop always emits at least one
    while (integerPart >= 10) {
        integerPart /= 10;
        ++intDigits;
    }

    return 1 /* sign */ + intDigits + 1 /* '.' */ + static_cast<std::size_t>(kFloatFormatDecimalPlaces);
}

/// A correctly-sized capacity for the `Buffer<char, N>` passed to
/// emitSvgPath() (svg_emitter.h) alongside a Workspace (workspace.h) for
/// closed-ring output, computed from the shape of what you'll actually
/// draw: @p maxPoints points across @p maxRings rings, no coordinate
/// larger than @p maxAbsCoordinate -- computed from numbers you already
/// know, not guessed.
///
/// Usage: `Buffer<char, svgOutputCharCapacityForRings(6000, 200, 400.0f)> svgPath;`
/// alongside a `Workspace<6000, 200>`.
/// @param maxPoints See Workspace's own MaxPoints.
/// @param maxRings See Workspace's own MaxRings.
/// @param maxAbsCoordinate The largest |x| or |y| your output will ever
/// reach. For azimuthal output going through makeViewport() (viewport.h),
/// that's just viewportRadiusPx -- clipRadiusKm cancels out of that
/// helper's own scale formula, so it doesn't factor in here at all.
// maxPoints/maxRings/maxAbsCoordinate are documented and always passed in
// this order (matching Workspace's own MaxPoints/MaxRings order, plus the
// magnitude bound last) -- reordering one alone would be its own hazard.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
constexpr std::size_t svgOutputCharCapacityForRings(std::size_t maxPoints, std::size_t maxRings, float maxAbsCoordinate)
{
    const std::size_t coordChars = svgCharsForMaxCoordinate(maxAbsCoordinate);
    // Per point: two coordinates + ',' + trailing ' '.
    const std::size_t perPoint = (2 * coordChars) + 2;
    // Per ring: "M " (2, first point only) + " L" (2, first point only) +
    // "Z " (2, ring close) -- see emitSvgPath's own walk.
    constexpr std::size_t kPerRingOverhead = 6;
    return (maxPoints * perPoint) + (maxRings * kPerRingOverhead);
}

/// Same as #svgOutputCharCapacityForRings, for emitSvgLinePath()'s
/// (svg_emitter.h) open-polyline output instead -- never closed with a
/// "Z" (see that function's own comment for why), so its per-run overhead
/// is smaller.
/// @param maxPoints See Workspace's own MaxPoints.
/// @param maxRuns The largest number of independent open runs -- pass
/// Workspace's own MaxRings (ringSizesB holds run sizes here, same slot
/// it holds ring sizes for the closed case).
/// @param maxAbsCoordinate See #svgOutputCharCapacityForRings's identical
/// parameter.
// See svgOutputCharCapacityForRings's identical parameter-order rationale.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
constexpr std::size_t svgOutputCharCapacityForLines(std::size_t maxPoints, std::size_t maxRuns, float maxAbsCoordinate)
{
    const std::size_t coordChars = svgCharsForMaxCoordinate(maxAbsCoordinate);
    const std::size_t perPoint = (2 * coordChars) + 2;
    // Per run: "M " (2) + " L" (2) for the first point only -- no "Z" close.
    constexpr std::size_t kPerRunOverhead = 4;
    return (maxPoints * perPoint) + (maxRuns * kPerRunOverhead);
}

} // namespace wrenium::geo
