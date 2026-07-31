// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "doctest/doctest.h"

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/byte_stream.h"
#include "wrenium/geo/error.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/input_format.h"

// loadInputGeometry parses the wire format topojson2bin produces -- either
// the checked-in .bin file's raw bytes, or its generated .h array included
// directly into MCU firmware. Since that
// data could in principle be corrupted (a bad flash read, a truncated
// download, a hand-edited file) or simply come from a different/future
// version of the converter, this parse path is the library's one real
// "untrusted input" boundary and must never do anything worse than return
// Error::UnrecognizedFormat/Error::TruncatedData/Error::CapacityExceeded --
// no crash, no UB, no silent misread. These tests build the wire bytes by
// hand (mirroring writer.cpp's
// encodeGeometry exactly) rather than depending on the converter tool,
// since this is the core library's own test suite.

using namespace wrenium::geo;

namespace {

template <std::size_t Capacity>
void appendRing(Buffer<std::uint8_t, Capacity> &bytes, const GeoPoint *points, std::uint32_t pointCount)
{
    REQUIRE(detail::writeU32LE(bytes, pointCount) == Error::Ok);
    for (std::uint32_t i = 0; i < pointCount; ++i) {
        REQUIRE(detail::writeFloatLE(bytes, points[i].latRad) == Error::Ok);
        REQUIRE(detail::writeFloatLE(bytes, points[i].lonRad) == Error::Ok);
    }
}

template <std::size_t Capacity>
void writeHeader(Buffer<std::uint8_t, Capacity> &bytes, std::uint32_t magic, std::uint32_t version, std::uint32_t ringCount) // NOLINT(bugprone-easily-swappable-parameters)
{
    REQUIRE(detail::writeU32LE(bytes, magic) == Error::Ok);
    REQUIRE(detail::writeU32LE(bytes, version) == Error::Ok);
    REQUIRE(detail::writeU32LE(bytes, ringCount) == Error::Ok);
}

} // namespace

TEST_CASE("loadInputGeometry parses a well-formed two-ring stream, including per-ring lat bounds")
{
    const GeoPoint ring0[] = {
        {0.1f, 0.2f},
        {-0.3f, 0.4f},
        {0.5f, -0.6f},
    };
    const GeoPoint ring1[] = {
        {1.0f, 1.0f},
        {1.5f, 1.2f},
    };

    Buffer<std::uint8_t, 256> bytes;
    writeHeader(bytes, kInputGeometryMagic, kInputGeometryVersion, 2);
    appendRing(bytes, ring0, 3);
    appendRing(bytes, ring1, 2);

    InputGeometry<16, 4> geometry;

    const Error err = loadInputGeometry(bytes.data(), bytes.size(), geometry);
    REQUIRE(err == Error::Ok);

    REQUIRE(geometry.points.size() == 5);
    REQUIRE(geometry.ringSizes.size() == 2);
    CHECK(geometry.ringSizes[0] == 3);
    CHECK(geometry.ringSizes[1] == 2);

    CHECK(geometry.points[0].latRad == doctest::Approx(0.1f));
    CHECK(geometry.points[0].lonRad == doctest::Approx(0.2f));
    CHECK(geometry.points[4].latRad == doctest::Approx(1.5f));
    CHECK(geometry.points[4].lonRad == doctest::Approx(1.2f));

    REQUIRE(geometry.ringMinLat.size() == 2);
    REQUIRE(geometry.ringMaxLat.size() == 2);
    CHECK(geometry.ringMinLat[0] == doctest::Approx(-0.3f));
    CHECK(geometry.ringMaxLat[0] == doctest::Approx(0.5f));
    CHECK(geometry.ringMinLat[1] == doctest::Approx(1.0f));
    CHECK(geometry.ringMaxLat[1] == doctest::Approx(1.5f));
}

TEST_CASE("loadInputGeometry accepts a zero-ring stream (empty geometry)")
{
    Buffer<std::uint8_t, 32> bytes;
    writeHeader(bytes, kInputGeometryMagic, kInputGeometryVersion, 0);

    InputGeometry<16, 4> geometry;

    const Error err = loadInputGeometry(bytes.data(), bytes.size(), geometry);
    CHECK(err == Error::Ok);
    CHECK(geometry.points.size() == 0);
    CHECK(geometry.ringSizes.size() == 0);
}

TEST_CASE("loadInputGeometry rejects a ring with zero points cleanly (no crash, empty bounds are well-defined)")
{
    Buffer<std::uint8_t, 32> bytes;
    writeHeader(bytes, kInputGeometryMagic, kInputGeometryVersion, 1);
    REQUIRE(detail::writeU32LE(bytes, 0) == Error::Ok); // pointCount == 0

    InputGeometry<16, 4> geometry;

    const Error err = loadInputGeometry(bytes.data(), bytes.size(), geometry);
    CHECK(err == Error::Ok);
    REQUIRE(geometry.ringSizes.size() == 1);
    CHECK(geometry.ringSizes[0] == 0);
    // minLat/maxLat both default to 0.0f (the loop's p==0 initializer never
    // runs for an empty ring) -- documenting the actual behavior, not just
    // asserting it doesn't crash.
    CHECK(geometry.ringMinLat[0] == 0.0f);
    CHECK(geometry.ringMaxLat[0] == 0.0f);
}

TEST_CASE("loadInputGeometry rejects an empty buffer")
{
    InputGeometry<16, 4> geometry;

    const Error err = loadInputGeometry(nullptr, 0, geometry);
    CHECK(err == Error::TruncatedData);
}

TEST_CASE("loadInputGeometry rejects a buffer too short to hold even the header")
{
    Buffer<std::uint8_t, 32> bytes;
    // 8 bytes -- one uint32 short of the 12-byte header.
    REQUIRE(detail::writeU32LE(bytes, kInputGeometryMagic) == Error::Ok);
    REQUIRE(detail::writeU32LE(bytes, kInputGeometryVersion) == Error::Ok);

    InputGeometry<16, 4> geometry;

    const Error err = loadInputGeometry(bytes.data(), bytes.size(), geometry);
    CHECK(err == Error::TruncatedData);
}

TEST_CASE("loadInputGeometry rejects a wrong magic number")
{
    Buffer<std::uint8_t, 32> bytes;
    writeHeader(bytes, 0xDEADBEEFu, kInputGeometryVersion, 0);

    InputGeometry<16, 4> geometry;

    const Error err = loadInputGeometry(bytes.data(), bytes.size(), geometry);
    CHECK(err == Error::UnrecognizedFormat);
}

TEST_CASE("loadInputGeometry rejects a wrong/future version number")
{
    Buffer<std::uint8_t, 32> bytes;
    writeHeader(bytes, kInputGeometryMagic, kInputGeometryVersion + 1, 0);

    InputGeometry<16, 4> geometry;

    const Error err = loadInputGeometry(bytes.data(), bytes.size(), geometry);
    CHECK(err == Error::UnrecognizedFormat);
}

TEST_CASE("loadInputGeometry rejects a stream that ends mid-ring-header")
{
    // Declares 2 rings but the stream is cut off right after the header,
    // before the first ring's own 4-byte pointCount field.
    Buffer<std::uint8_t, 32> bytes;
    writeHeader(bytes, kInputGeometryMagic, kInputGeometryVersion, 2);

    InputGeometry<16, 4> geometry;

    const Error err = loadInputGeometry(bytes.data(), bytes.size(), geometry);
    CHECK(err == Error::TruncatedData);
}

TEST_CASE("loadInputGeometry rejects a stream that ends mid-point (ring claims more points than remain)")
{
    Buffer<std::uint8_t, 64> bytes;
    writeHeader(bytes, kInputGeometryMagic, kInputGeometryVersion, 1);
    REQUIRE(detail::writeU32LE(bytes, 3) == Error::Ok); // claims 3 points
    // Only one full point's worth of floats actually follow.
    REQUIRE(detail::writeFloatLE(bytes, 0.1f) == Error::Ok);
    REQUIRE(detail::writeFloatLE(bytes, 0.2f) == Error::Ok);

    InputGeometry<16, 4> geometry;

    const Error err = loadInputGeometry(bytes.data(), bytes.size(), geometry);
    CHECK(err == Error::TruncatedData);
}

TEST_CASE("loadInputGeometry rejects a stream truncated by exactly one byte within a point")
{
    // A subtler truncation than a whole missing point: the ring claims 2
    // points, and the stream has every byte of the first plus 7 of the
    // second's 8 -- one byte short, not a whole field short.
    Buffer<std::uint8_t, 64> bytes;
    writeHeader(bytes, kInputGeometryMagic, kInputGeometryVersion, 1);
    REQUIRE(detail::writeU32LE(bytes, 2) == Error::Ok);
    REQUIRE(detail::writeFloatLE(bytes, 0.1f) == Error::Ok);
    REQUIRE(detail::writeFloatLE(bytes, 0.2f) == Error::Ok);
    REQUIRE(detail::writeFloatLE(bytes, 0.3f) == Error::Ok);
    Buffer<std::uint8_t, 64> truncated;
    for (std::size_t i = 0; i + 1 < bytes.size(); ++i) {
        REQUIRE(truncated.pushBack(bytes[i]) == Error::Ok);
    }

    InputGeometry<16, 4> geometry;

    const Error err = loadInputGeometry(truncated.data(), truncated.size(), geometry);
    CHECK(err == Error::TruncatedData);
}

TEST_CASE("loadInputGeometry reports Error::CapacityExceeded when ring count exceeds geometry's ring capacity")
{
    Buffer<std::uint8_t, 128> bytes;
    writeHeader(bytes, kInputGeometryMagic, kInputGeometryVersion, 3); // 3 rings
    const GeoPoint p[] = {{0.0f, 0.0f}, {0.1f, 0.1f}};
    appendRing(bytes, p, 2);
    appendRing(bytes, p, 2);
    appendRing(bytes, p, 2);

    InputGeometry<16, 2> geometry; // room for only 2 rings

    const Error err = loadInputGeometry(bytes.data(), bytes.size(), geometry);
    CHECK(err == Error::CapacityExceeded);
    CHECK(geometry.ringSizes.size() <= geometry.ringSizes.capacity());
}

TEST_CASE("loadInputGeometry reports Error::CapacityExceeded when total point count exceeds geometry's point capacity")
{
    Buffer<std::uint8_t, 128> bytes;
    writeHeader(bytes, kInputGeometryMagic, kInputGeometryVersion, 1);
    const GeoPoint ring[] = {{0.0f, 0.0f}, {0.1f, 0.1f}, {0.2f, 0.2f}, {0.3f, 0.3f}};
    appendRing(bytes, ring, 4); // 4 points

    InputGeometry<2, 4> geometry; // room for only 2 points

    const Error err = loadInputGeometry(bytes.data(), bytes.size(), geometry);
    CHECK(err == Error::CapacityExceeded);
    CHECK(geometry.points.size() <= geometry.points.capacity());
}
