// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"

/// @file
/// Workspace's internal typed storage for its point-stage buffer (stageB):
/// clip.h's output starts as sphere-space GeoPoints and, after project()
/// runs, holds planar Points at those exact same slots -- one raw
/// two-float shape reused for both, so the reuse itself costs nothing
/// beyond the 8 bytes either representation alone would need.

namespace wrenium::geo::detail {

/// Storage for one point that's either sphere-space (GeoPoint, read via
/// #geo()) or planar/projected (Point's x/y, read/written directly),
/// never both at once -- see this file's own overview comment for why one
/// slot is reused instead of two. Plain fields rather than a union of
/// GeoPoint/Point: switching a union's active member isn't usable inside
/// a C++17 constant expression, and this same storage backs Workspace's
/// stageB (workspace.h), which project() (azimuthal_pipeline.h,
/// cylindrical_pipeline.h) transforms in place.
struct PointStorage
{
    float x = 0.0f;
    float y = 0.0f;

    constexpr PointStorage() = default;

    /// Seeds this slot from a sphere-space point -- x/y hold latRad/lonRad
    /// until project() overwrites them with planar coordinates instead.
    constexpr explicit PointStorage(const GeoPoint &g)
        : x(g.latRad), y(g.lonRad)
    {
    }

    /// Overwrites this slot with a planar point -- how project() turns a
    /// rotated GeoPoint slot into its final output Point.
    constexpr PointStorage &operator=(const Point &p)
    {
        x = p.x;
        y = p.y;
        return *this;
    }

    /// Reads this slot as a sphere-space point -- valid before project()
    /// has overwritten it with planar coordinates.
    constexpr GeoPoint geo() const
    {
        return GeoPoint{x, y};
    }
};

} // namespace wrenium::geo::detail
