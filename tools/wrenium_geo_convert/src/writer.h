// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "topojson_decoder.h"

// Serializes decoded rings into the exact byte layout defined by
// include/wrenium/geo/input_format.h (wrenium::geo::InputGeometryHeader + flat ring
// list), then writes that same byte buffer out as either a raw .bin file
// or a generated C++ header -- both derived from one in-memory encode, so
// the geometry is never decoded/encoded twice.

namespace wrenium_geo_convert {

// Packs rings into the wire format from include/wrenium/geo/input_format.h:
//   InputGeometryHeader (magic, version, ringCount)
//   ringCount x { uint32_t pointCount; pointCount x GeoPoint (latRad, lonRad) }
// Always little-endian, regardless of host byte order.
std::vector<std::uint8_t> encodeGeometry(const std::vector<Ring> &rings);

// Writes `bytes` verbatim to a binary file at `path`. Throws
// std::runtime_error on failure to open/write.
void writeBinaryFile(const std::string &path, const std::vector<std::uint8_t> &bytes);

// Writes a generated C++ header at `path` containing
// `static const std::uint8_t <arrayName>[]` with the identical bytes (plus
// a `<arrayName>Size` size constant), so a build can #include the data
// directly with zero runtime file I/O. Also emits `<arrayName>PointCount`/
// `<arrayName>RingCount` -- a consumer's InputGeometry<MaxPoints, MaxRings>
// (input_format.h) needs to fit this exact dataset, and those counts are
// only ever known here, at generation time, from the decoded rings
// themselves -- not re-derivable from the encoded byte buffer alone
// without re-parsing it. Throws std::runtime_error on failure to open/write.
void writeHeaderFile(const std::string &path, const std::string &arrayName, const std::vector<std::uint8_t> &bytes, std::size_t pointCount, std::size_t ringCount);

} // namespace wrenium_geo_convert
