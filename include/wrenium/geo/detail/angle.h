// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "wrenium/geo/projection.h"

/// @file
/// Plain angle arithmetic shared library-wide -- not tied to any one
/// projection family (unlike detail/azimuthal/, this has no rotation-frame
/// or clip-test meaning at all).

namespace wrenium::geo::detail {

/// Wraps @p value to within `(-kPi, kPi]`. Used wherever a plain
/// longitude/bearing delta needs normalizing -- azimuthal's clip test
/// (detail/azimuthal/clip.h) and cylindrical's antimeridian handling
/// (detail/cylindrical/) both need the same operation, so it lives here
/// rather than under either family's own `detail`.
inline float wrapPi(float value)
{
    while (value > kPi) {
        value -= 2.0f * kPi;
    }
    while (value <= -kPi) {
        value += 2.0f * kPi;
    }
    return value;
}

} // namespace wrenium::geo::detail
