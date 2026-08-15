// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "topojson_decoder.h"
#include "writer.h"

namespace {

nlohmann::json loadJsonFile(const std::string &path)
{
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("topojson2bin: failed to open input file: " + path);
    }

    nlohmann::json json;
    file >> json;
    return json;
}

void printUsage(const char *programName)
{
    std::cerr << "usage: " << programName << " [--mesh] <input.topojson> <object-name> <output.bin> <output.h> [array-name]\n"
              << "  --mesh          decode object-name as a countries-style GeometryCollection and\n"
              << "                  emit only its interior (country-to-country) shared-arc border\n"
              << "                  segments as open polylines, instead of closed coastline rings\n"
              << "  input.topojson  path to a TopoJSON file (e.g. world-atlas's land-110m.json)\n"
              << "  object-name     key under \"objects\" to convert (e.g. \"land\", or \"countries\" with --mesh)\n"
              << "  output.bin      path to write the raw binary geometry (input_format.h layout)\n"
              << "  output.h        path to write the generated C++ byte-array header\n"
              << "  array-name      optional C++ array identifier (default: kWreniumGeoCoastlineData)\n";
}

} // namespace

int main(int argc, char *argv[])
{
    int argi = 1;
    bool meshMode = false;
    if (argc >= 2 && std::string(argv[1]) == "--mesh") {
        meshMode = true;
        ++argi;
    }
    const int remaining = argc - argi;

    if (remaining != 4 && remaining != 5) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const std::string inputPath = argv[argi];
    const std::string objectName = argv[argi + 1];
    const std::string outputBinPath = argv[argi + 2];
    const std::string outputHeaderPath = argv[argi + 3];
    const std::string arrayName = (remaining == 5) ? argv[argi + 4] : "kWreniumGeoCoastlineData";

    try {
        const nlohmann::json topology = loadJsonFile(inputPath);
        const std::vector<wrenium_geo_convert::Ring> rings = meshMode
            ? wrenium_geo_convert::decodeBorderMesh(topology, objectName)
            : wrenium_geo_convert::decodeTopology(topology, objectName);

        std::size_t totalPoints = 0;
        for (const wrenium_geo_convert::Ring &ring : rings) {
            totalPoints += ring.size();
        }

        // Decode once, encode once, write both output forms from the same
        // in-memory byte buffer.
        const std::vector<std::uint8_t> bytes = wrenium_geo_convert::encodeGeometry(rings);

        wrenium_geo_convert::writeBinaryFile(outputBinPath, bytes);
        wrenium_geo_convert::writeHeaderFile(outputHeaderPath, arrayName, bytes, totalPoints, rings.size());

        std::cout << "topojson2bin: decoded " << rings.size() << (meshMode ? " border segments, " : " rings, ")
                  << totalPoints << " points total (" << bytes.size() << " bytes)\n"
                  << "  wrote " << outputBinPath << "\n"
                  << "  wrote " << outputHeaderPath << "\n";
    } catch (const std::exception &e) {
        std::cerr << "topojson2bin: error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
