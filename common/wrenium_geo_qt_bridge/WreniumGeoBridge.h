// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

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
    //
    // useOrthographic picks the radial-distance formula (pipeline.h's
    // ProjectFn template parameter) at the call level -- equidistant
    // (default, false) or orthographic (true, detail/azimuthal/
    // orthographic.h). Each maps to its own fully-specialized projectRings
    // instantiation with the formula inlined, so this is one branch per
    // call, not per point -- the hot per-point loop itself has no runtime
    // indirection either way. Defaults to false so existing callers that
    // don't pass it keep their current (equidistant) behavior unchanged.
    Q_INVOKABLE QString computeCoastlineSvgPath(double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useBinaryEmitter, bool useOrthographic = false);

    // Exposes wrenium::geo::kEarthRadiusKm (projection.h) so QML's drag-to-rotate
    // math (Main.qml) can invert the exact same scale formula
    // computeCoastlineSvgPath() uses internally, instead of duplicating the
    // constant as a magic number that could silently drift from the
    // library's own value.
    Q_INVOKABLE double earthRadiusKm() const;

    // Exposes cylindrical::kMercatorMaxLatRad (mercator.h), in degrees, so
    // examples/mercatormap can clamp centerLatDeg to it after drag/zoom
    // updates -- same reasoning as earthRadiusKm() above. Necessary, not
    // just tidy: past this latitude, project()'s own clamp collapses
    // every point beyond it to the same y, so an unclamped center can
    // drift into a dead zone where the map stops visibly responding to
    // further panning in that direction.
    Q_INVOKABLE double mercatorMaxLatDeg() const;

    // Border-line counterpart to computeCoastlineSvgPath, for country border
    // segments (data/world_borders_110m.h, see clipLineToSink/
    // projectLines) -- a fully separate pipeline call and output buffer
    // from the coastline path above, by design: border data is optional
    // (the caller can simply never call this) and has no inside/outside
    // fill-rule concerns at all. Returns an empty string on the same error
    // conditions as computeCoastlineSvgPath. useOrthographic: see
    // computeCoastlineSvgPath's identical parameter -- must match whatever
    // was passed there for the same map, or the two layers won't line up.
    Q_INVOKABLE QString computeBorderSvgPath(double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useBinaryEmitter, bool useOrthographic = false);

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
    // useOrthographic: see computeCoastlineSvgPath's identical parameter --
    // must match whatever was passed there for the same map, or a marker
    // placed via this method won't line up with the map underneath it.
    Q_INVOKABLE QVariantList projectPoint(double lat, double lon, double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useOrthographic = false) const;

    // Mercator counterpart to computeCoastlineSvgPath -- a fully separate
    // pipeline (cylindrical::projectRingsMercator, cylindrical_pipeline.h),
    // not a useOrthographic-style flag on the azimuthal methods above: no
    // clip radius, no rotation, a rectangular map instead of a disc. Reuses
    // the already-loaded m_input (raw GeoPoint rings, projection-agnostic)
    // and m_workspace (safe: never called in the same recompute as
    // computeCoastlineSvgPath, and every pipeline call fully overwrites
    // whatever was in it). halfWidthKm/viewportWidthPx together give scale
    // (output units per km) the same way clipRadiusKm/viewportRadiusPx give
    // it above, just width- instead of radius-based, matching how a
    // rectangular map's "zoom level" is naturally expressed.
    //
    // viewportHeightPx also feeds projectRingsMercator's own
    // clipLatRad/clipLonRad -- a whole ring that's provably outside the
    // visible window skips projection entirely, a real, measured
    // performance win on constrained targets (see cylindrical_pipeline.h's
    // own comment). Both derived the same simple (linear, not exact)
    // way -- see that comment for why that's safe, not just convenient.
    Q_INVOKABLE QString computeMercatorCoastlineSvgPath(double centerLatDeg, double centerLonDeg, double halfWidthKm, double viewportWidthPx, double viewportHeightPx, bool useBinaryEmitter);

    // Border-line counterpart, mirroring computeMercatorCoastlineSvgPath /
    // computeBorderSvgPath's own relationship. Reuses m_borderInput/
    // m_borderWorkspace for the same reason as above.
    Q_INVOKABLE QString computeMercatorBorderSvgPath(double centerLatDeg, double centerLonDeg, double halfWidthKm, double viewportWidthPx, double viewportHeightPx, bool useBinaryEmitter);

    // Inverse of the Mercator forward projection (cylindrical::unproject,
    // mercator.h): given a planar point in the same coordinate space
    // computeMercatorCoastlineSvgPath()'s output uses (0, 0) at center),
    // returns [latDeg, lonDeg]. Used by examples/mercatormap for both
    // drag-to-pan (grab the geo point under the cursor on press) and
    // scroll-to-zoom-toward-cursor (grab the point under the cursor before
    // changing scale) -- see MercatorMap.qml.
    Q_INVOKABLE QVariantList unprojectMercatorPoint(double pointX, double pointY, double centerLatDeg, double centerLonDeg, double halfWidthKm, double viewportWidthPx) const;

    // Given a known geo point (anchorLatDeg/anchorLonDeg) and the screen
    // position it should appear at (pointX/pointY, same coordinate space
    // as unprojectMercatorPoint's own parameter), returns the center that
    // makes that true -- the shared building block behind both drag-to-pan
    // (same halfWidthKm, a moving screen position) and
    // scroll-to-zoom-toward-cursor (same screen position, a new
    // halfWidthKm) in examples/mercatormap.
    //
    // Implemented as cylindrical::unproject(Point{-pointX, -pointY}, anchor,
    // scale): project()'s x is antisymmetric under swapping point<->center
    // (exact except at the precise antimeridian boundary, irrelevant for
    // interactive use) and its y is exactly antisymmetric under that same
    // swap (a plain subtraction, no boundary case at all) -- so "what
    // center puts anchor at (pointX, pointY)" is exactly
    // unproject(-(pointX, pointY), anchor, scale). Verified directly (not
    // just derived by hand) in wrenium-geo's own test_mercator.cpp.
    Q_INVOKABLE QVariantList recenterKeepingPointFixed(double pointX, double pointY, double anchorLatDeg, double anchorLonDeg, double halfWidthKm, double viewportWidthPx) const;

    // Exposes cylindrical::clampCenterLatForViewport (mercator.h): unlike
    // a flat +-mercatorMaxLatDeg() clamp, this accounts for the current
    // zoom's own vertical extent, so panning stops once the *viewport's*
    // edge -- not just the center point -- would cross the map's valid
    // latitude range. examples/mercatormap uses this instead of a flat
    // clamp after every drag/zoom/typed-latitude update; see that
    // function's own comment for the dead-space bug a flat clamp causes
    // at wide zooms.
    Q_INVOKABLE double clampMercatorCenterLatDeg(double latDeg, double halfWidthKm, double viewportWidthPx, double viewportHeightPx) const;

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
