// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

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
// directly with zero runtime file I/O. Throws std::runtime_error on
// failure to open/write.
void writeHeaderFile(const std::string &path, const std::string &arrayName, const std::vector<std::uint8_t> &bytes);

} // namespace wrenium_geo_convert
