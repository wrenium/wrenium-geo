// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cstring>

#include "doctest/doctest.h"

#include "wrenium/geo/cylindrical_pipeline.h"
#include "wrenium/geo/cylindrical_svg.h"
#include "wrenium/geo/svg_emitter.h"

using namespace wrenium::geo;
using namespace wrenium::geo::cylindrical;

namespace {

InputGeometry<16, 4> makeSquareRing()
{
    InputGeometry<16, 4> input;
    const GeoPoint square[] = {
        makeGeoPoint(10.0f, 10.0f),
        makeGeoPoint(10.0f, 20.0f),
        makeGeoPoint(20.0f, 20.0f),
        makeGeoPoint(20.0f, 10.0f),
    };
    for (const GeoPoint &p : square) {
        input.points.pushBack(p);
    }
    input.ringSizes.pushBack(4);
    return input;
}

InputGeometry<16, 4> makeOpenLine()
{
    InputGeometry<16, 4> input;
    const GeoPoint points[] = {
        makeGeoPoint(10.0f, 10.0f),
        makeGeoPoint(15.0f, 15.0f),
        makeGeoPoint(20.0f, 20.0f),
    };
    for (const GeoPoint &p : points) {
        input.points.pushBack(p);
    }
    input.ringSizes.pushBack(3);
    return input;
}

} // namespace

TEST_CASE("cylindrical::projectRingsToSvg matches projectRings + emitSvgPath called separately")
{
    const InputGeometry<16, 4> input = makeSquareRing();
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);

    Workspace<16, 4> manual;
    Buffer<char, 256> manualSvgPath;
    const Error manualPipelineErr = projectRings(manual, input, center, 1.0f);
    REQUIRE(manualPipelineErr == Error::Ok);
    const Error manualEmitErr = emitSvgPath(manual.projectedPoints(), manual.projectedRingSizes().data(), manual.projectedRingSizes().size(), manualSvgPath);
    REQUIRE(manualEmitErr == Error::Ok);

    Workspace<16, 4> oneCall;
    Buffer<char, 256> oneCallSvgPath;
    const Error err = projectRingsToSvg(oneCall, oneCallSvgPath, input, center, 1.0f);
    REQUIRE(err == Error::Ok);

    REQUIRE(oneCallSvgPath.size() == manualSvgPath.size());
    CHECK(std::memcmp(oneCallSvgPath.data(), manualSvgPath.data(), manualSvgPath.size()) == 0);
}

TEST_CASE("cylindrical::projectLinesToSvg matches projectLines + emitSvgLinePath called separately")
{
    const InputGeometry<16, 4> input = makeOpenLine();
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);

    Workspace<16, 4> manual;
    Buffer<char, 256> manualSvgPath;
    const Error manualPipelineErr = projectLines(manual, input, center, 1.0f);
    REQUIRE(manualPipelineErr == Error::Ok);
    const Error manualEmitErr = emitSvgLinePath(manual.projectedPoints(), manual.projectedRingSizes().data(), manual.projectedRingSizes().size(), manualSvgPath);
    REQUIRE(manualEmitErr == Error::Ok);

    Workspace<16, 4> oneCall;
    Buffer<char, 256> oneCallSvgPath;
    const Error err = projectLinesToSvg(oneCall, oneCallSvgPath, input, center, 1.0f);
    REQUIRE(err == Error::Ok);

    REQUIRE(oneCallSvgPath.size() == manualSvgPath.size());
    CHECK(std::memcmp(oneCallSvgPath.data(), manualSvgPath.data(), manualSvgPath.size()) == 0);
}

TEST_CASE("cylindrical::projectRingsToSvg propagates a pipeline capacity failure without calling emitSvgPath")
{
    const InputGeometry<16, 4> input = makeSquareRing();
    const GeoPoint center = makeGeoPoint(0.0f, 0.0f);

    // MaxPoints=1 can't hold the 4-point square -- projectRings itself
    // must fail, so projectRingsToSvg should return that same error
    // without ever reaching emitSvgPath.
    Workspace<1, 4> tooSmall;
    Buffer<char, 256> svgPath;
    const Error err = projectRingsToSvg(tooSmall, svgPath, input, center, 1.0f);
    CHECK(err == Error::CapacityExceeded);
    CHECK(svgPath.size() == 0);
}
