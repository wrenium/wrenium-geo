// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "wrenium/geo/projection.h"

namespace wrenium::geo {

/// A view's clip radius and output scale, as computed by makeViewport().
/// Pass `clipRadiusRad`/`scale` directly to azimuthal::projectRings() /
/// azimuthal::projectLines() / azimuthal::projectPoint()
/// (azimuthal_pipeline.h) -- the cylindrical family has no clip-radius
/// concept (cylindrical::projectRings()/projectLines() take clipLatRad/
/// clipLonRad instead, cylindrical_pipeline.h).
struct Viewport
{
    float clipRadiusRad; ///< Pass directly as clipRadiusRad.
    float scale;         ///< Pass directly as scale.
};

/// Builds a Viewport from how a caller naturally describes their view: how
/// far out to show it, and how large to draw that on screen.
/// @param clipRadiusKm How far from center to include, in kilometers.
/// @param viewportRadiusPx That same radius, in output units (e.g. pixels).
/// @return The equivalent clipRadiusRad/scale pair.
inline Viewport makeViewport(float clipRadiusKm, float viewportRadiusPx)
{
    Viewport viewport;
    viewport.clipRadiusRad = clipRadiusKm / kEarthRadiusKm;
    viewport.scale = viewportRadiusPx / clipRadiusKm;
    return viewport;
}

} // namespace wrenium::geo
