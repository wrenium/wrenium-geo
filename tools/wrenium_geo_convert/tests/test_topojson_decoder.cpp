// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "topojson_decoder.h"
#include "wrenium/geo/input_format.h"
#include "writer.h"

// Synthetic-TopoJSON fixture used below (hand-verifiable by construction):
//
// transform.scale = [2, 3], transform.translate = [100, 200].
//
// Two arcs encode a rectangle A(0,0) -> B(10,0) -> C(10,10) -> D(0,10) ->
// back to A, in quantized (pre-transform) integer coordinates:
//   arc0 (natural order): A -> B -> C           deltas [[0,0],[10,0],[0,10]]
//   arc1 (natural order): A -> D -> C           deltas [[0,0],[0,10],[10,0]]
//
// A ring referencing arcs [0, ~1] (i.e. [0, -2] in JSON) walks arc0 forward
// (A,B,C), then arc1 *reversed* (C,D,A) -- exercising both joint dedup
// (the shared C) and negative-index reversal in one fixture.
//
// Expected quantized*transform absolute coordinates (lonDeg, latDeg):
//   A = (0*2+100,  0*3+200)  = (100, 200)
//   B = (10*2+100, 0*3+200)  = (120, 200)
//   C = (10*2+100, 10*3+200) = (120, 230)
//   D = (0*2+100,  10*3+200) = (100, 230)
//
// So the decoded ring, in (latRad, lonRad) order (axis swap + deg->rad),
// should be exactly [A, B, C, D] (4 distinct points -- no duplicated
// closing point).

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

nlohmann::json makeSyntheticTopology()
{
    return nlohmann::json::parse(R"json(
    {
        "type": "Topology",
        "transform": { "scale": [2, 3], "translate": [100, 200] },
        "arcs": [
            [[0, 0], [10, 0], [0, 10]],
            [[0, 0], [0, 10], [10, 0]]
        ],
        "objects": {
            "land": {
                "type": "GeometryCollection",
                "geometries": [
                    { "type": "Polygon", "arcs": [[0, -2]] }
                ]
            }
        }
    }
    )json");
}

} // namespace

TEST_CASE("decodeArcs applies cumulative delta-sum and transform scale/translate")
{
    const nlohmann::json topology = makeSyntheticTopology();
    const std::vector<wrenium_geo_convert::DecodedArc> arcs = wrenium_geo_convert::decodeArcs(topology);

    REQUIRE(arcs.size() == 2);

    // arc0: A(100,200) -> B(120,200) -> C(120,230)
    REQUIRE(arcs[0].size() == 3);
    CHECK(arcs[0][0].first == doctest::Approx(100.0));
    CHECK(arcs[0][0].second == doctest::Approx(200.0));
    CHECK(arcs[0][1].first == doctest::Approx(120.0));
    CHECK(arcs[0][1].second == doctest::Approx(200.0));
    CHECK(arcs[0][2].first == doctest::Approx(120.0));
    CHECK(arcs[0][2].second == doctest::Approx(230.0));

    // arc1: A(100,200) -> D(100,230) -> C(120,230)
    REQUIRE(arcs[1].size() == 3);
    CHECK(arcs[1][0].first == doctest::Approx(100.0));
    CHECK(arcs[1][0].second == doctest::Approx(200.0));
    CHECK(arcs[1][1].first == doctest::Approx(100.0));
    CHECK(arcs[1][1].second == doctest::Approx(230.0));
    CHECK(arcs[1][2].first == doctest::Approx(120.0));
    CHECK(arcs[1][2].second == doctest::Approx(230.0));
}

TEST_CASE("buildRing stitches arcs with joint dedup, honors reversal, drops closing duplicate")
{
    const nlohmann::json topology = makeSyntheticTopology();
    const std::vector<wrenium_geo_convert::DecodedArc> arcs = wrenium_geo_convert::decodeArcs(topology);

    const wrenium_geo_convert::Ring ring = wrenium_geo_convert::buildRing(arcs, {0, -2});

    REQUIRE(ring.size() == 4);

    const float expectedLat[4] = {
        static_cast<float>(200.0 * kDegToRad),
        static_cast<float>(200.0 * kDegToRad),
        static_cast<float>(230.0 * kDegToRad),
        static_cast<float>(230.0 * kDegToRad),
    };
    const float expectedLon[4] = {
        static_cast<float>(100.0 * kDegToRad),
        static_cast<float>(120.0 * kDegToRad),
        static_cast<float>(120.0 * kDegToRad),
        static_cast<float>(100.0 * kDegToRad),
    };

    for (int i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK(ring[static_cast<std::size_t>(i)].latRad == doctest::Approx(expectedLat[i]));
        CHECK(ring[static_cast<std::size_t>(i)].lonRad == doctest::Approx(expectedLon[i]));
    }
}

TEST_CASE("decodeTopology resolves a GeometryCollection/Polygon object into one flat ring")
{
    const nlohmann::json topology = makeSyntheticTopology();
    const std::vector<wrenium_geo_convert::Ring> rings = wrenium_geo_convert::decodeTopology(topology, "land");

    REQUIRE(rings.size() == 1);
    CHECK(rings[0].size() == 4);

    CHECK(rings[0][0].latRad == doctest::Approx(static_cast<float>(200.0 * kDegToRad)));
    CHECK(rings[0][0].lonRad == doctest::Approx(static_cast<float>(100.0 * kDegToRad)));
    CHECK(rings[0][2].latRad == doctest::Approx(static_cast<float>(230.0 * kDegToRad)));
    CHECK(rings[0][2].lonRad == doctest::Approx(static_cast<float>(120.0 * kDegToRad)));
}

TEST_CASE("encodeGeometry packs rings into the exact InputGeometryHeader wire layout")
{
    const nlohmann::json topology = makeSyntheticTopology();
    const std::vector<wrenium_geo_convert::Ring> rings = wrenium_geo_convert::decodeTopology(topology, "land");
    const std::vector<std::uint8_t> bytes = wrenium_geo_convert::encodeGeometry(rings);

    // header (12 bytes) + ring0's pointCount (4 bytes) + 4 points * 2 floats * 4 bytes
    REQUIRE(bytes.size() == 12 + 4 + 4 * 2 * 4);

    auto readU32 = [&](std::size_t offset) -> std::uint32_t {
        return static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    };

    CHECK(readU32(0) == wrenium::geo::kInputGeometryMagic);
    CHECK(readU32(4) == wrenium::geo::kInputGeometryVersion);
    CHECK(readU32(8) == 1u);  // ringCount
    CHECK(readU32(12) == 4u); // ring0 pointCount

    auto readFloat = [&](std::size_t offset) -> float {
        std::uint32_t bits = readU32(offset);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };

    CHECK(readFloat(16) == doctest::Approx(static_cast<float>(200.0 * kDegToRad))); // point0 lat
    CHECK(readFloat(20) == doctest::Approx(static_cast<float>(100.0 * kDegToRad))); // point0 lon
}

// ---- decodeBorderMesh: interior (country-to-country) shared-arc
// extraction, matching topojson.mesh(topology, obj, (a, b) => a !== b) ----
//
// Synthetic fixture: three countries sharing arcs pairwise.
//   arc0: referenced only by country "A" (its own outer/coastal edge) --
//         must be *excluded*.
//   arc1: referenced by both "A" and "B" (their shared border) -- must be
//         *included*, exactly once.
//   arc2: referenced by both "B" and "C" (a different shared border) --
//         also included.
// Each country is a single-ring Polygon: A = [0, 1], B = [~1, 2]
// (B walks the A/B border in reverse, then its own border with C), C = [~2].
namespace {

nlohmann::json makeSyntheticCountriesTopology()
{
    return nlohmann::json::parse(R"json(
    {
        "type": "Topology",
        "transform": { "scale": [1, 1], "translate": [0, 0] },
        "arcs": [
            [[0, 0], [10, 0]],
            [[10, 0], [0, 10]],
            [[10, 10], [-10, 0]]
        ],
        "objects": {
            "countries": {
                "type": "GeometryCollection",
                "geometries": [
                    { "type": "Polygon", "properties": { "name": "A" }, "arcs": [[0, 1]] },
                    { "type": "Polygon", "properties": { "name": "B" }, "arcs": [[-2, 2]] },
                    { "type": "Polygon", "properties": { "name": "C" }, "arcs": [[-3]] }
                ]
            }
        }
    }
    )json");
}

} // namespace

TEST_CASE("decodeBorderMesh keeps only arcs shared by two distinct features, each exactly once")
{
    const nlohmann::json topology = makeSyntheticCountriesTopology();
    const std::vector<wrenium_geo_convert::Line> lines = wrenium_geo_convert::decodeBorderMesh(topology, "countries");

    // arc0 (A only) excluded; arc1 (A & B) and arc2 (B & C) included --
    // never arc0, never duplicated even though B references both its
    // shared arcs.
    REQUIRE(lines.size() == 2);
    for (const wrenium_geo_convert::Line &line : lines) {
        CHECK(line.size() == 2);
    }
}

TEST_CASE("decodeBorderMesh returns nothing for a single feature with no shared arcs")
{
    nlohmann::json topology = nlohmann::json::parse(R"json(
    {
        "type": "Topology",
        "transform": { "scale": [1, 1], "translate": [0, 0] },
        "arcs": [
            [[0, 0], [10, 0], [0, 10], [-10, -10]]
        ],
        "objects": {
            "countries": {
                "type": "GeometryCollection",
                "geometries": [
                    { "type": "Polygon", "properties": { "name": "Solo" }, "arcs": [[0]] }
                ]
            }
        }
    }
    )json");

    const std::vector<wrenium_geo_convert::Line> lines = wrenium_geo_convert::decodeBorderMesh(topology, "countries");
    CHECK(lines.size() == 0);
}

#ifdef WRENIUM_GEO_CONVERT_TEST_DATA_DIR

TEST_CASE("smoke test: decodes the real world-atlas countries-110m.json into sane border-segment counts")
{
    const std::string path = std::string(WRENIUM_GEO_CONVERT_TEST_DATA_DIR) + "/countries-110m.json";
    std::ifstream file(path);
    REQUIRE_MESSAGE(file.good(), "expected fixture at " << path);

    nlohmann::json topology;
    file >> topology;

    const std::vector<wrenium_geo_convert::Line> lines = wrenium_geo_convert::decodeBorderMesh(topology, "countries");

    // world-atlas's countries-110m.json decodes to 326 border segments,
    // 2,974 points total. Checked as sane bounds here (world-atlas could
    // revise the dataset) rather than an exact pin.
    CHECK(lines.size() > 200);
    CHECK(lines.size() < 500);

    std::size_t totalPoints = 0;
    for (const wrenium_geo_convert::Line &line : lines) {
        CHECK(line.size() >= 2); // an open line needs at least two points
        totalPoints += line.size();
    }
    CHECK(totalPoints > 2000);
    CHECK(totalPoints < 4000);
}

TEST_CASE("smoke test: decodes the real world-atlas land-110m.json into sane ring/point counts")
{
    const std::string path = std::string(WRENIUM_GEO_CONVERT_TEST_DATA_DIR) + "/land-110m.json";
    std::ifstream file(path);
    REQUIRE_MESSAGE(file.good(), "expected fixture at " << path);

    nlohmann::json topology;
    file >> topology;

    // world-atlas's land-110m.json has 130 arcs, 5,129 total coordinate
    // pairs pre-flatten.
    REQUIRE(topology.at("arcs").size() == 130);

    std::size_t totalRawPairs = 0;
    for (const nlohmann::json &arc : topology.at("arcs")) {
        totalRawPairs += arc.size();
    }
    REQUIRE(totalRawPairs == 5129);

    const std::vector<wrenium_geo_convert::Ring> rings = wrenium_geo_convert::decodeTopology(topology, "land");

    // Flattened ring count is not expected to equal the arc count (rings
    // are built from concatenated arcs, and one ring can span several
    // arcs) -- assert sane bounds rather than an assumed 1:1 relationship.
    CHECK(rings.size() > 0);
    CHECK(rings.size() <= 130);

    std::size_t totalPoints = 0;
    for (const wrenium_geo_convert::Ring &ring : rings) {
        CHECK(ring.size() >= 3); // a closed ring needs at least a triangle
        totalPoints += ring.size();
    }

    // Each ring drops exactly one duplicated closing point relative to the
    // raw concatenated-arc point count, and arc-to-arc joints within a
    // ring are also deduplicated -- so the flattened total is comfortably
    // below the raw 5,129 figure, but not wildly so.
    CHECK(totalPoints > 4000);
    CHECK(totalPoints < 5129);
}

// ---- Malformed input: this tool runs offline, once, against a trusted
// TopoJSON file a developer chose (deliberately not part of the
// no-exceptions embedded library), so throwing on bad input is the
// accepted contract here, not a bug -- main.cpp's top-level try/catch
// turns any of these into a clean "topojson2bin: error: ..." message
// and a non-zero exit rather than a crash (see main.cpp). These tests
// confirm that contract actually holds for the malformed shapes a
// corrupted download or a hand-edited file could plausibly produce, not
// just for the one well-formed fixture every other test above uses.

TEST_CASE("decodeArcs throws when the topology has no \"transform\" key")
{
    const nlohmann::json topology = nlohmann::json::parse(R"json(
    { "type": "Topology", "arcs": [[[0, 0]]] }
    )json");

    CHECK_THROWS_AS(wrenium_geo_convert::decodeArcs(topology), nlohmann::json::exception);
}

TEST_CASE("decodeArcs throws when the topology has no \"arcs\" key")
{
    const nlohmann::json topology = nlohmann::json::parse(R"json(
    { "type": "Topology", "transform": { "scale": [1, 1], "translate": [0, 0] } }
    )json");

    CHECK_THROWS_AS(wrenium_geo_convert::decodeArcs(topology), nlohmann::json::exception);
}

TEST_CASE("decodeArcs throws when transform.scale has the wrong shape")
{
    const nlohmann::json topology = nlohmann::json::parse(R"json(
    {
        "type": "Topology",
        "transform": { "scale": [1], "translate": [0, 0] },
        "arcs": [[[0, 0]]]
    }
    )json");

    CHECK_THROWS_AS(wrenium_geo_convert::decodeArcs(topology), nlohmann::json::exception);
}

TEST_CASE("decodeTopology throws when objectName doesn't exist under \"objects\"")
{
    const nlohmann::json topology = makeSyntheticTopology();
    CHECK_THROWS_AS(wrenium_geo_convert::decodeTopology(topology, "nonexistent"), nlohmann::json::exception);
}

TEST_CASE("buildRing throws std::out_of_range for an arc index beyond the decoded arc list")
{
    const nlohmann::json topology = makeSyntheticTopology();
    const std::vector<wrenium_geo_convert::DecodedArc> arcs = wrenium_geo_convert::decodeArcs(topology);
    REQUIRE(arcs.size() == 2);

    // Valid indices here are only 0, 1 (or their reversed forms -1, -2).
    CHECK_THROWS_AS(wrenium_geo_convert::buildRing(arcs, {5}), std::out_of_range);
}

TEST_CASE("buildRing throws std::out_of_range for a reversed (negative) arc index beyond the decoded arc list")
{
    const nlohmann::json topology = makeSyntheticTopology();
    const std::vector<wrenium_geo_convert::DecodedArc> arcs = wrenium_geo_convert::decodeArcs(topology);

    // ~5 (bitwise complement) -- a reversed reference to a nonexistent arc.
    CHECK_THROWS_AS(wrenium_geo_convert::buildRing(arcs, {~5}), std::out_of_range);
}

TEST_CASE("decodeBorderMesh throws std::out_of_range for an out-of-range arc index")
{
    const nlohmann::json topology = nlohmann::json::parse(R"json(
    {
        "type": "Topology",
        "transform": { "scale": [1, 1], "translate": [0, 0] },
        "arcs": [[[0, 0], [10, 0]]],
        "objects": {
            "countries": {
                "type": "GeometryCollection",
                "geometries": [
                    { "type": "Polygon", "properties": { "name": "A" }, "arcs": [[7]] }
                ]
            }
        }
    }
    )json");

    CHECK_THROWS_AS(wrenium_geo_convert::decodeBorderMesh(topology, "countries"), std::out_of_range);
}

TEST_CASE("decodeTopology on an empty GeometryCollection yields no rings, not an error")
{
    const nlohmann::json topology = nlohmann::json::parse(R"json(
    {
        "type": "Topology",
        "transform": { "scale": [1, 1], "translate": [0, 0] },
        "arcs": [],
        "objects": {
            "land": { "type": "GeometryCollection", "geometries": [] }
        }
    }
    )json");

    const std::vector<wrenium_geo_convert::Ring> rings = wrenium_geo_convert::decodeTopology(topology, "land");
    CHECK(rings.empty());
}

#endif // WRENIUM_GEO_CONVERT_TEST_DATA_DIR
