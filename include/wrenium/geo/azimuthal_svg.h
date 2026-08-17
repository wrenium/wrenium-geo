// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "wrenium/geo/azimuthal_pipeline.h"
#include "wrenium/geo/svg_emitter.h"

/// @file
/// projectRingsToSvg()/projectLinesToSvg(): the rotate -> clip -> project
/// pipeline (azimuthal_pipeline.h) plus SVG emission (svg_emitter.h) in
/// one call. Its own header, not folded into azimuthal_pipeline.h itself,
/// so a caller who only wants the raw projected points doesn't pull in
/// svg_emitter.h.

namespace wrenium::geo::azimuthal {

/// Projects @p input's rings and writes the result as SVG path text into
/// @p svgPath -- projectRings() (azimuthal_pipeline.h) followed by
/// emitSvgPath() (svg_emitter.h), in one call.
/// @return Error::Ok on success, or whatever error projectRings()/
/// emitSvgPath() themselves would have returned for the same inputs --
/// see each's own @return doc.
// See projectRings's identical parameter pair and NOLINT rationale
// (azimuthal_pipeline.h).
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t OutputCharCapacity, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectRingsToSvg(
    Workspace<MaxPoints, MaxRings, MaxRingPoints> &workspace,
    Buffer<char, OutputCharCapacity> &svgPath,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const GeoPoint &center,
    float clipRadiusRad,
    float scale,
    ProjectionType projectionType)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const Error pipelineErr = projectRings(workspace, input, center, clipRadiusRad, scale, projectionType);
    if (pipelineErr != Error::Ok) {
        return pipelineErr;
    }
    return emitSvgPath(workspace.projectedPoints(), workspace.projectedRingSizes().data(), workspace.projectedRingSizes().size(), svgPath);
}

/// Projects @p input's open runs and writes the result as SVG path text
/// into @p svgPath -- projectLines() (azimuthal_pipeline.h) followed by
/// emitSvgLinePath() (svg_emitter.h), in one call.
// See projectRingsToSvg's identical parameter pair and NOLINT rationale.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <std::size_t MaxPoints, std::size_t MaxRings, std::size_t MaxRingPoints, std::size_t OutputCharCapacity, std::size_t InputMaxPoints, std::size_t InputMaxRings>
inline Error projectLinesToSvg(
    Workspace<MaxPoints, MaxRings, MaxRingPoints> &workspace,
    Buffer<char, OutputCharCapacity> &svgPath,
    const InputGeometry<InputMaxPoints, InputMaxRings> &input,
    const GeoPoint &center,
    float clipRadiusRad,
    float scale,
    ProjectionType projectionType)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const Error pipelineErr = projectLines(workspace, input, center, clipRadiusRad, scale, projectionType);
    if (pipelineErr != Error::Ok) {
        return pipelineErr;
    }
    return emitSvgLinePath(workspace.projectedPoints(), workspace.projectedRingSizes().data(), workspace.projectedRingSizes().size(), svgPath);
}

} // namespace wrenium::geo::azimuthal
