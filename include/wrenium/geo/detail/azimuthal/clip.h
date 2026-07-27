// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cmath>
#include <cstddef>

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/azimuthal/rotation.h"
#include "wrenium/geo/error.h"
#include "wrenium/geo/geo_point.h"
#include "wrenium/geo/point.h"

/// @file
/// Small-circle clip at a configurable runtime angular radius. After
/// rotate(), the clip circle is a constant-latitude threshold:
///
///     rotatedLatRad >= kHalfPi - clipRadiusRad
///
/// Sutherland-Hodgman-style: walk the ring, emit a crossing point wherever
/// consecutive points are on opposite sides of the boundary. Crossing
/// points are linearly interpolated in rotated space rather than solved
/// exactly on the sphere -- fine for a UI-scale display, not survey-grade.
///
/// Points are rotated lazily, one at a time, only when a cheap
/// latitude-only bound can't already prove them outside -- most points in
/// a typical (non-whole-world) view don't survive clipping, so this avoids
/// rotating them at all.

namespace wrenium::geo::azimuthal {

namespace detail {

inline bool isInsideClipCircle(const GeoPoint &rotatedPoint, float clipRadiusRad)
{
    const float threshold = kHalfPi - clipRadiusRad;
    return rotatedPoint.latRad >= threshold;
}

inline GeoPoint clipBoundaryCrossing(const GeoPoint &outside, const GeoPoint &inside, float clipRadiusRad)
{
    const float threshold = kHalfPi - clipRadiusRad;

    float lonA = outside.lonRad;
    float lonB = inside.lonRad;
    if (lonB - lonA > kPi) {
        lonB -= 2.0f * kPi;
    } else if (lonA - lonB > kPi) {
        lonB += 2.0f * kPi;
    }

    const float denom = inside.latRad - outside.latRad;
    float t = 0.5f;
    if (denom != 0.0f) {
        t = (threshold - outside.latRad) / denom;
    }
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }

    float lon = lonA + (lonB - lonA) * t;
    while (lon > kPi) {
        lon -= 2.0f * kPi;
    }
    while (lon <= -kPi) {
        lon += 2.0f * kPi;
    }

    GeoPoint result;
    result.latRad = threshold;
    result.lonRad = lon;
    return result;
}

// True angular distance from `center` is always >= |point.latRad -
// center.latRad| (a provable spherical-trig fact): if this bound alone
// already exceeds clipRadiusRad, the point is guaranteed outside without
// rotating.
inline bool isCheaplyOutside(const GeoPoint &rawPoint, const GeoPoint &center, float clipRadiusRad)
{
    return std::fabs(rawPoint.latRad - center.latRad) > clipRadiusRad;
}

// Shared by clipRingToSink's excursion-enclosure accumulator and
// isCenterEnclosedByRings (clip.h, further down) -- both need the same
// "wrap to within (-pi, pi]" operation on plain longitude/bearing deltas.
inline float wrapPi(float value)
{
    while (value > kPi) {
        value -= 2.0f * kPi;
    }
    while (value <= -kPi) {
        value += 2.0f * kPi;
    }
    return value;
}

// Traces the clip circle's own boundary between two bearings, one point
// roughly every 3 degrees, so a bridged gap follows the circle's edge
// instead of cutting a straight chord across it. Always sweeps in the
// fixed direction the caller specifies (+1 = increasing bearing, -1 =
// decreasing) -- never picks the shorter way itself; the caller (see
// clipRingToSink) already knows which direction is correct.
constexpr float kBoundaryArcStepRad = 3.0f * kPi / 180.0f;
constexpr int kBoundaryArcMaxPoints = 128;

template <typename Sink>
inline Error emitBoundaryArc(float fromLonRad, float toLonRad, int direction, float clipRadiusRad, Sink &&sink, std::size_t &outputCount)
{
    float delta = toLonRad - fromLonRad;
    if (direction > 0) {
        while (delta < 0.0f) {
            delta += 2.0f * kPi;
        }
        while (delta >= 2.0f * kPi) {
            delta -= 2.0f * kPi;
        }
    } else {
        while (delta > 0.0f) {
            delta -= 2.0f * kPi;
        }
        while (delta <= -2.0f * kPi) {
            delta += 2.0f * kPi;
        }
    }

    const float absDelta = delta < 0.0f ? -delta : delta;
    int steps = static_cast<int>(absDelta / kBoundaryArcStepRad) + 1;
    if (steps > kBoundaryArcMaxPoints) {
        steps = kBoundaryArcMaxPoints;
    }

    const float threshold = kHalfPi - clipRadiusRad;
    for (int s = 1; s < steps; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(steps);
        float lon = fromLonRad + delta * t;
        while (lon > kPi) {
            lon -= 2.0f * kPi;
        }
        while (lon <= -kPi) {
            lon += 2.0f * kPi;
        }

        GeoPoint arcPoint;
        arcPoint.latRad = threshold;
        arcPoint.lonRad = lon;
        const Error err = sink(arcPoint);
        if (err != Error::Ok) {
            return err;
        }
        ++outputCount;
    }
    return Error::Ok;
}

// Inverse of rotate(): given a point already expressed in the rotated
// frame (as if `center` were the pole), recovers its raw (lat, lon).
// Needed only for clipRingToSink's single reference-point anchor fact --
// nowhere else in the library goes backwards from rotated to raw space.
// sinLat is clamped to [-1, 1] before asinf() as a float-precision
// safety margin (mathematically always in range, but rounding can push it
// a hair outside).
//
// Special-cased within ~0.006 degrees of either pole: there, cosCenterLat
// is ~0, which drives both atan2f arguments in the general formula toward
// 0 simultaneously -- the result then depends on the sign of two
// near-zero rounding residuals, an exact +-180 degree longitude error
// (this anchor point feeds clipRingToSink's whole rejoin alternation, so
// that error flips every crossing's inside/outside assignment for the
// ring). Bypassed with the closed-form limit the general formula
// converges to at a pole instead (the north/south cases genuinely differ
// in sign/offset, not just a mirrored copy-paste).
inline GeoPoint unrotate(const GeoPoint &rotated, const GeoPoint &center)
{
    const float centralAngle = kHalfPi - rotated.latRad;
    const float bearing = rotated.lonRad;

    const float cosCenterLat = cosf(center.latRad);

    constexpr float kPoleCosEpsilon = 1e-4f;
    if (cosCenterLat > -kPoleCosEpsilon && cosCenterLat < kPoleCosEpsilon) {
        const bool northPole = sinf(center.latRad) > 0.0f;
        GeoPoint result;
        result.latRad = northPole ? (kHalfPi - centralAngle) : (centralAngle - kHalfPi);
        result.lonRad = center.lonRad + (northPole ? (kPi - bearing) : bearing);
        while (result.lonRad > kPi) {
            result.lonRad -= 2.0f * kPi;
        }
        while (result.lonRad <= -kPi) {
            result.lonRad += 2.0f * kPi;
        }
        return result;
    }

    const float sinCenterLat = sinf(center.latRad);
    const float sinAngle = sinf(centralAngle);
    const float cosAngle = cosf(centralAngle);

    float sinLat = sinCenterLat * cosAngle + cosCenterLat * sinAngle * cosf(bearing);
    if (sinLat > 1.0f) {
        sinLat = 1.0f;
    } else if (sinLat < -1.0f) {
        sinLat = -1.0f;
    }
    const float lat = asinf(sinLat);
    const float lon = center.lonRad + atan2f(sinf(bearing) * sinAngle * cosCenterLat, cosAngle - sinCenterLat * sinLat);

    GeoPoint result;
    result.latRad = lat;
    result.lonRad = lon;
    return result;
}

// Traces the *entire* clip circle boundary (every bearing), not just an arc
// between an exit and entry -- used by isCenterEnclosedByRings's caller
// (pipeline.h) for the case where clipping finds zero rings anywhere near
// the clip circle at all, but the center itself sits inside one of the
// input rings: with no coastline anywhere in view, the correct visible
// shape is the entire clip circle. Same 3-degree step / point cap as
// emitBoundaryArc, so the resulting circle's resolution matches every other
// clip-circle boundary this library ever draws.
template <typename Sink>
inline Error emitFullClipCircle(float clipRadiusRad, Sink &&sink, std::size_t &outputCount)
{
    const float threshold = kHalfPi - clipRadiusRad;
    constexpr float kFullCircleRad = 2.0f * kPi;

    int steps = static_cast<int>(kFullCircleRad / kBoundaryArcStepRad) + 1;
    if (steps > kBoundaryArcMaxPoints) {
        steps = kBoundaryArcMaxPoints;
    }

    for (int s = 0; s < steps; ++s) {
        const float lon = -kPi + kFullCircleRad * (static_cast<float>(s) / static_cast<float>(steps));

        GeoPoint arcPoint;
        arcPoint.latRad = threshold;
        arcPoint.lonRad = lon;
        const Error err = sink(arcPoint);
        if (err != Error::Ok) {
            return err;
        }
        ++outputCount;
    }
    return Error::Ok;
}

} // namespace detail

/// Clips one closed ring of *unrotated* input points against the circle of
/// angular radius @p clipRadiusRad around @p center, pushing surviving/
/// crossing points (already rotated) one at a time into @p sink rather
/// than a fixed buffer type, so callers can write directly into whatever
/// storage they have (e.g. a Workspace).
///
/// Ports d3-geo's rejoin algorithm (`src/clip/circle.js` +
/// `src/clip/rejoin.js`): split the ring into its visible "kept segments";
/// build two views of the same segment-boundary crossings, one in
/// ring-traversal order and one sorted by bearing along the clip boundary;
/// test once whether one arbitrary point on the clip boundary is inside
/// the ring, then propagate that fact to every other crossing by
/// alternation; traverse by ping-ponging between the two views. A ring can
/// clip into more than one disjoint visible piece (e.g. two separate capes
/// of the same landmass); @p ringBoundary is called once per piece, with
/// its point count, so a caller that needs each piece as its own separate
/// ring (pipeline.h does) knows where the boundaries are -- callers that
/// only want the flattened point sequence (e.g. clipRing()) can ignore it.
///
/// Known limitation: a ring where every segment is independently
/// self-contained (no segment chains to another) can pick the wrong arc
/// direction for the dominant segment on a near-exact 180-degree tie.
/// @param rawPoints Input ring points, unrotated.
/// @param pointCount Number of points at @p rawPoints.
/// @param center The clip circle's center.
/// @param clipRadiusRad Clip radius in radians.
/// @param rotatedCache Scratch space, at least @p pointCount entries.
/// @param sink Callback `Error(const GeoPoint&)` invoked once per surviving point.
/// @param ringBoundary Callback `Error(std::size_t)` invoked once per
/// disjoint output piece, with that piece's point count. Fewer than 3
/// points in a piece means no usable shape.
/// @param outputCount Receives the total number of points written to @p sink.
/// @return Error::Ok on success; Error::TooManyClipCrossings if the ring
/// crosses the clip boundary more than 128 times; otherwise whatever error
/// @p sink/@p ringBoundary returned.
template <typename Sink, typename RingBoundarySink>
inline Error clipRingToSink(const GeoPoint *rawPoints, std::size_t pointCount, const GeoPoint &center, float clipRadiusRad, GeoPoint *rotatedCache, Sink &&sink, RingBoundarySink &&ringBoundary, std::size_t &outputCount)
{
    outputCount = 0;
    if (pointCount == 0) {
        return Error::Ok;
    }

    // Built once per ring, reused by every rotate() call below instead of
    // each one separately recomputing sin/cos of center.latRad -- see
    // RotationFrame's own comment (detail/projection.h) for the measured win.
    const RotationFrame frame = makeRotationFrame(center);
    const float threshold = kHalfPi - clipRadiusRad;

    // Classifies rawPoints[idx] as inside/outside, rotating it only if the
    // cheap bound can't already prove it's outside. Uses rotateBegin()
    // rather than rotate() -- most points that reach this point still fail
    // the real circle test (see rotateBegin()'s own comment), so the
    // bearing atan2f rotate() would compute is deferred via rotateFinish()
    // until a point is confirmed inside. `hasRotated` reports whether
    // `rotated` was actually populated (bearing included); a point that
    // fails here gets the same hasRotated=false treatment as one that
    // failed the cheap check, so the pass-1 walk below (which needs a
    // crossing-adjacent point's bearing regardless of whether it survived)
    // transparently re-rotates it in full via its own existing fallback.
    // Caches the rotated value into rotatedCache[idx] so pass 2 can read it
    // back instead of rotating the same point again.
    auto classify = [&](std::size_t idx, GeoPoint &rotated, bool &hasRotated) -> bool {
        if (detail::isCheaplyOutside(rawPoints[idx], center, clipRadiusRad)) {
            hasRotated = false;
            return false;
        }
        const RotatePartial partial = rotateBegin(rawPoints[idx], frame);
        if (partial.rotatedLat < threshold) {
            hasRotated = false;
            return false;
        }
        rotated = rotateFinish(partial);
        rotatedCache[idx] = rotated;
        hasRotated = true;
        return true;
    };

    // Find a starting vertex that's actually inside, and begin the walk
    // there instead of always at index 0 -- guarantees the first transition
    // encountered is always an exit, so pass 1 below never has to handle a
    // ring that starts mid-excursion. If no vertex is inside at all, the
    // ring contributes nothing.
    std::size_t startIdx = 0;
    bool foundInside = false;
    for (std::size_t k = 0; k < pointCount; ++k) {
        GeoPoint tmpRotated{};
        bool tmpHasRotated = false;
        if (classify(k, tmpRotated, tmpHasRotated)) {
            startIdx = k;
            foundInside = true;
            break;
        }
    }
    if (!foundInside) {
        return Error::Ok;
    }

    // ---- Pass 1: walk the ring once, recording every exit/entry crossing
    // (bearing + ring index) without emitting anything yet. Kept segment j
    // (0..K-1) starts at entry[j] and ends at exit[(j+1)%K] -- exit[j] and
    // entry[j] share index j because they're recorded as a pair, in
    // ring-traversal order, exit first (see the loop below). ----
    constexpr std::size_t kMaxRingExcursions = 128;
    float exitBearings[kMaxRingExcursions];
    float entryBearings[kMaxRingExcursions];
    std::size_t entryRingIdx[kMaxRingExcursions];    // ring index of first kept point after entry[j]
    std::size_t exitRingIdxPrev[kMaxRingExcursions]; // ring index of last kept point before exit[j]
    std::size_t excursionCount = 0;

    std::size_t prevIdx = startIdx;
    GeoPoint prevRotated{};
    bool prevHasRotated = false;
    bool prevInside = classify(prevIdx, prevRotated, prevHasRotated); // known true

    for (std::size_t step = 1; step <= pointCount; ++step) {
        const std::size_t i = (startIdx + step) % pointCount;
        GeoPoint currRotated{};
        bool currHasRotated = false;
        const bool currInside = classify(i, currRotated, currHasRotated);

        if (currInside && !prevInside) {
            if (!prevHasRotated) {
                prevRotated = rotate(rawPoints[prevIdx], frame);
            }
            const GeoPoint crossing = detail::clipBoundaryCrossing(prevRotated, currRotated, clipRadiusRad);
            if (excursionCount >= kMaxRingExcursions) {
                return Error::TooManyClipCrossings;
            }
            entryBearings[excursionCount] = crossing.lonRad;
            entryRingIdx[excursionCount] = i;
            ++excursionCount;
        } else if (!currInside && prevInside) {
            if (!currHasRotated) {
                currRotated = rotate(rawPoints[i], frame);
            }
            const GeoPoint crossing = detail::clipBoundaryCrossing(prevRotated, currRotated, clipRadiusRad);
            if (excursionCount >= kMaxRingExcursions) {
                return Error::TooManyClipCrossings;
            }
            exitBearings[excursionCount] = crossing.lonRad; // paired with entryBearings[excursionCount], set next
            exitRingIdxPrev[excursionCount] = prevIdx;
        }

        prevIdx = i;
        prevRotated = currRotated;
        prevHasRotated = currHasRotated;
        prevInside = currInside;
    }

    // No excursions at all: the whole ring is inside the clip circle. Every
    // point was already rotated (and cached) by classify() above -- read it
    // back instead of rotating again.
    if (excursionCount == 0) {
        for (std::size_t step = 1; step <= pointCount; ++step) {
            const std::size_t i = (startIdx + step) % pointCount;
            const GeoPoint rotated = rotatedCache[i];
            const Error err = sink(rotated);
            if (err != Error::Ok) {
                return err;
            }
            ++outputCount;
        }
        return ringBoundary(outputCount);
    }

    const std::size_t K = excursionCount;
    const std::size_t N = 2 * K; // total logical crossings

    // ---- Build the rejoin structure (d3-geo's clip/rejoin.js model, see
    // this function's top comment): logical crossing 2j is segment j's own entry, 2j+1
    // is segment j's own exit (into the excursion that follows it in ring
    // order). "subject" order is plain ring-traversal (array index) order;
    // "clip" order is bearing-sorted along the clip boundary. Since both
    // views share the same logical index for a given crossing (nothing is
    // physically moved when building the sorted order -- only a separate
    // index permutation is computed), a crossing's "opposite" view
    // (subject <-> clip) is always the same index; no explicit
    // cross-reference is needed, just toggling which link table is
    // currently being followed. ----
    float bearing[2 * kMaxRingExcursions];
    std::size_t segOf[2 * kMaxRingExcursions];
    bool isEntryMarker[2 * kMaxRingExcursions]; // fixed subject-side marker: true = segment's own entry, false = exit

    for (std::size_t j = 0; j < K; ++j) {
        bearing[2 * j] = entryBearings[j];
        segOf[2 * j] = j;
        isEntryMarker[2 * j] = true;

        bearing[2 * j + 1] = exitBearings[(j + 1) % K];
        segOf[2 * j + 1] = j;
        isEntryMarker[2 * j + 1] = false;
    }

    std::size_t subjectNext[2 * kMaxRingExcursions];
    std::size_t subjectPrev[2 * kMaxRingExcursions];
    for (std::size_t i = 0; i < N; ++i) {
        subjectNext[i] = (i + 1) % N;
        subjectPrev[i] = (i + N - 1) % N;
    }

    std::size_t clipOrder[2 * kMaxRingExcursions];
    for (std::size_t i = 0; i < N; ++i) {
        clipOrder[i] = i;
    }
    // Simple insertion sort -- K is small in practice (real coastline data
    // measured at most a handful of excursions per ring at any tested
    // radius), so this is not a performance concern even at O(K^2).
    for (std::size_t a = 1; a < N; ++a) {
        const std::size_t key = clipOrder[a];
        const float keyBearing = bearing[key];
        std::size_t b = a;
        while (b > 0 && bearing[clipOrder[b - 1]] > keyBearing) {
            clipOrder[b] = clipOrder[b - 1];
            --b;
        }
        clipOrder[b] = key;
    }

    std::size_t clipNext[2 * kMaxRingExcursions];
    std::size_t clipPrev[2 * kMaxRingExcursions];
    for (std::size_t k = 0; k < N; ++k) {
        const std::size_t cur = clipOrder[k];
        clipNext[cur] = clipOrder[(k + 1) % N];
        clipPrev[cur] = clipOrder[(k + N - 1) % N];
    }

    using detail::wrapPi;

    // Reference point for seeding the entry/exit alternation below, plus
    // which sorted interval (refK) it falls in. Deliberately *not* a fixed
    // bearing (e.g. always due "north" of center) -- that reference point's
    // raw position moves as clipRadiusRad changes, and for a real,
    // geometrically complex coastline it will eventually sweep across an
    // actual ring edge as the radius varies, flipping refInsideRing below
    // and corrupting every downstream clipIsEntry value even though nothing
    // about the ring's own crossings changed. Using the bearing at this
    // ring's own widest crossing-gap instead keeps the reference point as
    // far as possible from the coastline detail we already know is nearby.
    std::size_t refK = 0;
    float refBearing = 0.0f;
    {
        float bestGap = -1.0f;
        for (std::size_t k = 0; k < N; ++k) {
            const float a = bearing[clipOrder[k]];
            const float b = bearing[clipOrder[(k + 1) % N]];
            const float gap = (k + 1 < N) ? (b - a) : (b + 2.0f * kPi - a);
            if (gap > bestGap) {
                bestGap = gap;
                refK = k;
                refBearing = wrapPi(a + gap * 0.5f);
            }
        }
    }

    // Is the reference point (on the clip boundary, at refBearing) inside
    // the ring? Raw-lat/lon ray-cast via detail::unrotate to recover
    // the reference point's raw position -- same technique as
    // isCenterEnclosedByRings below. `threshold` computed once, above.
    const GeoPoint refRotated{threshold, refBearing};
    const GeoPoint refRaw = detail::unrotate(refRotated, center);

    bool refInsideRing;
    {
        bool inside = false;
        for (std::size_t step = 0; step < pointCount; ++step) {
            const GeoPoint &p0 = rawPoints[step];
            const GeoPoint &p1 = rawPoints[(step + 1) % pointCount];

            const float delta = wrapPi(p1.lonRad - p0.lonRad);
            if (delta == 0.0f) {
                continue;
            }
            const float refDelta = wrapPi(refRaw.lonRad - p0.lonRad);
            // Distance from p1 to the reference, computed directly rather
            // than as delta - refDelta: needed when refRaw and p1 are the
            // same meridian via different float representations (e.g.
            // -180 vs +180 deg) -- delta and refDelta round independently
            // and can disagree by a tiny epsilon there.
            const float remainingDelta = wrapPi(p1.lonRad - refRaw.lonRad);

            // Half-open interval ([0, delta) vs. (0, delta]) so a ring
            // vertex sitting exactly on the reference's own meridian is
            // attributed to exactly one of its two adjacent edges.
            const bool crosses = (delta > 0.0f)
                ? (refDelta >= 0.0f && remainingDelta > 0.0f)
                : (refDelta <= 0.0f && remainingDelta < 0.0f);
            if (!crosses) {
                continue;
            }

            const float t = refDelta / delta;
            const float crossingLat = p0.latRad + (p1.latRad - p0.latRad) * t;
            if (crossingLat > refRaw.latRad) {
                inside = !inside;
            }
        }
        refInsideRing = inside;
    }

    bool clipIsEntry[2 * kMaxRingExcursions];
    {
        bool cur = refInsideRing;
        for (std::size_t step = 0; step < N; ++step) {
            cur = !cur;
            const std::size_t k = (refK + 1 + step) % N;
            clipIsEntry[clipOrder[k]] = cur;
        }
#ifdef WRENIUM_GEO_DEBUG_REJOIN
        if (pointCount > 500) {
            std::fprintf(stderr, "  refK=%zu N=%zu\n", refK, N);
            for (std::size_t i = 0; i < N; ++i) {
                std::fprintf(stderr, "  idx=%zu seg=%zu isEntryMarker=%d bearing=%.3f clipIsEntry=%d\n",
                    i, segOf[i], (int)isEntryMarker[i], bearing[i] * 180.0f / kPi, (int)clipIsEntry[i]);
            }
        }
#endif
    }

    // Output: ping-pong between the subject list (emit real segment
    // points) and the clip list (emit a boundary arc), moving in the
    // direction fixed by whichever flag is current. Each outer-loop pass
    // traces one disjoint cycle. Reads rotatedCache[idx] rather than
    // rotating again -- classify() already rotated and cached every
    // "inside" point above.
    auto emitSegmentForward = [&](std::size_t seg) -> Error {
        const std::size_t begin = entryRingIdx[seg];
        const std::size_t end = exitRingIdxPrev[(seg + 1) % K];
        std::size_t idx = begin;
        for (;;) {
            const GeoPoint rotated = rotatedCache[idx];
            const Error err = sink(rotated);
            if (err != Error::Ok) {
                return err;
            }
            ++outputCount;
            if (idx == end) {
                break;
            }
            idx = (idx + 1) % pointCount;
        }
        return Error::Ok;
    };

    auto emitSegmentBackward = [&](std::size_t seg) -> Error {
        const std::size_t begin = entryRingIdx[seg];
        const std::size_t end = exitRingIdxPrev[(seg + 1) % K];
        std::size_t idx = end;
        for (;;) {
            const GeoPoint rotated = rotatedCache[idx];
            const Error err = sink(rotated);
            if (err != Error::Ok) {
                return err;
            }
            ++outputCount;
            if (idx == begin) {
                break;
            }
            idx = (idx + pointCount - 1) % pointCount;
        }
        return Error::Ok;
    };

    bool visited[2 * kMaxRingExcursions] = {};
    for (std::size_t startI = 0; startI < N; ++startI) {
        if (visited[startI]) {
            continue;
        }

        const std::size_t cycleStartCount = outputCount;
        std::size_t current = startI;
        bool isSubject = true;

        do {
            visited[current] = true;
            const bool entry = isSubject ? isEntryMarker[current] : clipIsEntry[current];
            std::size_t nextCurrent;
            if (entry) {
                Error err;
                if (isSubject) {
                    err = emitSegmentForward(segOf[current]);
                } else {
                    err = detail::emitBoundaryArc(bearing[current], bearing[clipNext[current]], +1, clipRadiusRad, sink, outputCount);
                }
                if (err != Error::Ok) {
                    return err;
                }
                nextCurrent = isSubject ? subjectNext[current] : clipNext[current];
            } else {
                Error err;
                if (isSubject) {
                    err = emitSegmentBackward(segOf[subjectPrev[current]]);
                } else {
                    err = detail::emitBoundaryArc(bearing[current], bearing[clipPrev[current]], -1, clipRadiusRad, sink, outputCount);
                }
                if (err != Error::Ok) {
                    return err;
                }
                nextCurrent = isSubject ? subjectPrev[current] : clipPrev[current];
            }
            current = nextCurrent;
            isSubject = !isSubject;
        } while (!visited[current]);

        const Error boundaryErr = ringBoundary(outputCount - cycleStartCount);
        if (boundaryErr != Error::Ok) {
            return boundaryErr;
        }
    }

    return Error::Ok;
}

/// Convenience wrapper for the common case of clipping straight into a plain
/// Buffer<GeoPoint, Capacity> -- what standalone clip-stage tests use (the
/// pipeline itself uses clipRingToSink() directly). Ring boundaries (see
/// clipRingToSink()) are ignored: only the flattened point sequence is kept,
/// not which disjoint cycle each point belongs to.
/// @tparam Capacity Output buffer capacity.
/// @tparam MaxRingPoints Bounds the *input* ring's point count, for
/// clipRingToSink's rotated-point cache (stack-allocated here).
/// @param rawPoints Input ring points, unrotated.
/// @param pointCount Number of points at @p rawPoints.
/// @param center The clip circle's center.
/// @param clipRadiusRad Clip radius in radians.
/// @param output Receives the clipped, rotated points.
/// @param outputCount Receives the number of points written to @p output.
/// @return Error::Ok on success, or Error::CapacityExceeded if the result
/// doesn't fit.
template <std::size_t Capacity, std::size_t MaxRingPoints = 256>
inline Error clipRing(const GeoPoint *rawPoints, std::size_t pointCount, const GeoPoint &center, float clipRadiusRad, Buffer<GeoPoint, Capacity> &output, std::size_t &outputCount)
{
    GeoPoint rotatedCache[MaxRingPoints];
    return clipRingToSink(
        rawPoints, pointCount, center, clipRadiusRad, rotatedCache,
        [&output](const GeoPoint &p) { return output.pushBack(p); },
        [](std::size_t) { return Error::Ok; },
        outputCount);
}

/// Clips one *open* polyline against the same clip circle as
/// clipRingToSink() -- used for country border segments, which have no
/// inside/outside concept, so none of clipRingToSink's rejoin machinery is
/// needed: a polyline just enters/exits the circle any number of times,
/// each surviving run emitted independently.
/// @param rawPoints Input polyline points, unrotated.
/// @param pointCount Number of points at @p rawPoints.
/// @param center The clip circle's center.
/// @param clipRadiusRad Clip radius in radians.
/// @param sink Callback `Error(const GeoPoint&)` invoked once per surviving point.
/// @param runBoundary Callback `Error(std::size_t)` invoked once per
/// surviving run, with that run's point count, right after its points
/// reach @p sink. Filtering out runs too short to draw usefully is left
/// to the caller.
/// @param outputCount Receives the total number of points written to @p sink.
/// @return Error::Ok on success, or whatever error @p sink/@p runBoundary returned.
template <typename Sink, typename RunBoundarySink>
inline Error clipLineToSink(const GeoPoint *rawPoints, std::size_t pointCount, const GeoPoint &center, float clipRadiusRad, Sink &&sink, RunBoundarySink &&runBoundary, std::size_t &outputCount)
{
    outputCount = 0;
    if (pointCount == 0) {
        return Error::Ok;
    }

    // Built once per line, reused by every rotate() call below.
    const RotationFrame frame = makeRotationFrame(center);
    const float threshold = kHalfPi - clipRadiusRad;

    // See clipRingToSink's identical classify() for why rotateBegin()/
    // rotateFinish() replace a plain rotate() call here.
    auto classify = [&](std::size_t idx, GeoPoint &rotated, bool &hasRotated) -> bool {
        if (detail::isCheaplyOutside(rawPoints[idx], center, clipRadiusRad)) {
            hasRotated = false;
            return false;
        }
        const RotatePartial partial = rotateBegin(rawPoints[idx], frame);
        if (partial.rotatedLat < threshold) {
            hasRotated = false;
            return false;
        }
        rotated = rotateFinish(partial);
        hasRotated = true;
        return true;
    };

    std::size_t runCount = 0;

    GeoPoint prevRotated{};
    bool prevHasRotated = false;
    bool prevInside = classify(0, prevRotated, prevHasRotated);
    if (prevInside) {
        if (!prevHasRotated) {
            prevRotated = rotate(rawPoints[0], frame);
        }
        const Error err = sink(prevRotated);
        if (err != Error::Ok) {
            return err;
        }
        ++outputCount;
        ++runCount;
    }

    for (std::size_t i = 1; i < pointCount; ++i) {
        GeoPoint currRotated{};
        bool currHasRotated = false;
        const bool currInside = classify(i, currRotated, currHasRotated);

        if (currInside != prevInside) {
            if (!prevHasRotated) {
                prevRotated = rotate(rawPoints[i - 1], frame);
            }
            if (!currHasRotated) {
                currRotated = rotate(rawPoints[i], frame);
            }
            const GeoPoint crossing = currInside
                ? detail::clipBoundaryCrossing(prevRotated, currRotated, clipRadiusRad)
                : detail::clipBoundaryCrossing(currRotated, prevRotated, clipRadiusRad);

            const Error err = sink(crossing);
            if (err != Error::Ok) {
                return err;
            }
            ++outputCount;
            ++runCount;

            if (!currInside) {
                const Error rerr = runBoundary(runCount);
                if (rerr != Error::Ok) {
                    return rerr;
                }
                runCount = 0;
            }
        }

        if (currInside) {
            if (!currHasRotated) {
                currRotated = rotate(rawPoints[i], frame);
            }
            const Error err = sink(currRotated);
            if (err != Error::Ok) {
                return err;
            }
            ++outputCount;
            ++runCount;
        }

        prevRotated = currRotated;
        prevHasRotated = currHasRotated;
        prevInside = currInside;
    }

    if (runCount > 0) {
        const Error err = runBoundary(runCount);
        if (err != Error::Ok) {
            return err;
        }
    }

    return Error::Ok;
}

/// Determines whether @p center itself is enclosed by the input rings --
/// resolves the ambiguity left when clipRingToSink() finds zero surviving
/// rings: that alone doesn't say whether nothing should be drawn (center
/// outside every ring) or the whole clip circle should be filled (center
/// inside one of them).
///
/// Deliberately tested directly in raw (lat, lon) space, not via
/// rotate()+project(): projecting first is numerically unstable for points
/// near @p center's antipode, which corrupts the ray-cast parity. Standard
/// even-odd ray-casting point-in-polygon test (ray cast due north from
/// @p center), matching the OddEvenFill rule the SVG/binary output uses.
///
/// Ported from d3-geo's `src/polygonContains.js`: each edge is tested for
/// whether it crosses the *query's own meridian* independently, using a
/// per-edge local wrap (`wrapPi`), with no reference value shared between
/// edges.
/// @param rawPoints Flattened points across all rings.
/// @param ringSizes Each ring's point count.
/// @param center The point to test.
/// @return True iff @p center is enclosed by an odd number of the rings
/// (the even-odd fill rule).
template <std::size_t MaxRings>
inline bool isCenterEnclosedByRings(const GeoPoint *rawPoints, const Buffer<std::size_t, MaxRings> &ringSizes, const GeoPoint &center)
{
    using detail::wrapPi;

    bool inside = false;
    std::size_t offset = 0;

    for (std::size_t r = 0; r < ringSizes.size(); ++r) {
        const std::size_t ringSize = ringSizes[r];
        for (std::size_t step = 0; step < ringSize; ++step) {
            const GeoPoint &p0 = rawPoints[offset + step];
            const GeoPoint &p1 = rawPoints[offset + (step + 1) % ringSize];

            const float delta = wrapPi(p1.lonRad - p0.lonRad);
            if (delta == 0.0f) {
                continue;
            }
            const float centerDelta = wrapPi(center.lonRad - p0.lonRad);
            // Distance from p1 to center, computed directly (see
            // clipRingToSink's identical check) so a shared vertex can't
            // be claimed by both its adjacent edges.
            const float remainingDelta = wrapPi(p1.lonRad - center.lonRad);

            const bool crosses = (delta > 0.0f)
                ? (centerDelta >= 0.0f && remainingDelta > 0.0f)
                : (centerDelta <= 0.0f && remainingDelta < 0.0f);
            if (!crosses) {
                continue;
            }

            const float t = centerDelta / delta;
            const float crossingLat = p0.latRad + (p1.latRad - p0.latRad) * t;
            if (crossingLat > center.latRad) {
                inside = !inside;
            }
        }
        offset += ringSize;
    }

    return inside;
}

} // namespace wrenium::geo::azimuthal
