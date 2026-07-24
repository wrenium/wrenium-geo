// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#ifndef WRENIUM_GEO_QT_BRIDGE_WRENIUM_GEO_BRIDGE_H
#define WRENIUM_GEO_QT_BRIDGE_WRENIUM_GEO_BRIDGE_H

#include <cstddef>
#include <cstdint>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <qqmlintegration.h>

#include <wrenium/geo/buffer.h>
#include <wrenium/geo/geo_point.h>
#include <wrenium/geo/input_format.h>
#include <wrenium/geo/workspace.h>

// C++/QML bridge wrapping the wrenium-geo core library, shared by all three
// Qt Quick apps (demos/rotator, demos/radar, examples/azimuthmap) via the
// wrenium_geo_qt_bridge QML module -- import it to use the WreniumGeoBridge
// type. Loads the checked-in real-world coastline data
// (data/world_coastline_110m.h) once, then re-runs rotate->clip->project on
// every computeCoastlineSvgPath() call. Both the direct-SVG and binary-then-decode
// paths write their final result into m_workspace.svgPath, so the renderer
// always reads the result the same way regardless of which path produced it.
class WreniumGeoBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit WreniumGeoBridge(QObject *parent = nullptr);

    // Returns an SVG path `d` string for the given center/clip-radius/
    // viewport, or an empty string if the pipeline reports an Error (e.g.
    // clipRadiusKm/viewportRadiusPx <= 0, or -- should never happen with
    // the checked-in data -- a capacity overflow).
    Q_INVOKABLE QString computeCoastlineSvgPath(double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useBinaryEmitter);

    // Exposes wrenium::geo::kEarthRadiusKm (projection.h) so QML's drag-to-rotate
    // math (Main.qml) can invert the exact same scale formula
    // computeCoastlineSvgPath() uses internally, instead of duplicating the
    // constant as a magic number that could silently drift from the
    // library's own value.
    Q_INVOKABLE double earthRadiusKm() const;

    // Border-line counterpart to computeCoastlineSvgPath, for country border
    // segments (data/world_borders_110m.h, see clipLineToSink/
    // projectLines) -- a fully separate pipeline call and output buffer
    // from the coastline path above, by design: border data is optional
    // (the caller can simply never call this) and has no inside/outside
    // fill-rule concerns at all. Returns an empty string on the same error
    // conditions as computeCoastlineSvgPath.
    Q_INVOKABLE QString computeBorderSvgPath(double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useBinaryEmitter);

    // Projects an arbitrary point (e.g. a station marker or waypoint --
    // not part of either coastline/border dataset) into the exact same
    // (x, y) coordinate space computeCoastlineSvgPath()/computeBorderSvgPath()'s
    // returned path data uses: (0, 0) at the center, same scale (output
    // units per km). Lets QML place a marker Item on the map without
    // hand-rolling a second projection formula (wrenium::geo::projectPoint,
    // pipeline.h). Returns a 3-element list [x, y, visible] (x/y only
    // meaningful when visible is true, i.e. the point falls inside the
    // current clip circle) -- a plain QVariantList rather than a
    // QVariantMap, since this is called once per marker on every recompute
    // and a QVariantList is cheaper to construct for a fixed 3-field shape.
    Q_INVOKABLE QVariantList projectPoint(double lat, double lon, double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx) const;

private:
    static constexpr std::size_t kMaxPoints = 6000; // real data is 4997 points
    static constexpr std::size_t kMaxRings = 200;   // real data is 126 rings
    static constexpr std::size_t kMaxBinaryBytes = 131072;

    // Border data is much smaller than the coastline dataset (2,974 points/
    // 326 segments total) and clipLineToSink never inserts extra
    // boundary-following points (unlike clipRingToSink's arc bridging --
    // an open polyline just ends its run at the crossing point instead of
    // needing to be re-closed along the clip circle), so a clipped view
    // can never produce more points/runs than the input data itself --
    // still sized with headroom, not tightly to the exact real numbers.
    static constexpr std::size_t kMaxBorderPoints = 6000;
    static constexpr std::size_t kMaxBorderRings = 1024;
    static constexpr std::size_t kMaxBorderBinaryBytes = 131072;

    bool loadInputOnce();
    bool loadBorderInputOnce();

    wrenium::geo::Workspace<kMaxPoints, kMaxRings> m_workspace;
    // Points plus each ring's own [minLat, maxLat], loaded once by
    // loadInputGeometry (pipeline.h) instead of rescanned by projectRings
    // on every recompute -- see that function's own comment for the
    // measured saving.
    wrenium::geo::InputGeometry<kMaxPoints, kMaxRings> m_input;
    wrenium::geo::Buffer<std::uint8_t, kMaxBinaryBytes> m_binaryPath;
    bool m_inputLoaded = false;

    wrenium::geo::Workspace<kMaxBorderPoints, kMaxBorderRings> m_borderWorkspace;
    wrenium::geo::InputGeometry<kMaxBorderPoints, kMaxBorderRings> m_borderInput;
    wrenium::geo::Buffer<std::uint8_t, kMaxBorderBinaryBytes> m_borderBinaryPath;
    bool m_borderInputLoaded = false;
};

#endif // WRENIUM_GEO_QT_BRIDGE_WRENIUM_GEO_BRIDGE_H
