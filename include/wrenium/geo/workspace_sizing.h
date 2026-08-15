// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>

#include "wrenium/geo/float_format.h"

/// @file
/// One-call version of workspace.h's own "Sizing a Workspace" guide, for a
/// single Workspace shared across a closed-ring dataset (coastlines, for
/// example) and an open-polyline dataset (borders, for example) -- each
/// capacity sized for whichever of the two needs more of it, since a
/// shared Workspace draws both through the same buffers.

namespace wrenium::geo {

/// Workspace (workspace.h) capacities computed by sharedWorkspaceSizeFor(),
/// below -- pass directly as its MaxPoints/MaxRings/OutputCharCapacity
/// template arguments.
struct SharedWorkspaceSize
{
    std::size_t maxPoints;
    std::size_t maxRings;
    std::size_t outputCharCapacity;
};

/// Computes a shared Workspace's MaxPoints/MaxRings/OutputCharCapacity from
/// two datasets' own generated Info structs (topojson2bin, see
/// tools/wrenium_geo_convert's own README section): the closed-ring
/// dataset needs the bigger point margin (clipping can synthesize extra
/// points closing a cut ring, "arc bridging" -- see workspace.h's own
/// sizing guide), the open-polyline dataset needs a smaller point margin
/// but can have the bigger ring/run count, so each capacity takes
/// whichever dataset needs more of it.
/// @tparam RingDatasetInfo Any type with pointCount/ringCount members --
/// the closed-ring dataset's own generated Info struct.
/// @tparam LineDatasetInfo Same, for the open-polyline dataset.
/// @param ringInfo The closed-ring dataset's own generated Info struct.
/// @param lineInfo The open-polyline dataset's own generated Info struct.
/// @param maxViewportPx The largest coordinate your output can ever
/// reach -- see svgOutputCharCapacityForRings()'s identical parameter
/// (float_format.h).
/// @param pointMargin Extra points reserved for the ring dataset's own
/// arc bridging.
/// @param ringMargin Extra rings/runs reserved for either dataset.
/// @param lineMargin Extra points reserved for the line dataset's own
/// clip-crossing growth -- smaller than pointMargin since an open run can
/// only gain a couple of points at a crossing, never a whole arc's worth.
/// @return maxPoints/maxRings/outputCharCapacity, ready to pass as
/// Workspace's own MaxPoints/MaxRings/OutputCharCapacity template
/// arguments -- its MaxRingPoints can keep its own default (always safe,
/// see Workspace's own comment).
// maxViewportPx/pointMargin/ringMargin/lineMargin are documented and
// always passed in this order -- reordering one alone would be its own
// hazard.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <typename RingDatasetInfo, typename LineDatasetInfo>
constexpr SharedWorkspaceSize sharedWorkspaceSizeFor(
    const RingDatasetInfo &ringInfo,
    const LineDatasetInfo &lineInfo,
    float maxViewportPx,
    std::size_t pointMargin = 1000,
    std::size_t ringMargin = 50,
    std::size_t lineMargin = 200)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const std::size_t ringPoints = ringInfo.pointCount + pointMargin;
    const std::size_t linePoints = lineInfo.pointCount + lineMargin;
    const std::size_t maxPoints = ringPoints > linePoints ? ringPoints : linePoints;

    const std::size_t ringRings = ringInfo.ringCount + ringMargin;
    const std::size_t lineRuns = lineInfo.ringCount + ringMargin;
    const std::size_t maxRings = ringRings > lineRuns ? ringRings : lineRuns;

    const std::size_t ringChars = svgOutputCharCapacityForRings(maxPoints, maxRings, maxViewportPx);
    const std::size_t lineChars = svgOutputCharCapacityForLines(maxPoints, maxRings, maxViewportPx);

    return SharedWorkspaceSize{maxPoints, maxRings, ringChars > lineChars ? ringChars : lineChars};
}

} // namespace wrenium::geo
