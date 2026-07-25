# AGENTS.md

wrenium-geo: a header-only C++17 geometry library (`include/wrenium/geo/`)
plus Qt Quick demos/examples that exercise it. See `README.md` for the
project overview and usage guide -- this file covers things an agent
would otherwise have to rediscover the hard way.

## Setup commands

```sh
cmake -S . -B build                                        # library + topojson2bin only
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.x   # + Qt apps
cmake --build build --target tests topojson2bin_tests examples demos
ctest --test-dir build --output-on-failure
```

## After any source change

1. Rebuild + `ctest --test-dir build --output-on-failure`.
2. Headless-run any touched Qt app: `QT_QPA_PLATFORM=offscreen timeout 3
   build/<app>/<app>`. Exit 124 = expected timeout (still running fine
   when killed), not a failure.
3. `rm -rf docs/api && doxygen Doxyfile 2>&1 | grep -i warning` --
   expect nothing. A warning means a public doc comment is broken (see
   the `e.g.` gotcha below), not just cosmetic.
4. `clang-format --dry-run --Werror` over every `.h`/`.cpp` outside
   `build/` and `extern/`.

## Code conventions

- `include/wrenium/geo/` is header-only, zero-heap, and unconditionally
  free of exceptions/RTTI -- not behind a flag. Fallible operations
  return `Error` (`error.h`), never throw.
- `include/wrenium/geo/detail/` is implementation-only (excluded from
  Doxygen). Anything a caller must name to call a public function
  belongs outside `detail/`, fully documented.
- `include/wrenium/geo/` must never mention Qt, QML, or MCU-specific
  examples -- it has non-Qt, non-embedded consumers too. `common/`,
  `examples/`, `demos/` are Qt-specific and may reference Qt freely.
- Keep sibling names symmetric: `computeCoastlineSvgPath` /
  `computeBorderSvgPath`, not `computeSvgPath` / `computeBorderSvgPath`
  -- an unqualified name next to a qualified one misreads as "the
  general case."
- Comments describe the code as it is: no narrating what it "used to"
  look like, no stating what's already obvious from the file's
  location (e.g. "not part of the library" inside `demos/`), no
  self-references to "this repository" or CI.

## Doxygen

- `JAVADOC_AUTOBRIEF` is on, and Doxygen ends a `///` brief at the
  first `. ` -- including inside `e.g.`/`i.e.`, silently truncating
  the sentence ("e." ends it). Write "for example"/"that is" instead.
  Plain `//` comments and Markdown files are unaffected.
- To hide a necessarily-public member from generated docs, use
  `@cond WRENIUM_GEO_INTERNAL` / `@endcond` -- `@internal` alone does
  *not* remove it from the listing, only its description text.
- `README.md` is also the Doxygen main page. Keep it project-overview
  level; put per-type/per-function detail in the doc comments
  themselves, not duplicated in both places.
