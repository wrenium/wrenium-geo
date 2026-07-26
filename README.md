# wrenium-geo

[API documentation](https://wrenium.github.io/wrenium-geo/)

A C++17 header-only geometry library that projects geographic coastline and
border data onto a 2D plane from an arbitrary center point -- currently via
azimuthal equidistant projection, with room for other projections later --
emitting either an SVG path string or a compact binary path stream. Built
for constrained environments: zero heap allocation, and no exceptions or
RTTI anywhere in the library.

![Example output from examples/azimuthmap](docs/azimuthmap-screenshot.png)

## Features

- **Produces map geometry centered on any point on Earth**, via a rotate ->
  clip -> project pipeline: rotates the sphere so that center point becomes
  the pole, clips coastline/border data down to a configurable radius
  around it, then projects what's left with a closed-form azimuthal
  equidistant formula (true distance and bearing from the center point are
  preserved exactly).
- **Fixed-capacity, zero-heap containers** throughout (`Buffer<T, Capacity>`),
  sized entirely at compile time via template parameters -- no
  `std::vector`, no dynamic allocation.
- **No exceptions or RTTI anywhere in the library**: every fallible
  operation reports failure via an `Error` enum instead; the test suite
  always compiles under `-fno-exceptions -fno-rtti` (see `tests/CMakeLists.txt`).
- **Two output formats**: an SVG path `d` string, or a compact tagged-float
  binary stream (magic + version header, little-endian) -- see
  `common/binary_path_decoder_example` for a reference decoder.
- **TopoJSON converter** (`topojson2bin`, in `tools/wrenium_geo_convert`) turns
  [world-atlas](https://github.com/topojson/world-atlas) TopoJSON data into
  the library's own binary input-geometry format, output as both a raw
  `.bin` file and a generated C++ header (a `static const uint8_t[]` array
  to `#include` directly).

## Terminology

- **Ring**: a closed polygon boundary (one landmass's coastline, or one island).
- **Run**: an independent *open* polyline (one border segment), as opposed to a closed ring.
- **Center**: the `GeoPoint` a projection is centered on.
- **Clip radius**: the angular radius (radians) around `center` kept in the output.
- **Scale**: output units per kilometer.

Full definitions live with the types/functions that use them -- see the
[API documentation](https://wrenium.github.io/wrenium-geo/) (`input_format.h`
for ring, `svg_emitter.h`/`binary_emitter.h` for run).

## Repository layout

```
include/wrenium/geo/          public headers (namespace wrenium::geo),
                             header-only
tests/                       doctest suite
tools/wrenium_geo_convert/    offline TopoJSON -> input-geometry converter
                             tool (builds as `topojson2bin`)
common/wrenium_geo_qt_bridge/ shared C++/QML bridge (WreniumGeoBridge) --
                             used by all three Qt Quick apps below, and
                             itself the reference example for wrapping the
                             library as a QML type
examples/azimuthmap/         minimal Qt Quick integration example (pan/zoom,
                             CLI screenshot mode) showing how to drive
                             WreniumGeoBridge from an app
demos/rotator/               antenna-rotator-controller-style showcase demo
demos/radar/                 PPI radar scope showcase demo
```

`examples/` and `demos/` are kept distinct on purpose: `examples/azimuthmap`
is a minimal reference for people *integrating* wrenium-geo into their own
app, while `demos/rotator` and `demos/radar` are full showcase applications
for people
*evaluating* it.

## TopoJSON converter

`topojson2bin` converts a TopoJSON file into wrenium-geo's binary
input-geometry format. Each run writes the same data out twice: a raw
`.bin` file, and a generated C++ header to `#include` directly.

`topojson2bin` expects **quantized TopoJSON** -- the default output of
tools like `topojson-server`/`mapshaper` (and what
[world-atlas](https://github.com/topojson/world-atlas) ships): arcs are
delta-encoded integers, recovered via a required top-level
`transform.scale`/`transform.translate`. Unquantized TopoJSON (plain
float arc coordinates, no `transform`) isn't supported.

Required top-level fields: `arcs`, `transform.scale`, `transform.translate`,
and `objects.<object-name>` for whichever object you name on the command
line. That named object must be a `Polygon`, `MultiPolygon`, or a
`GeometryCollection` of either:

- Without `--mesh`: decoded as closed coastline rings (e.g. world-atlas's
  `land-110m.json`, object `land`).
- With `--mesh`: the object is treated as a `GeometryCollection` of
  country-like features, and only the *interior* shared-arc borders
  between distinct features are emitted, as open polylines (e.g.
  world-atlas's `countries-110m.json`, object `countries`).

These two commands are exactly how the checked-in default data
(`common/wrenium_geo_qt_bridge/data/world_coastline_110m.h`,
`world_borders_110m.h`) was produced -- coastline without `--mesh`,
borders with it:

```sh
topojson2bin land-110m.json land coastline.bin coastline.h
topojson2bin --mesh countries-110m.json countries borders.bin borders.h
```

## Coordinate conventions

- **Input** (`GeoPoint`): latitude/longitude in radians.
- **Output** (`Point`): screen/SVG axes (y down), `(0, 0)` at `center` --
  translate by your own viewport's center to draw it.

Exact sign conventions and unit details are documented on `GeoPoint`/`Point`
themselves -- see the [API documentation](https://wrenium.github.io/wrenium-geo/).

## Using the library

wrenium-geo is header-only. Either add `include/` to your own include path
directly, or consume it as a CMake target:

```cmake
add_subdirectory(path/to/wrenium-geo) # or FetchContent_Declare + FetchContent_MakeAvailable
target_link_libraries(your_target PRIVATE Wrenium::geo)
```

Minimal usage: load a **coastline** dataset (closed rings) once, then
project it centered on an arbitrary point and emit an SVG path -- the same
three calls a caller re-runs on every pan/zoom, just with a different
`center`/`clipRadiusRad`.

```cpp
#include <wrenium/geo/input_format.h>
#include <wrenium/geo/pipeline.h>
#include <wrenium/geo/svg_emitter.h>
#include <wrenium/geo/workspace.h>

constexpr std::size_t kMaxPoints = 6000; // sized for the dataset being loaded
constexpr std::size_t kMaxRings = 200;

wrenium::geo::Workspace<kMaxPoints, kMaxRings> coastline;   // working buffers + output, reused across calls
wrenium::geo::InputGeometry<kMaxPoints, kMaxRings> coastlineInput;   // the loaded, unprojected coastline rings

// Parse the coastline data once (e.g. topojson2bin's generated header) --
// it doesn't change between recomputes below, only center/radius do.
wrenium::geo::loadInputGeometry(coastlineBytes, coastlineByteCount, coastlineInput);

// Where to center the map (Helsinki), and how much of the world around it
// to include and how large to draw it.
const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(60.0f, 25.0f);
const float clipRadiusKm = 2000.0f;    // how far from center to include
const float viewportRadiusPx = 400.0f; // that same radius, in output units
const float clipRadiusRad = clipRadiusKm / wrenium::geo::kEarthRadiusKm;
const float scale = viewportRadiusPx / clipRadiusKm; // output units per km

// Rotate -> clip -> project every coastline ring, writing the result into coastline.
wrenium::geo::projectRings(coastline, coastlineInput, center, clipRadiusRad, scale);

// Read the result back out as an SVG path `d` string.
wrenium::geo::emitSvgPath(coastline.projectedPoints(), coastline.projectedRingSizes().data(),
                           coastline.projectedRingSizes().size(), coastline.svgPath);

// coastline.svgPath now holds "M x,y L x,y ... Z" path data, ready to draw.
```

## Building

Requires CMake >= 3.21 and a C++17 compiler. Test/tool dependencies
(doctest, nlohmann/json) are fetched automatically at configure time --
no submodules to init.

```sh
git clone <repo-url>
cd wrenium-geo
cmake -S . -B build
cmake --build build
```

A plain build produces only the `topojson2bin` CLI tool (no Qt needed).
Everything else -- the test suite, and the Qt Quick apps
(`examples/azimuthmap`, `demos/rotator`, `demos/radar`, which require Qt >=
6.8) -- is excluded from the default `all` target so a plain build stays
fast. Build them by name or via their umbrella targets:

```sh
cmake --build build --target tests             # library test suite
cmake --build build --target topojson2bin_tests # converter test suite
ctest --test-dir build

cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.x/gcc_64
cmake --build build --target examples   # builds azimuthmap
cmake --build build --target demos      # builds rotator + radar

doxygen Doxyfile                        # API reference: docs/api/html/index.html
```

## License

MIT (see `LICENSE.md`). `detail/azimuthal/clip.h` ports algorithms from
[d3-geo](https://github.com/d3/d3-geo) (ISC), and the embedded/tested
coastline and border data is derived from
[world-atlas](https://github.com/topojson/world-atlas) (ISC), ultimately
from [Natural Earth](https://www.naturalearthdata.com/) (public domain). The
three Qt Quick apps link Qt dynamically under LGPLv3. See
`THIRD_PARTY_NOTICES.md` for full attributions.
