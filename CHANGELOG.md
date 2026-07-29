# Changelog

All notable changes to this project are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning follows
[Semantic Versioning](https://semver.org/).

## [0.3.0]

### Added
- Azimuthal orthographic projection (`detail/azimuthal/orthographic.h`) as a
  second radial-distance formula, selectable via `projectRings`/
  `projectLines`/`projectPoint`'s `ProjectFn` template parameter (defaults
  to equidistant, unchanged for existing callers).
- Runtime projection toggle in `WreniumGeoBridge` (`useOrthographic`
  parameter) and a UI toggle in `examples/azimuthmap`.

### Changed
- Trig backend switched to [wrenium-f32math](https://github.com/wrenium/wrenium-f32math)
  (fetched via CMake `FetchContent`), replacing raw `<cmath>` calls in the
  azimuthal rotation/projection code.
- `clipRingToSink` no longer redundantly re-rotates a ring's starting
  vertex (measured ~2.5-4.2% pipeline speedup).

### Fixed
- `ProjectedPoint::visible` now has a default member initializer (was
  previously left uninitialized on default construction).

## [0.2.3]
- Moved azimuthal-family code into a `wrenium::geo::azimuthal` namespace.
- Moved `PointStorage` into `wrenium::geo::detail`.

## [0.2.2]
- Unified `doctest` onto `FetchContent`, matching `nlohmann/json`.
- Clarified the binary path decoder is a reference example, not a shipped
  SVG decoder.

## [0.2.1]
### Changed
- `rotate()`'s bearing `atan2f` is now deferred until a point passes the
  clip test (`rotateBegin()`/`rotateFinish()` split).
### Fixed
- Dropped a redundant per-point `L` command from SVG path output.

## [0.2.0]
### Added
- `makeViewport()` helper.
### Fixed
- Broken/missing cross-reference links to `projectRings`/`projectLines`/
  `projectPoint`.
- Missing/unhelpful class-level descriptions.
### Changed
- Renamed `PathCommands` to `PathCommand`.

## [0.1.2]
### Fixed
- Inverted fill for rings whose coastline crosses a fixed reference
  bearing.

## [0.1.1]
### Added
- `CONTRIBUTING.md`.
- `-Wall -Wextra -Wpedantic` (`/W4` on MSVC) across all first-party
  targets.
### Changed
- All headers switched to `#pragma once`.
- `sizeof(Header)` used instead of a hardcoded `12` for the wire header
  size.

## [0.1.0]
### Added
- Initial release: core header-only library (rotate -> clip -> project
  pipeline, azimuthal equidistant projection), SVG and binary path
  emitters, `topojson2bin` converter tool, test suite, `examples/azimuthmap`,
  `demos/rotator`, `demos/radar`, CI, Doxygen API reference.

[0.3.0]: https://github.com/wrenium/wrenium-geo/compare/v0.2.3...main
[0.2.3]: https://github.com/wrenium/wrenium-geo/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/wrenium/wrenium-geo/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/wrenium/wrenium-geo/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/wrenium/wrenium-geo/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/wrenium/wrenium-geo/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/wrenium/wrenium-geo/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/wrenium/wrenium-geo/releases/tag/v0.1.0
