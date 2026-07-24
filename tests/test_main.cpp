// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

// Single doctest-owned main() for the whole `tests` binary -- every
// other .cpp in tests/ only defines TEST_CASE blocks.
