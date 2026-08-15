// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>
#include <string>

#include "doctest/doctest.h"

#include "wrenium/geo/azimuthal_constexpr_svg.h"
#include "wrenium/geo/detail/azimuthal/rotation.h"
#include "wrenium/geo/geo_point.h"

using namespace wrenium::geo;
using namespace wrenium::geo::azimuthal;

namespace {

bool approxEqual(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

// A small fixed ring (a diamond) around a fixed center -- every value here
// is a compile-time constant, which is the whole point of this test: none
// of this is read from a file or computed from a runtime argument.
constexpr GeoPoint kCenter = makeGeoPoint(28.6f, 17.2f);
constexpr float kClipRadiusRad = 0.5f;
constexpr float kScale = 40.0f;

constexpr GeoPoint kDiamond[4] = {
    makeGeoPoint(29.6f, 17.2f),
    makeGeoPoint(28.6f, 18.2f),
    makeGeoPoint(27.6f, 17.2f),
    makeGeoPoint(28.6f, 16.2f),
};
constexpr std::size_t kDiamondRingSizes[1] = {4};

// The actual proof: assigning to a constexpr variable only compiles if
// projectRingsToSvgConstexpr() genuinely evaluated at compile time -- if
// anything inside it (rotate, project, the visibility test, Buffer,
// emitSvgPath, appendFixedFloat) ever stops being constexpr-capable, this
// line fails to compile, not fall back to running later. That's this
// function's own regression test, enforced by the compiler on every build.
constexpr auto kSvg = projectRingsToSvgConstexpr<4, 1, 128>(kDiamond, kDiamondRingSizes, 1, kCenter, kClipRadiusRad, kScale);

static_assert(kSvg.size() > 0, "compile-time projection produced no output");
static_assert(kSvg[0] == 'M', "path should open with a move command");

} // namespace

TEST_CASE("projectRingsToSvgConstexpr actually evaluates at compile time")
{
    // kSvg was already computed by the compiler, above -- this just makes
    // its content visible to a normal test run/failure message.
    const std::string svg(kSvg.data(), kSvg.size());
    CAPTURE(svg);
    CHECK(kSvg.size() > 0);
    CHECK(kSvg[0] == 'M');
    CHECK(kSvg[kSvg.size() - 2] == 'Z'); // "... Z " -- 'Z' then a trailing space
}

TEST_CASE("azimuthal::detail::rotateConstexpr agrees with the real runtime rotate()")
{
    // projectRingsToSvgConstexpr() already calls the real
    // azimuthal::projectEquidistant() directly (constexpr-capable since
    // projectEquidistant() itself doesn't need sqrt/asin), so the only
    // reimplemented piece left to check against the real pipeline is
    // rotateConstexpr() itself.
    constexpr float kToleranceRad = 1e-3f;

    for (const GeoPoint &raw : kDiamond) {
        const GeoPoint expectedRotated = rotate(raw, kCenter);
        const GeoPoint actualRotated = azimuthal::detail::rotateConstexpr(raw, kCenter);
        CAPTURE(raw.latRad);
        CAPTURE(raw.lonRad);
        CHECK(approxEqual(actualRotated.latRad, expectedRotated.latRad, kToleranceRad));
        CHECK(approxEqual(actualRotated.lonRad, expectedRotated.lonRad, kToleranceRad));
    }
}

TEST_CASE("azimuthal::detail::isVisibleConstexpr drops points outside the clip radius")
{
    // kDiamond sits well inside kClipRadiusRad; a point on the opposite
    // side of the sphere shouldn't survive.
    const GeoPoint farAway = makeGeoPoint(-28.6f, -162.8f);
    const GeoPoint rotated = azimuthal::detail::rotateConstexpr(farAway, kCenter);
    CHECK_FALSE(azimuthal::detail::isVisibleConstexpr(rotated, kClipRadiusRad));

    const GeoPoint rotatedNear = azimuthal::detail::rotateConstexpr(kDiamond[0], kCenter);
    CHECK(azimuthal::detail::isVisibleConstexpr(rotatedNear, kClipRadiusRad));
}
