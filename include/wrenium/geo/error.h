// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

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
    InvalidParameter,     ///< A runtime argument was outside the range an
                          ///< operation is defined for -- checked
                          ///< explicitly rather than left to produce a
                          ///< degenerate result or loop indefinitely (a
                          ///< non-positive step size, for example). Fix:
                          ///< pass a value in the range that operation's
                          ///< own doc comment documents.
};

/// A short, human-readable description of @p error, for logs/diagnostics --
/// not meant to be shown to end users as-is. Every enumerator has a
/// distinct message (see Error's own doc comments above for the fix); an
/// unrecognized value (only reachable via an invalid enum cast) falls back
/// to a generic message rather than undefined behavior.
inline const char *errorToString(Error error)
{
    switch (error) {
    case Error::Ok:
        return "Ok";
    case Error::CapacityExceeded:
        return "CapacityExceeded: a fixed-capacity Buffer/Workspace ran out of room -- increase its capacity template parameter";
    case Error::TooManyClipCrossings:
        return "TooManyClipCrossings: a ring crossed the clip boundary more times than the pipeline's fixed tracking limit allows";
    case Error::UnrecognizedFormat:
        return "UnrecognizedFormat: a wire stream's magic number or version didn't match what the parser expects";
    case Error::TruncatedData:
        return "TruncatedData: a wire stream ended before its header or a structure the header describes was fully read";
    case Error::MalformedStream:
        return "MalformedStream: a wire stream's content violates the format's own structure";
    case Error::InvalidParameter:
        return "InvalidParameter: a runtime argument was outside the range that operation is defined for";
    }
    return "unrecognized Error value";
}

} // namespace wrenium::geo
