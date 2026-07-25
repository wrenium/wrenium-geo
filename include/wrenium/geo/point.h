// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

namespace wrenium::geo {

/// A point in the projected, planar output space: screen/SVG axis
/// convention (x increases right, y increases downward), north-up, in
/// arbitrary output units (such as pixels) set by the `scale` parameter
/// passed to projectRings()/projectLines() (pipeline.h). (0, 0) is the
/// projection center itself, not a viewport corner -- to place the result
/// in a GUI, translate every point by your own viewport's center. Distinct
/// from GeoPoint (sphere-space, radians) so the two are never accidentally
/// interchanged -- see geo_point.h.
struct Point
{
    float x = 0.0f;
    float y = 0.0f;
};

} // namespace wrenium::geo
