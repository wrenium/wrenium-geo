// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#ifndef WRENIUM_GEO_COMMON_BINARY_PATH_DECODER_H
#define WRENIUM_GEO_COMMON_BINARY_PATH_DECODER_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <wrenium/geo/binary_format.h>
#include <wrenium/geo/buffer.h>
#include <wrenium/geo/error.h>
#include <wrenium/geo/float_format.h>

// Example decoder: turns wrenium-geo's binary path stream (binary_format.h)
// back into an SVG path `d` string, written entirely against the library's
// public headers -- no wrenium/geo/detail/* needed, so this is something
// any consumer could write themselves. WreniumGeoBridge only uses it to
// prove its binary-then-decode path renders the same geometry as the
// direct SVG path; a real binary-format consumer would more likely draw
// the MoveTo/LineTo/ClosePath commands directly instead of reconstructing
// SVG text.

namespace BinaryPathDecoderExample {

template <typename Commands = wrenium::geo::PathCommands>
class BinaryPathDecoder
{
public:
    template <std::size_t OutCapacity>
    static wrenium::geo::Error decode(const std::uint8_t *data, std::size_t byteCount, wrenium::geo::Buffer<char, OutCapacity> &out)
    {
        out.clear();

        constexpr std::size_t headerSize = sizeof(wrenium::geo::PathBinaryHeader);
        if (byteCount < headerSize) {
            return wrenium::geo::Error::TruncatedData;
        }

        const std::uint32_t magic = readU32LE(data);
        const std::uint32_t version = readU32LE(data + 4);
        const std::uint32_t elementCount = readU32LE(data + 8);

        if (magic != wrenium::geo::kPathBinaryMagic || version != wrenium::geo::kPathBinaryVersion) {
            return wrenium::geo::Error::UnrecognizedFormat;
        }

        const std::size_t payloadBytes = static_cast<std::size_t>(elementCount) * 4;
        if (byteCount < headerSize + payloadBytes) {
            return wrenium::geo::Error::TruncatedData;
        }

        const std::size_t streamEnd = headerSize + payloadBytes;
        std::size_t cursor = headerSize;
        bool ringOpen = false;

        while (cursor < streamEnd) {
            const float tag = readFloatLE(data + cursor);
            cursor += 4;

            if (tag == Commands::MoveTo || tag == Commands::LineTo) {
                if (cursor + 8 > streamEnd) {
                    return wrenium::geo::Error::TruncatedData;
                }
                const float x = readFloatLE(data + cursor);
                cursor += 4;
                const float y = readFloatLE(data + cursor);
                cursor += 4;

                wrenium::geo::Error err = out.pushBack(tag == Commands::MoveTo ? 'M' : 'L');
                if (err != wrenium::geo::Error::Ok) {
                    return err;
                }
                err = out.pushBack(' ');
                if (err != wrenium::geo::Error::Ok) {
                    return err;
                }
                err = wrenium::geo::appendFixedFloat(out, x);
                if (err != wrenium::geo::Error::Ok) {
                    return err;
                }
                err = out.pushBack(',');
                if (err != wrenium::geo::Error::Ok) {
                    return err;
                }
                err = wrenium::geo::appendFixedFloat(out, y);
                if (err != wrenium::geo::Error::Ok) {
                    return err;
                }
                err = out.pushBack(' ');
                if (err != wrenium::geo::Error::Ok) {
                    return err;
                }
                ringOpen = true;
            } else if (tag == Commands::ClosePath) {
                if (!ringOpen) {
                    return wrenium::geo::Error::MalformedStream;
                }
                wrenium::geo::Error err = out.pushBack('Z');
                if (err != wrenium::geo::Error::Ok) {
                    return err;
                }
                err = out.pushBack(' ');
                if (err != wrenium::geo::Error::Ok) {
                    return err;
                }
                ringOpen = false;
            } else {
                return wrenium::geo::Error::MalformedStream;
            }
        }

        return wrenium::geo::Error::Ok;
    }

private:
    static std::uint32_t readU32LE(const std::uint8_t *bytes)
    {
        std::uint32_t value;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }

    static float readFloatLE(const std::uint8_t *bytes)
    {
        const std::uint32_t bits = readU32LE(bytes);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
};

} // namespace BinaryPathDecoderExample

#endif // WRENIUM_GEO_COMMON_BINARY_PATH_DECODER_H
