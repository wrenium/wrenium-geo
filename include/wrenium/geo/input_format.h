// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>
#include <cstdint>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/byte_stream.h"
#include "wrenium/geo/error.h"
#include "wrenium/geo/geo_point.h"

/// @file
/// Pre-projection input geometry binary format -- written by the offline
/// converter tool and read by loadInputGeometry(), below. Projection-
/// agnostic: both the azimuthal (azimuthal_pipeline.h) and cylindrical
/// (cylindrical_pipeline.h) pipelines read the InputGeometry this produces.
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
///
/// Sizing InputGeometry<MaxPoints, MaxRings>: it holds your entire
/// checked-in dataset, so its capacity has to fit that dataset exactly --
/// use the point/ring counts `topojson2bin` generates alongside the
/// dataset itself (`<name>Info.pointCount`/`.ringCount`), never a
/// hand-copied number. See workspace.h's own "Sizing a Workspace" comment
/// for the fuller methodology, including how Workspace's own capacity
/// (usually sized together with InputGeometry) differs from this one.

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

template <std::size_t MaxPoints, std::size_t MaxRings>
struct InputGeometry;

template <std::size_t MaxPoints, std::size_t MaxRings>
inline Error loadInputGeometry(const std::uint8_t *data, std::size_t byteCount, InputGeometry<MaxPoints, MaxRings> &geometry);

/// Parses this file's own wire layout (InputGeometryHeader + ring_count x
/// { pointCount, pointCount x GeoPoint }, little-endian) out of a raw byte
/// buffer -- the converter's checked-in .bin file, or its generated .h
/// array compiled directly into the caller's own binary, for example --
/// into @p geometry.
///
/// Also computes each ring's own [minLat, maxLat] into
/// @p geometry's ringMinLat/ringMaxLat, one entry per ring --
/// azimuthal::projectRings() / azimuthal::projectLines()
/// (azimuthal_pipeline.h) need this exact bound for their whole-ring
/// visibility pre-filter on *every* recompute, but the input geometry itself
/// never changes between recomputes (only center/clipRadiusRad do), so it's
/// computed once here rather than rescanned on every call.
/// @param data Raw wire bytes.
/// @param byteCount Number of bytes at @p data.
/// @param geometry Receives the parsed points and per-ring metadata.
/// @return Error::Ok on success; Error::UnrecognizedFormat for a bad
/// magic/version; Error::TruncatedData if the stream ends before a
/// structure it describes is fully present; Error::CapacityExceeded if the
/// data has more points/rings than @p geometry can hold.
template <std::size_t MaxPoints, std::size_t MaxRings>
inline Error loadInputGeometry(const std::uint8_t *data, std::size_t byteCount, InputGeometry<MaxPoints, MaxRings> &geometry)
{
    geometry.points.clear();
    geometry.ringSizes.clear();
    geometry.ringMinLat.clear();
    geometry.ringMaxLat.clear();

    constexpr std::size_t headerSize = sizeof(InputGeometryHeader);
    if (byteCount < headerSize) {
        return Error::TruncatedData;
    }

    std::size_t cursor = 0;
    const std::uint32_t magic = detail::readU32LE(data + cursor);
    cursor += 4;
    const std::uint32_t version = detail::readU32LE(data + cursor);
    cursor += 4;
    const std::uint32_t ringCount = detail::readU32LE(data + cursor);
    cursor += 4;

    if (magic != kInputGeometryMagic || version != kInputGeometryVersion) {
        return Error::UnrecognizedFormat;
    }

    for (std::uint32_t r = 0; r < ringCount; ++r) {
        if (cursor + 4 > byteCount) {
            return Error::TruncatedData;
        }
        const std::uint32_t pointCount = detail::readU32LE(data + cursor);
        cursor += 4;

        Error err = geometry.ringSizes.pushBack(static_cast<std::size_t>(pointCount));
        if (err != Error::Ok) {
            return err;
        }

        float minLat = 0.0f;
        float maxLat = 0.0f;

        for (std::uint32_t p = 0; p < pointCount; ++p) {
            if (cursor + 8 > byteCount) {
                return Error::TruncatedData;
            }
            GeoPoint point;
            point.latRad = detail::readFloatLE(data + cursor);
            cursor += 4;
            point.lonRad = detail::readFloatLE(data + cursor);
            cursor += 4;

            if (p == 0) {
                minLat = point.latRad;
                maxLat = point.latRad;
            } else if (point.latRad < minLat) {
                minLat = point.latRad;
            } else if (point.latRad > maxLat) {
                maxLat = point.latRad;
            }

            err = geometry.points.pushBack(point);
            if (err != Error::Ok) {
                return err;
            }
        }

        err = geometry.ringMinLat.pushBack(minLat);
        if (err != Error::Ok) {
            return err;
        }
        err = geometry.ringMaxLat.pushBack(maxLat);
        if (err != Error::Ok) {
            return err;
        }
    }

    return Error::Ok;
}

/// A loaded, in-memory input dataset -- what loadInputGeometry() fills and
/// azimuthal::projectRings() / azimuthal::projectLines()
/// (azimuthal_pipeline.h) or cylindrical::projectRings() /
/// cylindrical::projectLines() (cylindrical_pipeline.h) read from. Bundles
/// the point list with its per-ring metadata into one value so they're
/// always passed (and can't accidentally be mismatched) together.
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

    /// Parses @p data/@p byteCount (loadInputGeometry(), above) the first
    /// time this succeeds; every call after that returns Error::Ok
    /// immediately without touching this object or re-parsing anything,
    /// including calls with different @p data/@p byteCount -- the whole
    /// point is that the caller's own data never actually changes. The
    /// checked-in coastline/border data is exactly this case: it doesn't
    /// change between recomputes, only center/clipRadiusRad do, so
    /// re-parsing it on every call would be pure waste. A call that fails
    /// isn't remembered as failed: the next call retries the parse from
    /// scratch, same as calling loadInputGeometry() directly would.
    /// @param data Raw wire bytes -- see loadInputGeometry()'s identical parameter.
    /// @param byteCount Number of bytes at @p data.
    /// @return Error::Ok if this object is now loaded (this call or an
    /// earlier one); otherwise whatever loadInputGeometry() itself returned.
    Error ensureLoaded(const std::uint8_t *data, std::size_t byteCount)
    {
        if (m_loaded) {
            return Error::Ok;
        }
        const Error err = loadInputGeometry(data, byteCount, *this);
        m_loaded = (err == Error::Ok);
        return err;
    }

    /// True once ensureLoaded() has succeeded, from this call or an earlier one.
    bool isLoaded() const
    {
        return m_loaded;
    }

private:
    bool m_loaded = false;
};

} // namespace wrenium::geo
