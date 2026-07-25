// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/error.h"
#include "wrenium/geo/float_format.h"
#include "wrenium/geo/point.h"

/// @file
/// Walks a projected ring/point representation (the pipeline's output --
/// see pipeline.h/workspace.h; a "ring" is defined in input_format.h) and
/// writes an SVG path `d` attribute string:
/// "M x,y L x,y ... Z " per ring. Even-odd fill is assumed by consumers
/// (SVG's `fill-rule="evenodd"`) -- no hole/outer ring tracking is needed
/// here.

namespace wrenium::geo {

/// Writes an SVG path `d` string for a set of closed rings.
/// @param points Flat array covering every ring back to back.
/// @param ringSizes Point count of ring i is `ringSizes[i]`; rings with
/// fewer than 2 points are skipped (nothing meaningful to draw).
/// @param ringCount Number of rings.
/// @param out Receives the path string (cleared first).
/// @return Error::Ok on success, or Error::CapacityExceeded if @p out is
/// too small.
template <std::size_t OutCapacity>
inline Error emitSvgPath(const Point *points, const std::size_t *ringSizes, std::size_t ringCount, Buffer<char, OutCapacity> &out)
{
    out.clear();

    std::size_t pointOffset = 0;
    for (std::size_t ringIndex = 0; ringIndex < ringCount; ++ringIndex) {
        const std::size_t ringSize = ringSizes[ringIndex];
        if (ringSize < 2) {
            pointOffset += ringSize;
            continue;
        }

        for (std::size_t i = 0; i < ringSize; ++i) {
            const Point &p = points[pointOffset + i];

            Error err = out.pushBack(i == 0 ? 'M' : 'L');
            if (err != Error::Ok) {
                return err;
            }
            err = out.pushBack(' ');
            if (err != Error::Ok) {
                return err;
            }
            err = appendFixedFloat(out, p.x);
            if (err != Error::Ok) {
                return err;
            }
            err = out.pushBack(',');
            if (err != Error::Ok) {
                return err;
            }
            err = appendFixedFloat(out, p.y);
            if (err != Error::Ok) {
                return err;
            }
            err = out.pushBack(' ');
            if (err != Error::Ok) {
                return err;
            }
        }

        Error err = out.pushBack('Z');
        if (err != Error::Ok) {
            return err;
        }
        err = out.pushBack(' ');
        if (err != Error::Ok) {
            return err;
        }

        pointOffset += ringSize;
    }

    return Error::Ok;
}

/// Border-line counterpart to emitSvgPath(): walks the *open* polyline run
/// output of @ref projectLines() (pipeline.h) and writes "M x,y L x,y ..."
/// per run -- deliberately never appending a "Z" close command, since a
/// border segment has no fill and closing it would draw a spurious chord
/// straight back to its own start. Render with fillColor: "transparent"
/// (or "none") and only a visible stroke.
/// @param points Flat array covering every polyline run back to back.
/// @param runSizes Point count of run i is `runSizes[i]`; runs with fewer
/// than 2 points are skipped.
/// @param runCount Number of independent polyline runs (entries in @p
/// runSizes) making up @p points.
/// @param out Receives the path string (cleared first).
/// @return Error::Ok on success, or Error::CapacityExceeded if @p out is
/// too small.
template <std::size_t OutCapacity>
inline Error emitSvgLinePath(const Point *points, const std::size_t *runSizes, std::size_t runCount, Buffer<char, OutCapacity> &out)
{
    out.clear();

    std::size_t pointOffset = 0;
    for (std::size_t runIndex = 0; runIndex < runCount; ++runIndex) {
        const std::size_t runSize = runSizes[runIndex];
        if (runSize < 2) {
            pointOffset += runSize;
            continue;
        }

        for (std::size_t i = 0; i < runSize; ++i) {
            const Point &p = points[pointOffset + i];

            Error err = out.pushBack(i == 0 ? 'M' : 'L');
            if (err != Error::Ok) {
                return err;
            }
            err = out.pushBack(' ');
            if (err != Error::Ok) {
                return err;
            }
            err = appendFixedFloat(out, p.x);
            if (err != Error::Ok) {
                return err;
            }
            err = out.pushBack(',');
            if (err != Error::Ok) {
                return err;
            }
            err = appendFixedFloat(out, p.y);
            if (err != Error::Ok) {
                return err;
            }
            err = out.pushBack(' ');
            if (err != Error::Ok) {
                return err;
            }
        }

        pointOffset += runSize;
    }

    return Error::Ok;
}

} // namespace wrenium::geo
