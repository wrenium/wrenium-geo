// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "WreniumGeoBridge.h"

#include <wrenium/geo/binary_emitter.h>
#include <wrenium/geo/pipeline.h>
#include <wrenium/geo/projection.h>
#include <wrenium/geo/svg_emitter.h>
#include <wrenium/geo/viewport.h>

#include "BinaryPathDecoder.h"
#include "world_borders_110m.h"
#include "world_coastline_110m.h"

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

QString WreniumGeoBridge::computeCoastlineSvgPath(double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useBinaryEmitter)
{
    if (!loadInputOnce()) {
        return QString();
    }
    if (clipRadiusKm <= 0.0 || viewportRadiusPx <= 0.0) {
        return QString();
    }

    const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(static_cast<float>(centerLatDeg), static_cast<float>(centerLonDeg));
    const wrenium::geo::Viewport viewport = wrenium::geo::makeViewport(static_cast<float>(clipRadiusKm), static_cast<float>(viewportRadiusPx));

    const wrenium::geo::Error pipelineErr = wrenium::geo::projectRings(
        m_workspace, m_input, center, viewport.clipRadiusRad, viewport.scale);
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

QString WreniumGeoBridge::computeBorderSvgPath(double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useBinaryEmitter)
{
    if (!loadBorderInputOnce()) {
        return QString();
    }
    if (clipRadiusKm <= 0.0 || viewportRadiusPx <= 0.0) {
        return QString();
    }

    const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(static_cast<float>(centerLatDeg), static_cast<float>(centerLonDeg));
    const wrenium::geo::Viewport viewport = wrenium::geo::makeViewport(static_cast<float>(clipRadiusKm), static_cast<float>(viewportRadiusPx));

    const wrenium::geo::Error pipelineErr = wrenium::geo::projectLines(
        m_borderWorkspace, m_borderInput, center, viewport.clipRadiusRad, viewport.scale);
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

QVariantList WreniumGeoBridge::projectPoint(double lat, double lon, double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx) const
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

    const wrenium::geo::ProjectedPoint projected = wrenium::geo::projectPoint(rawPoint, center, viewport.clipRadiusRad, viewport.scale);

    if (!projected.visible) {
        return {0.0, 0.0, false};
    }
    return {static_cast<double>(projected.point.x), static_cast<double>(projected.point.y), true};
}
