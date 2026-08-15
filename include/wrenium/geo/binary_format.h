// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>
#include <cstdint>

/// @file
/// Output binary path format (secondary emitter output, produced by
/// binary_emitter.h). Decoding it back is left to the consumer -- for
/// example by drawing the MoveTo/LineTo/ClosePath commands directly with a
/// custom renderer, or by reconstructing an SVG path string (see
/// common/binary_path_decoder_example for a reference decoder).
///
/// Wire layout (little-endian, always):
///   PathBinaryHeader
///   elementCount x float   (command tags and coordinate values, homogeneous)
///
/// Command tags are small sequential integers. Exact float equality against
/// these tags is safe: IEEE-754 represents small integers exactly, and the
/// tags are produced directly, never through arithmetic that could round.
///
/// Only MoveTo/LineTo/ClosePath exist -- no relative (m/l) variants and no
/// curve commands (the pipeline only ever emits straight-line segments).

namespace wrenium::geo {

/// Identifies this library's binary path wire format -- check it before
/// parsing anything else, so mismatched or corrupted data is rejected
/// immediately rather than partially decoded. ASCII "WPB1".
constexpr std::uint32_t kPathBinaryMagic = 0x57504231;
/// Wire format version for #kPathBinaryMagic -- a decoder should reject
/// any stream whose version doesn't match exactly.
constexpr std::uint32_t kPathBinaryVersion = 1;

/// Fixed-size header at the start of every binary path stream.
struct PathBinaryHeader
{
    std::uint32_t magic = kPathBinaryMagic;     ///< See #kPathBinaryMagic.
    std::uint32_t version = kPathBinaryVersion; ///< See #kPathBinaryVersion.
    std::uint32_t elementCount = 0;             ///< Total floats that follow (tags + coordinates).
};

/// Default command tag values -- compile-time configurable via the Commands
/// template parameter on BinaryPathEmitter (and mirrored by any decoder
/// written against this format).
struct PathCommand
{
    static constexpr float MoveTo = 0.0f;    ///< Starts a new subpath at the point that follows.
    static constexpr float LineTo = 1.0f;    ///< Draws a straight segment to the point that follows.
    static constexpr float ClosePath = 2.0f; ///< Closes the current subpath back to its MoveTo; no point follows.
};

/// The exact byte capacity BinaryPathEmitter::encode() (binary_emitter.h)
/// needs for up to @p maxPoints points across up to @p maxRings closed
/// rings. Unlike SVG text (svgOutputCharCapacityForRings(), float_format.h),
/// this has no coordinate-magnitude dependency at all -- every element is
/// a fixed 4-byte float regardless of value -- so it's an exact byte
/// count, not just a tight bound.
/// @param maxPoints See Workspace's own MaxPoints.
/// @param maxRings See Workspace's own MaxRings.
constexpr std::size_t binaryOutputByteCapacityForRings(std::size_t maxPoints, std::size_t maxRings)
{
    // 3 floats/point (tag + x + y) + 1 float/ring (the ClosePath tag) --
    // see BinaryPathEmitter::encode's own walk.
    return sizeof(PathBinaryHeader) + (maxPoints * 3 + maxRings) * sizeof(float);
}

/// Same as #binaryOutputByteCapacityForRings, for
/// LineBinaryPathEmitter::encode()'s (binary_emitter.h) open-polyline
/// output -- no run-count parameter, unlike its SVG counterpart
/// (svgOutputCharCapacityForLines()): an open run has no per-run overhead
/// at all in the binary format (no ClosePath tag ever written), so the
/// byte count depends only on the point count.
/// @param maxPoints See Workspace's own MaxPoints.
constexpr std::size_t binaryOutputByteCapacityForLines(std::size_t maxPoints)
{
    return sizeof(PathBinaryHeader) + (maxPoints * 3) * sizeof(float);
}

} // namespace wrenium::geo
