// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>
#include <cstdint>

#include "wrenium/geo/binary_format.h"
#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/byte_stream.h"
#include "wrenium/geo/error.h"
#include "wrenium/geo/point.h"

/// @file
/// Writes the pipeline's projected ring/point output (a "ring" is defined
/// in input_format.h) as the binary path stream: PathBinaryHeader followed
/// by elementCount floats (command tags + coordinate values, homogeneous).
/// Little-endian always.
///
/// Mirrors svg_emitter.h's ring-walk exactly (same skip-rings-under-2-points
/// rule, same point order) so the two are guaranteed to describe the same
/// geometry -- see common/binary_path_decoder_example for a reference
/// decoder back to SVG.

namespace wrenium::geo {

/// Encodes closed rings into the binary path stream (see binary_format.h).
/// @tparam Commands Command-tag values to write; defaults to PathCommand.
template <typename Commands = PathCommand>
class BinaryPathEmitter
{
public:
    /// @param points Flat array covering every ring back to back.
    /// @param ringSizes Point count of ring i is `ringSizes[i]`; rings with
    /// fewer than 2 points are skipped.
    /// @param ringCount Number of rings.
    /// @param out Receives the encoded bytes (cleared first).
    /// @return Error::Ok on success, or Error::CapacityExceeded if @p out
    /// is too small.
    template <std::size_t OutCapacity>
    static Error encode(const Point *points, const std::size_t *ringSizes, std::size_t ringCount, Buffer<std::uint8_t, OutCapacity> &out)
    {
        out.clear();

        const std::size_t elementCount = countElements(ringSizes, ringCount);

        Error err = detail::writeU32LE(out, kPathBinaryMagic);
        if (err != Error::Ok) {
            return err;
        }
        err = detail::writeU32LE(out, kPathBinaryVersion);
        if (err != Error::Ok) {
            return err;
        }
        err = detail::writeU32LE(out, static_cast<std::uint32_t>(elementCount));
        if (err != Error::Ok) {
            return err;
        }

        std::size_t pointOffset = 0;
        for (std::size_t ringIndex = 0; ringIndex < ringCount; ++ringIndex) {
            const std::size_t ringSize = ringSizes[ringIndex];
            if (ringSize < 2) {
                pointOffset += ringSize;
                continue;
            }

            for (std::size_t i = 0; i < ringSize; ++i) {
                const Point &p = points[pointOffset + i];

                err = detail::writeFloatLE(out, i == 0 ? Commands::MoveTo : Commands::LineTo);
                if (err != Error::Ok) {
                    return err;
                }
                err = detail::writeFloatLE(out, p.x);
                if (err != Error::Ok) {
                    return err;
                }
                err = detail::writeFloatLE(out, p.y);
                if (err != Error::Ok) {
                    return err;
                }
            }

            err = detail::writeFloatLE(out, Commands::ClosePath);
            if (err != Error::Ok) {
                return err;
            }

            pointOffset += ringSize;
        }

        return Error::Ok;
    }

private:
    static std::size_t countElements(const std::size_t *ringSizes, std::size_t ringCount)
    {
        std::size_t total = 0;
        for (std::size_t ringIndex = 0; ringIndex < ringCount; ++ringIndex) {
            const std::size_t ringSize = ringSizes[ringIndex];
            if (ringSize < 2) {
                continue;
            }
            total += ringSize * 3; // (tag + x + y) per point
            total += 1;            // ClosePath tag
        }
        return total;
    }
};

/// Border-line counterpart to BinaryPathEmitter: encodes the *open*
/// polyline run output of @ref projectLines() (pipeline.h) the same way (same
/// magic/version header, same MoveTo/LineTo tag stream), but never writes a
/// ClosePath tag after a run -- a border segment has no fill and closing it
/// would draw a spurious chord back to its own start.
/// @tparam Commands Command-tag values to write; defaults to PathCommand.
template <typename Commands = PathCommand>
class LineBinaryPathEmitter
{
public:
    /// @param points Flat array covering every polyline run back to back.
    /// @param runSizes Point count of run i is `runSizes[i]`; runs with
    /// fewer than 2 points are skipped.
    /// @param runCount Number of independent polyline runs (entries in @p
    /// runSizes) making up @p points.
    /// @param out Receives the encoded bytes (cleared first).
    /// @return Error::Ok on success, or Error::CapacityExceeded if @p out
    /// is too small.
    template <std::size_t OutCapacity>
    static Error encode(const Point *points, const std::size_t *runSizes, std::size_t runCount, Buffer<std::uint8_t, OutCapacity> &out)
    {
        out.clear();

        const std::size_t elementCount = countElements(runSizes, runCount);

        Error err = detail::writeU32LE(out, kPathBinaryMagic);
        if (err != Error::Ok) {
            return err;
        }
        err = detail::writeU32LE(out, kPathBinaryVersion);
        if (err != Error::Ok) {
            return err;
        }
        err = detail::writeU32LE(out, static_cast<std::uint32_t>(elementCount));
        if (err != Error::Ok) {
            return err;
        }

        std::size_t pointOffset = 0;
        for (std::size_t runIndex = 0; runIndex < runCount; ++runIndex) {
            const std::size_t runSize = runSizes[runIndex];
            if (runSize < 2) {
                pointOffset += runSize;
                continue;
            }

            for (std::size_t i = 0; i < runSize; ++i) {
                const Point &p = points[pointOffset + i];

                err = detail::writeFloatLE(out, i == 0 ? Commands::MoveTo : Commands::LineTo);
                if (err != Error::Ok) {
                    return err;
                }
                err = detail::writeFloatLE(out, p.x);
                if (err != Error::Ok) {
                    return err;
                }
                err = detail::writeFloatLE(out, p.y);
                if (err != Error::Ok) {
                    return err;
                }
            }

            pointOffset += runSize;
        }

        return Error::Ok;
    }

private:
    static std::size_t countElements(const std::size_t *runSizes, std::size_t runCount)
    {
        std::size_t total = 0;
        for (std::size_t runIndex = 0; runIndex < runCount; ++runIndex) {
            const std::size_t runSize = runSizes[runIndex];
            if (runSize < 2) {
                continue;
            }
            total += runSize * 3; // (tag + x + y) per point
        }
        return total;
    }
};

} // namespace wrenium::geo
