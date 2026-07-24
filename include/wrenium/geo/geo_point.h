// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#ifndef WRENIUM_GEO_GEO_POINT_H
#define WRENIUM_GEO_GEO_POINT_H

#include "wrenium/geo/projection.h"

namespace wrenium::geo {

/// A point on the sphere, in radians, standard geographic sign convention:
/// latitude north-positive in [-pi/2, pi/2], longitude east-positive in
/// [-pi, pi]. Measured against a perfect sphere (kEarthRadiusKm), not an
/// ellipsoidal Earth model. Distinct from Point (planar/projected
/// coordinates) so the two are never accidentally interchanged.
struct GeoPoint
{
    float latRad = 0.0f;
    float lonRad = 0.0f;
};

/// Builds a GeoPoint from latitude/longitude in degrees.
/// @param latDeg Latitude in degrees, north-positive.
/// @param lonDeg Longitude in degrees, east-positive.
/// @return The equivalent GeoPoint, in radians.
inline GeoPoint makeGeoPoint(float latDeg, float lonDeg)
{
    constexpr float kDegToRad = kPi / 180.0f;
    return GeoPoint{latDeg * kDegToRad, lonDeg * kDegToRad};
}

} // namespace wrenium::geo

#endif // WRENIUM_GEO_GEO_POINT_H
