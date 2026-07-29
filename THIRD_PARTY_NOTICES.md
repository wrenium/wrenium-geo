# Third-party notices

This project is licensed under the MIT License (see LICENSE.md). It also
incorporates or references the following third-party works.

## d3-geo (ported algorithms)

`include/wrenium/geo/detail/azimuthal/clip.h` ports the small-circle clip/rejoin algorithm from
d3-geo's `src/clip/circle.js` + `src/clip/rejoin.js`, and the point-in-polygon
test in `isCenterEnclosedByRings()` from `src/polygonContains.js`.

    d3-geo
    Copyright 2010-2024 Mike Bostock
    https://github.com/d3/d3-geo

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

## doctest (test dependency)

Fetched at configure time (CMake FetchContent, tag v2.5.3), not vendored.
Used only by the test suite (`tests/`), not by the library itself.

    doctest
    Copyright (c) 2016-2023 Viktor Kirilov
    MIT License -- https://github.com/doctest/doctest

## world-atlas (topojson2bin test fixtures / expected input format)

`tools/wrenium_geo_convert/data/land-110m.json` and `countries-110m.json`
(used by the converter's test suite) are world-atlas's own TopoJSON files.
Coastline/border data embedded in any wrenium-geo demo app via
`topojson2bin` is derived from the same source.

    world-atlas
    Copyright 2013-2019 Michael Bostock
    https://github.com/topojson/world-atlas

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

world-atlas itself packages data from Natural Earth (public domain;
no attribution required, credited here as a courtesy):

    Made with Natural Earth. Free vector and raster map data @
    naturalearthdata.com.

## nlohmann/json (topojson2bin build dependency)

Fetched at configure time (CMake FetchContent, tag v3.11.3), not vendored.

    JSON for Modern C++
    Copyright (c) 2013-2022 Niels Lohmann
    MIT License -- https://github.com/nlohmann/json

## Qt (examples/azimuthmap, demos/rotator, demos/radar -- dynamically linked, LGPLv3)

These three apps link dynamically against Qt (QtQuick, QtQuick.Controls,
QtQuick.Layouts, QtQuick.Shapes), used unmodified under the GNU Lesser
General Public License v3.0. Per the LGPLv3's obligations for dynamic
linking:

- This repository's own source may remain under any license (MIT here);
  the LGPLv3 does not extend to it.
- Qt itself is not modified or vendored -- it is located and linked at
  build time via the system/SDK Qt installation.
- Users are free to substitute a different (e.g. modified) build of Qt and
  relink any of these apps against it, as normal dynamic linking allows.
- The complete LGPLv3 license text (which incorporates the GNU GPLv3 by
  reference) is included at `third_party_licenses/LGPL-3.0-only.txt` and
  `third_party_licenses/GPL-3.0-only.txt` -- kept outside `LICENSES/`
  since that REUSE-specified directory is for licenses that tag files in
  this repository, and no file here is itself GPL/LGPL-licensed (Qt is
  linked dynamically at build time, never vendored).

    Qt
    Copyright (C) The Qt Company Ltd. and other contributors
    https://www.qt.io/licensing
    LGPL v3.0 -- https://www.gnu.org/licenses/lgpl-3.0.html
