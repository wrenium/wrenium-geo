// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cstring>

#include "doctest/doctest.h"

#include "wrenium/geo/azimuthal_pipeline.h"
#include "wrenium/geo/azimuthal_svg.h"
#include "wrenium/geo/svg_emitter.h"

using namespace wrenium::geo;
using namespace wrenium::geo::azimuthal;

namespace {

InputGeometry<16, 4> makeTriangleRing()
{
    InputGeometry<16, 4> input;
    input.points.pushBack(makeGeoPoint(1.0f, 0.0f));
    input.points.pushBack(makeGeoPoint(0.0f, 1.0f));
    input.points.pushBack(makeGeoPoint(-1.0f, 0.0f));
    input.ringSizes.pushBack(input.points.size());
    input.ringMinLat.pushBack(-1.0f);
    input.ringMaxLat.pushBack(1.0f);
    return input;
}

InputGeometry<16, 4> makeOpenLine()
{
    InputGeometry<16, 4> input;
    input.points.pushBack(makeGeoPoint(1.0f, 0.0f));
    input.points.pushBack(makeGeoPoint(0.0f, 1.0f));
    input.points.pushBack(makeGeoPoint(-1.0f, 0.0f));
    input.ringSizes.pushBack(input.points.size());
    input.ringMinLat.pushBack(-1.0f);
    input.ringMaxLat.pushBack(1.0f);
    return input;
}

} // namespace

TEST_CASE("projectRingsToSvg matches projectRings + emitSvgPath called separately")
{
    const InputGeometry<16, 4> input = makeTriangleRing();
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    constexpr float clipRadiusRad = 30.0f * kPi / 180.0f;
    constexpr float scale = 1.0f;

    Workspace<16, 4> manual;
    const Error manualPipelineErr = projectRings(manual, input, center, clipRadiusRad, scale, ProjectionType::Equidistant);
    REQUIRE(manualPipelineErr == Error::Ok);
    const Error manualEmitErr = emitSvgPath(manual.projectedPoints(), manual.projectedRingSizes().data(), manual.projectedRingSizes().size(), manual.svgPath);
    REQUIRE(manualEmitErr == Error::Ok);

    Workspace<16, 4> oneCall;
    const Error err = projectRingsToSvg(oneCall, input, center, clipRadiusRad, scale, ProjectionType::Equidistant);
    REQUIRE(err == Error::Ok);

    REQUIRE(oneCall.svgPath.size() == manual.svgPath.size());
    CHECK(std::memcmp(oneCall.svgPath.data(), manual.svgPath.data(), manual.svgPath.size()) == 0);
}

TEST_CASE("projectLinesToSvg matches projectLines + emitSvgLinePath called separately")
{
    const InputGeometry<16, 4> input = makeOpenLine();
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    constexpr float clipRadiusRad = 30.0f * kPi / 180.0f;
    constexpr float scale = 1.0f;

    Workspace<16, 4> manual;
    const Error manualPipelineErr = projectLines(manual, input, center, clipRadiusRad, scale, ProjectionType::Equidistant);
    REQUIRE(manualPipelineErr == Error::Ok);
    const Error manualEmitErr = emitSvgLinePath(manual.projectedPoints(), manual.projectedRingSizes().data(), manual.projectedRingSizes().size(), manual.svgPath);
    REQUIRE(manualEmitErr == Error::Ok);

    Workspace<16, 4> oneCall;
    const Error err = projectLinesToSvg(oneCall, input, center, clipRadiusRad, scale, ProjectionType::Equidistant);
    REQUIRE(err == Error::Ok);

    REQUIRE(oneCall.svgPath.size() == manual.svgPath.size());
    CHECK(std::memcmp(oneCall.svgPath.data(), manual.svgPath.data(), manual.svgPath.size()) == 0);
    // Never closed with a "Z" -- see emitSvgLinePath's own comment.
    CHECK(oneCall.svgPath[oneCall.svgPath.size() - 2] != 'Z');
}

TEST_CASE("projectRingsToSvg propagates a pipeline capacity failure without calling emitSvgPath")
{
    const InputGeometry<16, 4> input = makeTriangleRing();
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);
    constexpr float clipRadiusRad = 30.0f * kPi / 180.0f;

    // MaxPoints=1 can't hold the 3-point triangle -- projectRings itself
    // must fail, so projectRingsToSvg should return that same error
    // without ever reaching emitSvgPath.
    Workspace<1, 4> tooSmall;
    const Error err = projectRingsToSvg(tooSmall, input, center, clipRadiusRad, 1.0f, ProjectionType::Equidistant);
    CHECK(err == Error::CapacityExceeded);
    CHECK(tooSmall.svgPath.size() == 0);
}
