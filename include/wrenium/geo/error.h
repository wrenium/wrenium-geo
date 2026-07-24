// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#ifndef WRENIUM_GEO_ERROR_H
#define WRENIUM_GEO_ERROR_H

namespace wrenium::geo {

/// No exceptions anywhere in this library -- every fallible operation
/// returns one of these instead.
enum class Error
{
    Ok = 0,
    CapacityExceeded,     ///< A fixed-capacity Buffer/Workspace ran out of room
                          ///< for another element. Fix: increase that
                          ///< Buffer's/Workspace's capacity template parameter.
    TooManyClipCrossings, ///< A single ring crossed the clip boundary more
                          ///< times than the pipeline's fixed tracking limit
                          ///< allows (see detail/azimuthal/clip.h). Not
                          ///< adjustable by the caller -- indicates a
                          ///< pathologically self-crossing input ring.
    UnrecognizedFormat,   ///< A wire stream's magic number or version didn't
                          ///< match what the parser expects. Fix: pass a
                          ///< stream produced by this library's matching
                          ///< encoder/converter version.
    TruncatedData,        ///< A wire stream ended before its header, or a
                          ///< structure the header describes, was fully read.
                          ///< Fix: pass the complete stream and its true byte
                          ///< count.
    MalformedStream,      ///< A wire stream's content violates the format's
                          ///< own structure -- an unrecognized command tag,
                          ///< or a close with no matching open. Fix: pass a
                          ///< stream produced by this library's own encoder,
                          ///< unmodified.
};

} // namespace wrenium::geo

#endif // WRENIUM_GEO_ERROR_H
