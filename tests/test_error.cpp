// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <string>

#include "doctest/doctest.h"

#include "wrenium/geo/error.h"

using namespace wrenium::geo;

TEST_CASE("errorToString gives every enumerator its own non-empty message")
{
    const Error allErrors[] = {
        Error::Ok, Error::CapacityExceeded, Error::TooManyClipCrossings,
        Error::UnrecognizedFormat, Error::TruncatedData, Error::MalformedStream,
        Error::InvalidParameter};

    for (const Error error : allErrors) {
        const char *message = errorToString(error);
        REQUIRE(message != nullptr);
        CHECK(message[0] != '\0');

        // Every other enumerator's message must differ from this one --
        // guards against a copy-pasted case body returning the wrong string.
        for (const Error other : allErrors) {
            if (other != error) {
                CHECK(std::string(errorToString(other)) != message);
            }
        }
    }
}
