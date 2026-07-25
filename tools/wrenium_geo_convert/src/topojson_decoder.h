// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "wrenium/geo/geo_point.h"

// Decodes TopoJSON (e.g. world-atlas's land-110m.json) into a flat list of
// closed rings of wrenium::geo::GeoPoint. Split out of main.cpp specifically
// so this logic is directly callable/unit-testable via doctest, without
// spawning the compiled tool as a subprocess.
//
// TopoJSON background: geometry is encoded as shared "arcs", each a
// sequence of [dx, dy] integer deltas that must be cumulatively summed to
// recover quantized absolute coordinates, then mapped through the
// topology's transform.scale/translate to real lon/lat degrees. Geometries
// reference arcs by index into the topology's "arcs" array; a negative
// index ~i (bitwise complement) means "arc i, traversed in reverse." A
// ring is a list of arc references whose decoded points are concatenated
// (dropping the duplicate point at each arc-to-arc joint) into one closed
// loop.

namespace wrenium_geo_convert {

// One fully decoded arc: cumulative-summed, transform-applied absolute
// (lonDeg, latDeg) pairs, in the arc's own natural (non-reversed) order.
using DecodedArc = std::vector<std::pair<double, double>>;

// One closed ring, in this format's final (latRad, lonRad) convention.
// Distinct vertices only -- no duplicated closing point (the ring's
// closure back to its first vertex is implicit, matching how the SVG
// emitter's "Z"/close-path command works).
using Ring = std::vector<wrenium::geo::GeoPoint>;

// Delta-decodes every arc in topology["arcs"], applying
// topology["transform"]'s scale/translate. One entry per source arc, same
// order/index as the input, so index i here corresponds directly to arc
// index i (or ~i for the reversed form) in a geometry's arc references.
std::vector<DecodedArc> decodeArcs(const nlohmann::json &topology);

// Builds one closed ring from a TopoJSON ring's arc-index list: resolves
// each index (reversing the arc's points when the index is bitwise-
// complemented), concatenates them with joint-point dedup, then strips the
// final duplicated closing point if present, and converts each remaining
// (lonDeg, latDeg) vertex into a wrenium::geo::GeoPoint (latRad, lonRad) --
// swapping axis order and converting degrees to radians, both required
// (this is the well-known GeoJSON [lon, lat] gotcha).
Ring buildRing(const std::vector<DecodedArc> &arcs, const std::vector<int> &arcIndices);

// Full decode: resolves topology["objects"][objectName] (a Polygon,
// MultiPolygon, or a GeometryCollection wrapping either) into a flat list
// of rings -- no landmass/feature grouping.
std::vector<Ring> decodeTopology(const nlohmann::json &topology, const std::string &objectName);

// One open polyline -- structurally identical to Ring (both are just a
// point sequence; whether it's read as closed or open is entirely up to
// the consumer, see detail/azimuthal/clip.h's clipRingToSink vs. clipLineToSink), but kept
// as its own name at this layer since a border segment is never a closed
// shape.
using Line = Ring;

// Decodes topology["objects"][objectName] (expected to be a
// GeometryCollection of country-like Polygon/MultiPolygon features, e.g.
// world-atlas's "countries" object) into the set of *interior* shared-arc
// border segments -- matching topojson-client's own
// `topojson.mesh(topology, object, (a, b) => a !== b)` idiom: an arc
// referenced by exactly one distinct feature is that feature's own
// outer/coastal edge (already covered by the separately-decoded land
// object, so it's excluded here); an arc referenced by two or more
// distinct features is a country-to-country border and is emitted exactly
// once as an open Line, regardless of how many features reference it or
// in which direction. Border data is decoded/clipped/rendered as a fully
// separate pipeline from the coastline rings.
std::vector<Line> decodeBorderMesh(const nlohmann::json &topology, const std::string &objectName);

} // namespace wrenium_geo_convert
