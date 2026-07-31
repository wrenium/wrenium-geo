// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "WreniumGeoBridge.h"

#include <wrenium/geo/azimuthal_pipeline.h>
#include <wrenium/geo/binary_emitter.h>
#include <wrenium/geo/cylindrical_pipeline.h>
#include <wrenium/geo/detail/azimuthal/orthographic.h>
#include <wrenium/geo/detail/cylindrical/mercator.h>
#include <wrenium/geo/projection.h>
#include <wrenium/geo/svg_emitter.h>
#include <wrenium/geo/viewport.h>

#include "BinaryPathDecoder.h"
#include "world_borders_110m.h"
#include "world_coastline_110m.h"

namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

} // namespace

WreniumGeoBridge::WreniumGeoBridge(QObject *parent)
    : QObject(parent)
{
}

bool WreniumGeoBridge::loadInputOnce()
{
    if (m_inputLoaded) {
        return true;
    }

    const wrenium::geo::Error err = wrenium::geo::loadInputGeometry(
        kWreniumGeoWorldCoastline110m, sizeof(kWreniumGeoWorldCoastline110m), m_input);
    m_inputLoaded = (err == wrenium::geo::Error::Ok);
    return m_inputLoaded;
}

bool WreniumGeoBridge::loadBorderInputOnce()
{
    if (m_borderInputLoaded) {
        return true;
    }

    const wrenium::geo::Error err = wrenium::geo::loadInputGeometry(
        kWreniumGeoWorldBorders110m, sizeof(kWreniumGeoWorldBorders110m), m_borderInput);
    m_borderInputLoaded = (err == wrenium::geo::Error::Ok);
    return m_borderInputLoaded;
}

QString WreniumGeoBridge::computeCoastlineSvgPath(double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useBinaryEmitter, bool useOrthographic)
{
    if (!loadInputOnce()) {
        return QString();
    }
    if (clipRadiusKm <= 0.0 || viewportRadiusPx <= 0.0) {
        return QString();
    }

    const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(static_cast<float>(centerLatDeg), static_cast<float>(centerLonDeg));
    const wrenium::geo::Viewport viewport = wrenium::geo::makeViewport(static_cast<float>(clipRadiusKm), static_cast<float>(viewportRadiusPx));

    const wrenium::geo::Error pipelineErr = useOrthographic
        ? wrenium::geo::azimuthal::projectRings<wrenium::geo::azimuthal::projectOrthographic>(m_workspace, m_input, center, viewport.clipRadiusRad, viewport.scale)
        : wrenium::geo::azimuthal::projectRings(m_workspace, m_input, center, viewport.clipRadiusRad, viewport.scale);
    if (pipelineErr != wrenium::geo::Error::Ok) {
        return QString();
    }

    wrenium::geo::Error emitErr;
    if (!useBinaryEmitter) {
        emitErr = wrenium::geo::emitSvgPath(
            m_workspace.projectedPoints(), m_workspace.projectedRingSizes().data(),
            m_workspace.projectedRingSizes().size(), m_workspace.svgPath);
    } else {
        emitErr = wrenium::geo::BinaryPathEmitter<>::encode(
            m_workspace.projectedPoints(), m_workspace.projectedRingSizes().data(),
            m_workspace.projectedRingSizes().size(), m_binaryPath);
        if (emitErr == wrenium::geo::Error::Ok) {
            emitErr = BinaryPathDecoderExample::BinaryPathDecoder<>::decode(
                m_binaryPath.data(), m_binaryPath.size(), m_workspace.svgPath);
        }
    }

    if (emitErr != wrenium::geo::Error::Ok) {
        return QString();
    }

    return QString::fromLatin1(m_workspace.svgPath.data(), static_cast<int>(m_workspace.svgPath.size()));
}

double WreniumGeoBridge::earthRadiusKm() const
{
    return wrenium::geo::kEarthRadiusKm;
}

double WreniumGeoBridge::mercatorMaxLatDeg() const
{
    return static_cast<double>(wrenium::geo::cylindrical::kMercatorMaxLatRad) * kRadToDeg;
}

QString WreniumGeoBridge::computeBorderSvgPath(double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useBinaryEmitter, bool useOrthographic)
{
    if (!loadBorderInputOnce()) {
        return QString();
    }
    if (clipRadiusKm <= 0.0 || viewportRadiusPx <= 0.0) {
        return QString();
    }

    const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(static_cast<float>(centerLatDeg), static_cast<float>(centerLonDeg));
    const wrenium::geo::Viewport viewport = wrenium::geo::makeViewport(static_cast<float>(clipRadiusKm), static_cast<float>(viewportRadiusPx));

    const wrenium::geo::Error pipelineErr = useOrthographic
        ? wrenium::geo::azimuthal::projectLines<wrenium::geo::azimuthal::projectOrthographic>(m_borderWorkspace, m_borderInput, center, viewport.clipRadiusRad, viewport.scale)
        : wrenium::geo::azimuthal::projectLines(m_borderWorkspace, m_borderInput, center, viewport.clipRadiusRad, viewport.scale);
    if (pipelineErr != wrenium::geo::Error::Ok) {
        return QString();
    }

    wrenium::geo::Error emitErr;
    if (!useBinaryEmitter) {
        emitErr = wrenium::geo::emitSvgLinePath(
            m_borderWorkspace.projectedPoints(), m_borderWorkspace.projectedRingSizes().data(),
            m_borderWorkspace.projectedRingSizes().size(), m_borderWorkspace.svgPath);
    } else {
        emitErr = wrenium::geo::LineBinaryPathEmitter<>::encode(
            m_borderWorkspace.projectedPoints(), m_borderWorkspace.projectedRingSizes().data(),
            m_borderWorkspace.projectedRingSizes().size(), m_borderBinaryPath);
        if (emitErr == wrenium::geo::Error::Ok) {
            emitErr = BinaryPathDecoderExample::BinaryPathDecoder<>::decode(
                m_borderBinaryPath.data(), m_borderBinaryPath.size(), m_borderWorkspace.svgPath);
        }
    }

    if (emitErr != wrenium::geo::Error::Ok) {
        return QString();
    }

    return QString::fromLatin1(m_borderWorkspace.svgPath.data(), static_cast<int>(m_borderWorkspace.svgPath.size()));
}

QVariantList WreniumGeoBridge::projectPoint(double lat, double lon, double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useOrthographic) const
{
    if (clipRadiusKm <= 0.0 || viewportRadiusPx <= 0.0) {
        return {0.0, 0.0, false};
    }

    const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(static_cast<float>(centerLatDeg), static_cast<float>(centerLonDeg));
    const wrenium::geo::GeoPoint rawPoint = wrenium::geo::makeGeoPoint(static_cast<float>(lat), static_cast<float>(lon));
    // Same makeViewport() call computeCoastlineSvgPath/computeBorderSvgPath
    // use, so a marker placed via this method's result lands exactly where
    // the SVG/binary path output puts the same location.
    const wrenium::geo::Viewport viewport = wrenium::geo::makeViewport(static_cast<float>(clipRadiusKm), static_cast<float>(viewportRadiusPx));

    const wrenium::geo::azimuthal::ProjectedPoint projected = useOrthographic
        ? wrenium::geo::azimuthal::projectPoint<wrenium::geo::azimuthal::projectOrthographic>(rawPoint, center, viewport.clipRadiusRad, viewport.scale)
        : wrenium::geo::azimuthal::projectPoint(rawPoint, center, viewport.clipRadiusRad, viewport.scale);

    if (!projected.visible) {
        return {0.0, 0.0, false};
    }
    return {static_cast<double>(projected.point.x), static_cast<double>(projected.point.y), true};
}

QString WreniumGeoBridge::computeMercatorCoastlineSvgPath(double centerLatDeg, double centerLonDeg, double halfWidthKm, double viewportWidthPx, double viewportHeightPx, bool useBinaryEmitter)
{
    if (!loadInputOnce()) {
        return QString();
    }
    if (halfWidthKm <= 0.0 || viewportWidthPx <= 0.0 || viewportHeightPx <= 0.0) {
        return QString();
    }

    const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(static_cast<float>(centerLatDeg), static_cast<float>(centerLonDeg));
    const float scale = static_cast<float>(viewportWidthPx / 2.0 / halfWidthKm);
    // clipLonRad is exact (x is exactly linear in longitude). clipLatRad
    // reuses the same linear formula for latitude, an approximation that's
    // always safe (never excludes a ring that's actually visible) -- see
    // cylindrical_pipeline.h's own comment on clipLatRad for why.
    const float clipLonRad = static_cast<float>(halfWidthKm / wrenium::geo::kEarthRadiusKm);
    const float halfHeightKm = static_cast<float>(viewportHeightPx / 2.0 / scale);
    const float clipLatRad = halfHeightKm / wrenium::geo::kEarthRadiusKm;

    const wrenium::geo::Error pipelineErr = wrenium::geo::cylindrical::projectRings(m_workspace, m_input, center, scale, clipLatRad, clipLonRad);
    if (pipelineErr != wrenium::geo::Error::Ok) {
        return QString();
    }

    wrenium::geo::Error emitErr;
    if (!useBinaryEmitter) {
        emitErr = wrenium::geo::emitSvgPath(
            m_workspace.projectedPoints(), m_workspace.projectedRingSizes().data(),
            m_workspace.projectedRingSizes().size(), m_workspace.svgPath);
    } else {
        emitErr = wrenium::geo::BinaryPathEmitter<>::encode(
            m_workspace.projectedPoints(), m_workspace.projectedRingSizes().data(),
            m_workspace.projectedRingSizes().size(), m_binaryPath);
        if (emitErr == wrenium::geo::Error::Ok) {
            emitErr = BinaryPathDecoderExample::BinaryPathDecoder<>::decode(
                m_binaryPath.data(), m_binaryPath.size(), m_workspace.svgPath);
        }
    }

    if (emitErr != wrenium::geo::Error::Ok) {
        return QString();
    }

    return QString::fromLatin1(m_workspace.svgPath.data(), static_cast<int>(m_workspace.svgPath.size()));
}

QString WreniumGeoBridge::computeMercatorBorderSvgPath(double centerLatDeg, double centerLonDeg, double halfWidthKm, double viewportWidthPx, double viewportHeightPx, bool useBinaryEmitter)
{
    if (!loadBorderInputOnce()) {
        return QString();
    }
    if (halfWidthKm <= 0.0 || viewportWidthPx <= 0.0 || viewportHeightPx <= 0.0) {
        return QString();
    }

    const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(static_cast<float>(centerLatDeg), static_cast<float>(centerLonDeg));
    const float scale = static_cast<float>(viewportWidthPx / 2.0 / halfWidthKm);
    // See computeMercatorCoastlineSvgPath's identical comment.
    const float clipLonRad = static_cast<float>(halfWidthKm / wrenium::geo::kEarthRadiusKm);
    const float halfHeightKm = static_cast<float>(viewportHeightPx / 2.0 / scale);
    const float clipLatRad = halfHeightKm / wrenium::geo::kEarthRadiusKm;

    const wrenium::geo::Error pipelineErr = wrenium::geo::cylindrical::projectLines(m_borderWorkspace, m_borderInput, center, scale, clipLatRad, clipLonRad);
    if (pipelineErr != wrenium::geo::Error::Ok) {
        return QString();
    }

    wrenium::geo::Error emitErr;
    if (!useBinaryEmitter) {
        emitErr = wrenium::geo::emitSvgLinePath(
            m_borderWorkspace.projectedPoints(), m_borderWorkspace.projectedRingSizes().data(),
            m_borderWorkspace.projectedRingSizes().size(), m_borderWorkspace.svgPath);
    } else {
        emitErr = wrenium::geo::LineBinaryPathEmitter<>::encode(
            m_borderWorkspace.projectedPoints(), m_borderWorkspace.projectedRingSizes().data(),
            m_borderWorkspace.projectedRingSizes().size(), m_borderBinaryPath);
        if (emitErr == wrenium::geo::Error::Ok) {
            emitErr = BinaryPathDecoderExample::BinaryPathDecoder<>::decode(
                m_borderBinaryPath.data(), m_borderBinaryPath.size(), m_borderWorkspace.svgPath);
        }
    }

    if (emitErr != wrenium::geo::Error::Ok) {
        return QString();
    }

    return QString::fromLatin1(m_borderWorkspace.svgPath.data(), static_cast<int>(m_borderWorkspace.svgPath.size()));
}

QVariantList WreniumGeoBridge::unprojectMercatorPoint(double pointX, double pointY, double centerLatDeg, double centerLonDeg, double halfWidthKm, double viewportWidthPx) const
{
    if (halfWidthKm <= 0.0 || viewportWidthPx <= 0.0) {
        return {centerLatDeg, centerLonDeg};
    }

    const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(static_cast<float>(centerLatDeg), static_cast<float>(centerLonDeg));
    const float scale = static_cast<float>(viewportWidthPx / 2.0 / halfWidthKm);
    const wrenium::geo::Point point{static_cast<float>(pointX), static_cast<float>(pointY)};

    const wrenium::geo::GeoPoint result = wrenium::geo::cylindrical::unproject(point, center, scale);
    return {static_cast<double>(result.latRad) * kRadToDeg, static_cast<double>(result.lonRad) * kRadToDeg};
}

QVariantList WreniumGeoBridge::recenterKeepingPointFixed(double pointX, double pointY, double anchorLatDeg, double anchorLonDeg, double halfWidthKm, double viewportWidthPx) const
{
    if (halfWidthKm <= 0.0 || viewportWidthPx <= 0.0) {
        return {anchorLatDeg, anchorLonDeg};
    }

    const wrenium::geo::GeoPoint anchor = wrenium::geo::makeGeoPoint(static_cast<float>(anchorLatDeg), static_cast<float>(anchorLonDeg));
    const float scale = static_cast<float>(viewportWidthPx / 2.0 / halfWidthKm);
    // See this method's own declaration (WreniumGeoBridge.h) for the
    // point<->center swap identity this relies on.
    const wrenium::geo::Point negated{static_cast<float>(-pointX), static_cast<float>(-pointY)};

    const wrenium::geo::GeoPoint newCenter = wrenium::geo::cylindrical::unproject(negated, anchor, scale);
    return {static_cast<double>(newCenter.latRad) * kRadToDeg, static_cast<double>(newCenter.lonRad) * kRadToDeg};
}

double WreniumGeoBridge::clampMercatorCenterLatDeg(double latDeg, double halfWidthKm, double viewportWidthPx, double viewportHeightPx) const
{
    if (halfWidthKm <= 0.0 || viewportWidthPx <= 0.0 || viewportHeightPx <= 0.0) {
        return latDeg;
    }

    const float scale = static_cast<float>(viewportWidthPx / 2.0 / halfWidthKm);
    const float latRad = static_cast<float>(latDeg) * static_cast<float>(1.0 / kRadToDeg);
    const float clampedLatRad = wrenium::geo::cylindrical::clampCenterLatForViewport(latRad, scale, static_cast<float>(viewportHeightPx));
    return static_cast<double>(clampedLatRad) * kRadToDeg;
}
