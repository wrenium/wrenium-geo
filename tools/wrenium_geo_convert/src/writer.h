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
// list), then writes that same byte buffer out three ways -- a raw .bin
// file, a generated C++ data header, and a separate, tiny generated C++
// info header -- all derived from one in-memory encode, so the geometry
// is never decoded/encoded twice.

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
// `static constexpr std::uint8_t <arrayName>[]` with the identical bytes,
// plus a `<arrayName>Size` constant, so a build can #include the data
// directly with zero runtime file I/O. Throws std::runtime_error on
// failure to open/write.
void writeDataHeaderFile(const std::string &path, const std::string &arrayName, const std::vector<std::uint8_t> &bytes);

// Writes a second, tiny generated C++ header at `path` containing
// `<arrayName>Info`, an anonymous pointCount/ringCount/maxRingPointCount
// struct: the capacity InputGeometry<MaxPoints, MaxRings>/a Workspace's
// own MaxRingPoints need for this dataset, standalone. These counts are
// only ever known here, at generation time, from the decoded rings
// themselves -- not re-derivable from the encoded byte buffer alone
// without re-parsing it. Throws std::runtime_error on failure to open/write.
void writeInfoHeaderFile(const std::string &path, const std::string &arrayName, std::size_t pointCount, std::size_t ringCount, std::size_t maxRingPointCount);

} // namespace wrenium_geo_convert
