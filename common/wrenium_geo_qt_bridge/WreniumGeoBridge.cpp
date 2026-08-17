// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "WreniumGeoBridge.h"

#include <QDebug>

#include <wrenium/geo/azimuthal_pipeline.h>
#include <wrenium/geo/azimuthal_svg.h>
#include <wrenium/geo/binary_emitter.h>
#include <wrenium/geo/cylindrical_pipeline.h>
#include <wrenium/geo/cylindrical_svg.h>
#include <wrenium/geo/detail/cylindrical/mercator.h>
#include <wrenium/geo/error.h>
#include <wrenium/geo/projection.h>
#include <wrenium/geo/spherical.h>
#include <wrenium/geo/viewport.h>

#include "BinaryPathDecoder.h"
#include "world_borders_110m.h"
#include "world_coastline_110m.h"

namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

// Logs why a pipeline/emit/load call failed instead of leaving the caller
// with only an empty QString and no clue.
void warnOnError(const char *context, wrenium::geo::Error error)
{
    if (error != wrenium::geo::Error::Ok) {
        qWarning() << "WreniumGeoBridge:" << context << "failed:" << wrenium::geo::errorToString(error);
    }
}

} // namespace

WreniumGeoBridge::WreniumGeoBridge(QObject *parent)
    : QObject(parent)
{
}

QString WreniumGeoBridge::computeCoastlineSvgPath(double centerLatDeg, double centerLonDeg, double clipRadiusKm, double viewportRadiusPx, bool useBinaryEmitter, bool useOrthographic)
{
    const wrenium::geo::Error loadErr = m_input.ensureLoaded(kWreniumGeoWorldCoastline110m, kWreniumGeoWorldCoastline110mSize);
    if (loadErr != wrenium::geo::Error::Ok) {
        warnOnError("computeCoastlineSvgPath: ensureLoaded", loadErr);
        return QString();
    }
    if (clipRadiusKm <= 0.0 || viewportRadiusPx <= 0.0) {
        return QString();
    }

    const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(static_cast<float>(centerLatDeg), static_cast<float>(centerLonDeg));
    const wrenium::geo::Viewport viewport = wrenium::geo::makeViewport(static_cast<float>(clipRadiusKm), static_cast<float>(viewportRadiusPx));

    const wrenium::geo::azimuthal::ProjectionType projectionType = useOrthographic ? wrenium::geo::azimuthal::ProjectionType::Orthographic : wrenium::geo::azimuthal::ProjectionType::Equidistant;

    wrenium::geo::Error err;
    if (!useBinaryEmitter) {
        err = wrenium::geo::azimuthal::projectRingsToSvg(m_workspace, m_svgPath, m_input, center, viewport.clipRadiusRad, viewport.scale, projectionType);
    } else {
        err = wrenium::geo::azimuthal::projectRings(m_workspace, m_input, center, viewport.clipRadiusRad, viewport.scale, projectionType);
        if (err == wrenium::geo::Error::Ok) {
            err = wrenium::geo::BinaryPathEmitter<>::encode(
                m_workspace.projectedPoints(), m_workspace.projectedRingSizes().data(),
                m_workspace.projectedRingSizes().size(), m_binaryPath);
        }
        if (err == wrenium::geo::Error::Ok) {
            err = BinaryPathDecoderExample::BinaryPathDecoder<>::decode(
                m_binaryPath.data(), m_binaryPath.size(), m_svgPath);
        }
    }

    if (err != wrenium::geo::Error::Ok) {
        warnOnError("computeCoastlineSvgPath", err);
        return QString();
    }

    return QString::fromLatin1(m_svgPath.data(), static_cast<int>(m_svgPath.size()));
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
    const wrenium::geo::Error loadErr = m_borderInput.ensureLoaded(kWreniumGeoWorldBorders110m, kWreniumGeoWorldBorders110mSize);
    if (loadErr != wrenium::geo::Error::Ok) {
        warnOnError("computeBorderSvgPath: ensureLoaded", loadErr);
        return QString();
    }
    if (clipRadiusKm <= 0.0 || viewportRadiusPx <= 0.0) {
        return QString();
    }

    const wrenium::geo::GeoPoint center = wrenium::geo::makeGeoPoint(static_cast<float>(centerLatDeg), static_cast<float>(centerLonDeg));
    const wrenium::geo::Viewport viewport = wrenium::geo::makeViewport(static_cast<float>(clipRadiusKm), static_cast<float>(viewportRadiusPx));

    const wrenium::geo::azimuthal::ProjectionType projectionType = useOrthographic ? wrenium::geo::azimuthal::ProjectionType::Orthographic : wrenium::geo::azimuthal::ProjectionType::Equidistant;

    wrenium::geo::Error err;
    if (!useBinaryEmitter) {
        err = wrenium::geo::azimuthal::projectLinesToSvg(m_borderWorkspace, m_borderSvgPath, m_borderInput, center, viewport.clipRadiusRad, viewport.scale, projectionType);
    } else {
        err = wrenium::geo::azimuthal::projectLines(m_borderWorkspace, m_borderInput, center, viewport.clipRadiusRad, viewport.scale, projectionType);
        if (err == wrenium::geo::Error::Ok) {
            err = wrenium::geo::LineBinaryPathEmitter<>::encode(
                m_borderWorkspace.projectedPoints(), m_borderWorkspace.projectedRingSizes().data(),
                m_borderWorkspace.projectedRingSizes().size(), m_borderBinaryPath);
        }
        if (err == wrenium::geo::Error::Ok) {
            err = BinaryPathDecoderExample::BinaryPathDecoder<>::decode(
                m_borderBinaryPath.data(), m_borderBinaryPath.size(), m_borderSvgPath);
        }
    }

    if (err != wrenium::geo::Error::Ok) {
        warnOnError("computeBorderSvgPath", err);
        return QString();
    }

    return QString::fromLatin1(m_borderSvgPath.data(), static_cast<int>(m_borderSvgPath.size()));
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

    const wrenium::geo::azimuthal::ProjectionType projectionType = useOrthographic ? wrenium::geo::azimuthal::ProjectionType::Orthographic : wrenium::geo::azimuthal::ProjectionType::Equidistant;
    const wrenium::geo::azimuthal::ProjectedPoint projected = wrenium::geo::azimuthal::projectPoint(rawPoint, center, viewport.clipRadiusRad, viewport.scale, projectionType);

    if (!projected.visible) {
        return {0.0, 0.0, false};
    }
    return {static_cast<double>(projected.point.x), static_cast<double>(projected.point.y), true};
}

QVariantList WreniumGeoBridge::destinationPoint(double latDeg, double lonDeg, double bearingDeg, double distanceKm) const
{
    const wrenium::geo::GeoPoint origin = wrenium::geo::makeGeoPoint(static_cast<float>(latDeg), static_cast<float>(lonDeg));
    const float bearingRad = static_cast<float>(bearingDeg) * static_cast<float>(1.0 / kRadToDeg);

    const wrenium::geo::GeoPoint result = wrenium::geo::destinationPoint(origin, static_cast<float>(distanceKm), bearingRad);
    return {static_cast<double>(result.latRad) * kRadToDeg, static_cast<double>(result.lonRad) * kRadToDeg};
}

double WreniumGeoBridge::distanceKm(double fromLatDeg, double fromLonDeg, double toLatDeg, double toLonDeg) const
{
    const wrenium::geo::GeoPoint from = wrenium::geo::makeGeoPoint(static_cast<float>(fromLatDeg), static_cast<float>(fromLonDeg));
    const wrenium::geo::GeoPoint to = wrenium::geo::makeGeoPoint(static_cast<float>(toLatDeg), static_cast<float>(toLonDeg));
    return static_cast<double>(wrenium::geo::distanceKm(from, to));
}

QString WreniumGeoBridge::computeMercatorCoastlineSvgPath(double centerLatDeg, double centerLonDeg, double halfWidthKm, double viewportWidthPx, double viewportHeightPx, bool useBinaryEmitter)
{
    const wrenium::geo::Error loadErr = m_input.ensureLoaded(kWreniumGeoWorldCoastline110m, kWreniumGeoWorldCoastline110mSize);
    if (loadErr != wrenium::geo::Error::Ok) {
        warnOnError("computeMercatorCoastlineSvgPath: ensureLoaded", loadErr);
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

    wrenium::geo::Error err;
    if (!useBinaryEmitter) {
        err = wrenium::geo::cylindrical::projectRingsToSvg(m_workspace, m_svgPath, m_input, center, scale, clipLatRad, clipLonRad);
    } else {
        err = wrenium::geo::cylindrical::projectRings(m_workspace, m_input, center, scale, clipLatRad, clipLonRad);
        if (err == wrenium::geo::Error::Ok) {
            err = wrenium::geo::BinaryPathEmitter<>::encode(
                m_workspace.projectedPoints(), m_workspace.projectedRingSizes().data(),
                m_workspace.projectedRingSizes().size(), m_binaryPath);
        }
        if (err == wrenium::geo::Error::Ok) {
            err = BinaryPathDecoderExample::BinaryPathDecoder<>::decode(
                m_binaryPath.data(), m_binaryPath.size(), m_svgPath);
        }
    }

    if (err != wrenium::geo::Error::Ok) {
        warnOnError("computeMercatorCoastlineSvgPath", err);
        return QString();
    }

    return QString::fromLatin1(m_svgPath.data(), static_cast<int>(m_svgPath.size()));
}

QString WreniumGeoBridge::computeMercatorBorderSvgPath(double centerLatDeg, double centerLonDeg, double halfWidthKm, double viewportWidthPx, double viewportHeightPx, bool useBinaryEmitter)
{
    const wrenium::geo::Error loadErr = m_borderInput.ensureLoaded(kWreniumGeoWorldBorders110m, kWreniumGeoWorldBorders110mSize);
    if (loadErr != wrenium::geo::Error::Ok) {
        warnOnError("computeMercatorBorderSvgPath: ensureLoaded", loadErr);
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

    wrenium::geo::Error err;
    if (!useBinaryEmitter) {
        err = wrenium::geo::cylindrical::projectLinesToSvg(m_borderWorkspace, m_borderSvgPath, m_borderInput, center, scale, clipLatRad, clipLonRad);
    } else {
        err = wrenium::geo::cylindrical::projectLines(m_borderWorkspace, m_borderInput, center, scale, clipLatRad, clipLonRad);
        if (err == wrenium::geo::Error::Ok) {
            err = wrenium::geo::LineBinaryPathEmitter<>::encode(
                m_borderWorkspace.projectedPoints(), m_borderWorkspace.projectedRingSizes().data(),
                m_borderWorkspace.projectedRingSizes().size(), m_borderBinaryPath);
        }
        if (err == wrenium::geo::Error::Ok) {
            err = BinaryPathDecoderExample::BinaryPathDecoder<>::decode(
                m_borderBinaryPath.data(), m_borderBinaryPath.size(), m_borderSvgPath);
        }
    }

    if (err != wrenium::geo::Error::Ok) {
        warnOnError("computeMercatorBorderSvgPath", err);
        return QString();
    }

    return QString::fromLatin1(m_borderSvgPath.data(), static_cast<int>(m_borderSvgPath.size()));
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
