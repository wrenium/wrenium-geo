// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "wrenium/geo/conic_pipeline.h"
#include "wrenium/geo/svg_emitter.h"

/// @file
/// projectRingsToSvg()/projectLinesToSvg(): the project pipeline
/// (conic_pipeline.h) plus SVG emission (svg_emitter.h) in one call, kept
/// in its own header so a caller who only wants the raw projected points
/// can skip pulling in svg_emitter.h -- same split as azimuthal_svg.h/
/// cylindrical_svg.h.

namespace wrenium::geo::conic {

/// Projects @p input's rings and writes the result as SVG path text into
/// @p svgPath -- projectRings() (conic_pipeline.h) followed by
/// emitSvgPath() (svg_emitter.h), in one call.
/// @return Error::Ok on success, or whatever error projectRings()/
/// emitSvgPath() themselves would have returned for the same inputs --
/// see each's own @return doc.
// See projectRings's identical parameter pair and NOLINT rationale
// (conic_pipeline.h).
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t OutputCharCapacity, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectRingsToSvg(
    Workspace<MaxPoints, MaxRings, MaxRingPoints> &workspace,
    Buffer<char, OutputCharCapacity> &svgPath,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const LambertConformalConicFrame &frame,
    float scale,
    float clipLatRad = kPi,
    float clipLonRad = kPi)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const Error pipelineErr = projectRings(workspace, input, frame, scale, clipLatRad, clipLonRad);
    if (pipelineErr != Error::Ok) {
        return pipelineErr;
    }
    return emitSvgPath(workspace.projectedPoints(), workspace.projectedRingSizes().data(), workspace.projectedRingSizes().size(), svgPath);
}

/// Projects @p input's open runs and writes the result as SVG path text
/// into @p svgPath -- projectLines() (conic_pipeline.h) followed by
/// emitSvgLinePath() (svg_emitter.h), in one call.
// See projectRingsToSvg's identical parameter pair and NOLINT rationale.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t OutputCharCapacity, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectLinesToSvg(
    Workspace<MaxPoints, MaxRings, MaxRingPoints> &workspace,
    Buffer<char, OutputCharCapacity> &svgPath,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const LambertConformalConicFrame &frame,
    float scale,
    float clipLatRad = kPi,
    float clipLonRad = kPi)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const Error pipelineErr = projectLines(workspace, input, frame, scale, clipLatRad, clipLonRad);
    if (pipelineErr != Error::Ok) {
        return pipelineErr;
    }
    return emitSvgLinePath(workspace.projectedPoints(), workspace.projectedRingSizes().data(), workspace.projectedRingSizes().size(), svgPath);
}

} // namespace wrenium::geo::conic
