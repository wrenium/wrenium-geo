// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

/// @file
/// Fundamental constants used regardless of projection family: angle units
/// (radians throughout, never degrees) and the Earth-radius constant that
/// defines the "scale" parameter's units convention. Deliberately just
/// constants -- the azimuthal-equidistant projection this library actually
/// implements lives under detail/azimuthal/ (rotation.h for the sphere
/// rotation any azimuthal variant shares, equidistant.h for the one
/// radial-distance formula specific to this variant), not here, so this
/// header never implies more than "generic definitions" to a future
/// non-azimuthal projection.

namespace wrenium::geo {

/// Pi, for converting between radians (used throughout this library) and
/// degrees (the unit most callers' own inputs are in).
constexpr float kPi = 3.14159265358979323846f;
/// Pi / 2 -- the rotated latitude of the projection center itself
/// (detail/azimuthal/rotation.h), and the threshold a clip radius is
/// measured down from.
constexpr float kHalfPi = kPi * 0.5f;

/// Earth's mean radius in kilometers -- the constant behind the "scale"
/// parameter's units convention (output units per kilometer):
/// distanceKm = centralAngleRad * kEarthRadiusKm.
constexpr float kEarthRadiusKm = 6371.0f;

} // namespace wrenium::geo
