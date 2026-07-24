// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#ifndef WRENIUM_GEO_DETAIL_POINT_STORAGE_H
#define WRENIUM_GEO_DETAIL_POINT_STORAGE_H

#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"

/// @file
/// Workspace's internal typed storage for its point-stage buffer (stageB):
/// clip.h's output starts as sphere-space GeoPoints and, after project()
/// runs, holds planar Points at those exact same slots. GeoPoint and Point
/// must never be conflated at the type level (see geo_point.h/point.h)
/// even though they happen to share the same in-memory shape (two
/// floats), so the element type is the union below, whose read/write side
/// (.geo vs .point) makes which interpretation is live explicit at each
/// point in the pipeline, rather than an implicit reinterpret_cast. This
/// is well-defined, portable C++ (GeoPoint and Point are both
/// standard-layout structs sharing a common initial sequence of two
/// floats) and keeps project() a true in-place transform over stageB's
/// storage.

namespace wrenium::geo {

/// Storage for one point that's either sphere-space (GeoPoint, via #geo)
/// or planar/projected (Point, via #point), never both at once -- see
/// this file's own overview comment for why a union is used here.
union PointStorage
{
    GeoPoint geo;
    Point point;

    PointStorage()
        : geo()
    {
    }

    explicit PointStorage(const GeoPoint &g)
        : geo(g)
    {
    }
};

} // namespace wrenium::geo

#endif // WRENIUM_GEO_DETAIL_POINT_STORAGE_H
