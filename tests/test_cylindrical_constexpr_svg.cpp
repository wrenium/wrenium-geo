// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <algorithm>
#include <string>

#include "doctest/doctest.h"

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/cylindrical_constexpr_svg.h"
#include "wrenium/geo/detail/cylindrical/mercator.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/svg_emitter.h"

using namespace wrenium::geo;
using namespace wrenium::geo::cylindrical;

namespace {

// Every value here is a compile-time constant -- the whole point of this
// test: none of this is read from a file or computed from a runtime
// argument.
constexpr GeoPoint kCenter = makeGeoPoint(0.0f, 0.0f);
constexpr float kScale = 1.0f;

constexpr GeoPoint kSquare[4] = {
    makeGeoPoint(10.0f, 10.0f),
    makeGeoPoint(10.0f, 20.0f),
    makeGeoPoint(20.0f, 20.0f),
    makeGeoPoint(20.0f, 10.0f),
};
constexpr std::size_t kSquareRingSizes[1] = {4};

// The actual proof: assigning to a constexpr variable only compiles if
// projectRingsToSvgConstexpr() genuinely evaluated at compile time -- if
// project()/emitSvgPath()/appendFixedFloat() ever stopped being
// constexpr-capable, this line fails to compile, not fall back to
// running later. That's this function's own regression test, enforced by
// the compiler on every build.
constexpr auto kSvg = projectRingsToSvgConstexpr<4, 1, 128>(kSquare, kSquareRingSizes, 1, kCenter, kScale);

static_assert(kSvg.size() > 0, "compile-time projection produced no output");
static_assert(kSvg[0] == 'M', "path should open with a move command");

} // namespace

TEST_CASE("cylindrical::projectRingsToSvgConstexpr actually evaluates at compile time")
{
    // kSvg was already computed by the compiler, above -- this just makes
    // its content visible to a normal test run/failure message.
    const std::string svg(kSvg.data(), kSvg.size());
    CAPTURE(svg);
    CHECK(kSvg.size() > 0);
    CHECK(kSvg[0] == 'M');
    CHECK(kSvg[kSvg.size() - 2] == 'Z'); // "... Z " -- 'Z' then a trailing space
}

TEST_CASE("cylindrical::projectRingsToSvgConstexpr matches project()/emitSvgPath called manually")
{
    // No reimplementation here (unlike azimuthal's own constexpr
    // version) -- project() is called directly, so this confirms the
    // compile-time result is byte-identical to the same calls made at
    // runtime, not just "some" plausible-looking text.
    Buffer<Point, 4> points;
    for (const GeoPoint &p : kSquare) {
        points.pushBack(project(p, kCenter, kScale));
    }
    const std::size_t ringSizes[1] = {4};
    Buffer<char, 128> manual;
    const Error err = emitSvgPath(points.data(), ringSizes, 1, manual);
    REQUIRE(err == Error::Ok);

    REQUIRE(manual.size() == kSvg.size());
    CHECK(std::equal(manual.begin(), manual.end(), kSvg.begin()));
}
