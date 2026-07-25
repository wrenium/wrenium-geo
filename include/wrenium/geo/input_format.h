// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>
#include <cstdint>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/geo_point.h"

/// @file
/// Pre-projection input geometry binary format -- written by the offline
/// converter tool and read by loadInputGeometry() (pipeline.h).
///
/// A **ring** is a closed sequence of points describing one polygon
/// boundary -- one landmass's coastline, or one island, for example. A
/// dataset with multiple disjoint shapes (separate islands, or a landmass
/// with a lake-shaped hole) is represented as multiple rings, stored back
/// to back in one flat list -- there's no explicit grouping by
/// landmass/feature, and no hole/outer flag marking which rings are holes:
/// overlapping rings combine purely via the even-odd fill rule (SVG's
/// `fill-rule="evenodd"`), the same rule an inner lake ring already
/// renders correctly under without any special hole handling.
///
/// Wire layout (little-endian, always):
///   InputGeometryHeader
///   ring_count x {
///       uint32_t pointCount
///       pointCount x GeoPoint   (latRad, lonRad)
///   }

namespace wrenium::geo {

/// Identifies this library's input-geometry wire format -- loadInputGeometry()
/// checks it before parsing anything else, so mismatched or corrupted data is
/// rejected immediately rather than partially parsed. ASCII "WGM1".
constexpr std::uint32_t kInputGeometryMagic = 0x57474D31;
/// Wire format version for #kInputGeometryMagic -- loadInputGeometry()
/// rejects a stream whose version doesn't match exactly.
constexpr std::uint32_t kInputGeometryVersion = 1;

/// Fixed-size header at the start of the input geometry wire format;
/// followed by `ringCount` entries of `uint32_t pointCount` +
/// `pointCount` x GeoPoint (latRad, lonRad), each as two little-endian
/// floats.
struct InputGeometryHeader
{
    std::uint32_t magic = kInputGeometryMagic;     ///< See #kInputGeometryMagic.
    std::uint32_t version = kInputGeometryVersion; ///< See #kInputGeometryVersion.
    std::uint32_t ringCount = 0;                   ///< Number of rings that follow.
};

/// A loaded, in-memory input dataset -- what loadInputGeometry() fills and
/// projectRings()/projectLines() (pipeline.h) read from. Bundles the point
/// list with its per-ring metadata into one value so they're always passed
/// (and can't accidentally be mismatched) together.
/// @tparam MaxPoints Capacity of #points.
/// @tparam MaxRings Capacity of #ringSizes/#ringMinLat/#ringMaxLat.
template <std::size_t MaxPoints, std::size_t MaxRings>
struct InputGeometry
{
    /// Flattened points, one entry per point across all rings.
    Buffer<GeoPoint, MaxPoints> points;
    /// Each ring's point count, one entry per ring.
    Buffer<std::size_t, MaxRings> ringSizes;
    /// Each ring's minimum latitude (radians), one entry per ring.
    Buffer<float, MaxRings> ringMinLat;
    /// Each ring's maximum latitude (radians), one entry per ring.
    Buffer<float, MaxRings> ringMaxLat;
};

} // namespace wrenium::geo
