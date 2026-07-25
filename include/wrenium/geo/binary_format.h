// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

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
struct PathCommands
{
    static constexpr float MoveTo = 0.0f;    ///< Starts a new subpath at the point that follows.
    static constexpr float LineTo = 1.0f;    ///< Draws a straight segment to the point that follows.
    static constexpr float ClosePath = 2.0f; ///< Closes the current subpath back to its MoveTo; no point follows.
};

} // namespace wrenium::geo
