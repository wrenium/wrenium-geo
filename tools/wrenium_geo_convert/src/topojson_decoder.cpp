// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "topojson_decoder.h"

#include <cmath>
#include <stdexcept>

namespace wrenium_geo_convert {

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kJointEpsilon = 1e-9;

} // namespace

std::vector<DecodedArc> decodeArcs(const nlohmann::json &topology)
{
    const nlohmann::json &transform = topology.at("transform");
    const double kx = transform.at("scale").at(0).get<double>();
    const double ky = transform.at("scale").at(1).get<double>();
    const double dx = transform.at("translate").at(0).get<double>();
    const double dy = transform.at("translate").at(1).get<double>();

    const nlohmann::json &rawArcs = topology.at("arcs");

    std::vector<DecodedArc> decoded;
    decoded.reserve(rawArcs.size());

    for (const nlohmann::json &rawArc : rawArcs) {
        DecodedArc arc;
        arc.reserve(rawArc.size());

        // Cumulative delta-sum resets at the start of every arc -- each
        // arc's first "delta" is really its own absolute quantized start
        // position (relative to the topology's transform origin).
        double x = 0.0;
        double y = 0.0;

        for (const nlohmann::json &delta : rawArc) {
            x += delta.at(0).get<double>();
            y += delta.at(1).get<double>();
            arc.emplace_back(x * kx + dx, y * ky + dy);
        }

        decoded.push_back(std::move(arc));
    }

    return decoded;
}

Ring buildRing(const std::vector<DecodedArc> &arcs, const std::vector<int> &arcIndices)
{
    std::vector<std::pair<double, double>> points;

    for (int rawIndex : arcIndices) {
        const bool reversed = rawIndex < 0;
        const std::size_t arcIndex = reversed
            ? static_cast<std::size_t>(~rawIndex)
            : static_cast<std::size_t>(rawIndex);

        if (arcIndex >= arcs.size()) {
            throw std::out_of_range("topojson2bin: arc index out of range");
        }
        const DecodedArc &arc = arcs[arcIndex];

        // Consecutive arcs in a ring share their joint coordinate (this
        // arc's first point, post-reversal, equals the previous arc's last
        // point) -- drop that duplicate before appending, matching
        // topojson-client's own arc-stitching convention.
        if (!points.empty()) {
            points.pop_back();
        }

        if (!reversed) {
            points.insert(points.end(), arc.begin(), arc.end());
        } else {
            points.insert(points.end(), arc.rbegin(), arc.rend());
        }
    }

    // The ring is closed by construction (its last point coincides with
    // its first) -- store only distinct vertices, no duplicated closing
    // point; closure back to the first point is implicit downstream.
    if (points.size() > 1) {
        const std::pair<double, double> &first = points.front();
        const std::pair<double, double> &last = points.back();
        if (std::abs(first.first - last.first) < kJointEpsilon &&
            std::abs(first.second - last.second) < kJointEpsilon) {
            points.pop_back();
        }
    }

    Ring ring;
    ring.reserve(points.size());
    for (const std::pair<double, double> &lonLatDeg : points) {
        wrenium::geo::GeoPoint point;
        // Axis swap ([lon, lat] -> (lat, lon)) AND degrees -> radians --
        // both must happen; this is the well-known GeoJSON/TopoJSON
        // coordinate-order gotcha.
        point.latRad = static_cast<float>(lonLatDeg.second * kDegToRad);
        point.lonRad = static_cast<float>(lonLatDeg.first * kDegToRad);
        ring.push_back(point);
    }

    return ring;
}

namespace {

void collectPolygonRings(const std::vector<DecodedArc> &arcs, const nlohmann::json &polygonArcs, std::vector<Ring> &rings)
{
    // polygonArcs: array of rings, each ring an array of arc indices.
    for (const nlohmann::json &ringArcs : polygonArcs) {
        rings.push_back(buildRing(arcs, ringArcs.get<std::vector<int>>()));
    }
}

void collectGeometryRings(const std::vector<DecodedArc> &arcs, const nlohmann::json &geometry, std::vector<Ring> &rings)
{
    const std::string type = geometry.at("type").get<std::string>();

    if (type == "Polygon") {
        collectPolygonRings(arcs, geometry.at("arcs"), rings);
    } else if (type == "MultiPolygon") {
        // arcs: array of polygons, each polygon an array of rings.
        for (const nlohmann::json &polygonArcs : geometry.at("arcs")) {
            collectPolygonRings(arcs, polygonArcs, rings);
        }
    }
    // Other geometry types (Point/LineString/MultiLineString) aren't
    // expected in a coastline "land"-style object; silently ignored, since
    // this converter is specifically scoped to polygon coastline data.
}

} // namespace

std::vector<Ring> decodeTopology(const nlohmann::json &topology, const std::string &objectName)
{
    const std::vector<DecodedArc> arcs = decodeArcs(topology);
    const nlohmann::json &object = topology.at("objects").at(objectName);

    std::vector<Ring> rings;

    if (object.at("type") == "GeometryCollection") {
        for (const nlohmann::json &geometry : object.at("geometries")) {
            collectGeometryRings(arcs, geometry, rings);
        }
    } else {
        collectGeometryRings(arcs, object, rings);
    }

    return rings;
}

namespace {

void collectArcRefs(const nlohmann::json &geometry, std::vector<int> &arcRefs)
{
    const std::string type = geometry.at("type").get<std::string>();

    if (type == "Polygon") {
        for (const nlohmann::json &ringArcs : geometry.at("arcs")) {
            for (const nlohmann::json &idx : ringArcs) {
                arcRefs.push_back(idx.get<int>());
            }
        }
    } else if (type == "MultiPolygon") {
        for (const nlohmann::json &polygonArcs : geometry.at("arcs")) {
            for (const nlohmann::json &ringArcs : polygonArcs) {
                for (const nlohmann::json &idx : ringArcs) {
                    arcRefs.push_back(idx.get<int>());
                }
            }
        }
    }
    // Other geometry types aren't expected in a countries-style object;
    // silently ignored, mirroring collectGeometryRings above.
}

} // namespace

std::vector<Line> decodeBorderMesh(const nlohmann::json &topology, const std::string &objectName)
{
    const std::vector<DecodedArc> arcs = decodeArcs(topology);
    const nlohmann::json &object = topology.at("objects").at(objectName);

    std::vector<const nlohmann::json *> geometries;
    if (object.at("type") == "GeometryCollection") {
        for (const nlohmann::json &geometry : object.at("geometries")) {
            geometries.push_back(&geometry);
        }
    } else {
        geometries.push_back(&object);
    }

    // ownerFirst[arcIndex]: index (into `geometries`) of the first feature
    // seen referencing this arc, or -1 if none yet. ownerMultiple[arcIndex]
    // becomes true the moment a *different* feature also references it --
    // that's the whole test; which specific features they are doesn't
    // matter beyond that.
    std::vector<int> ownerFirst(arcs.size(), -1);
    std::vector<bool> ownerMultiple(arcs.size(), false);

    for (std::size_t g = 0; g < geometries.size(); ++g) {
        std::vector<int> arcRefs;
        collectArcRefs(*geometries[g], arcRefs);

        for (int rawIndex : arcRefs) {
            const std::size_t arcIndex = rawIndex < 0
                ? static_cast<std::size_t>(~rawIndex)
                : static_cast<std::size_t>(rawIndex);
            if (arcIndex >= arcs.size()) {
                throw std::out_of_range("topojson2bin: arc index out of range");
            }

            if (ownerFirst[arcIndex] == -1) {
                ownerFirst[arcIndex] = static_cast<int>(g);
            } else if (ownerFirst[arcIndex] != static_cast<int>(g)) {
                ownerMultiple[arcIndex] = true;
            }
        }
    }

    std::vector<Line> lines;
    for (std::size_t arcIndex = 0; arcIndex < arcs.size(); ++arcIndex) {
        if (!ownerMultiple[arcIndex]) {
            continue;
        }

        const DecodedArc &arc = arcs[arcIndex];
        Line line;
        line.reserve(arc.size());
        for (const std::pair<double, double> &lonLatDeg : arc) {
            wrenium::geo::GeoPoint point;
            point.latRad = static_cast<float>(lonLatDeg.second * kDegToRad);
            point.lonRad = static_cast<float>(lonLatDeg.first * kDegToRad);
            line.push_back(point);
        }
        lines.push_back(std::move(line));
    }

    return lines;
}

} // namespace wrenium_geo_convert
