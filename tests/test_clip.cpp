// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <cmath>

#include "doctest/doctest.h"

#include "wrenium/geo/buffer.h"
#include "wrenium/geo/detail/azimuthal/clip.h"
#include "wrenium/geo/projection.h"

using namespace wrenium::geo;

namespace {

// Standard forward-geodesic ("destination point given start, bearing,
// distance") formula -- the mathematical inverse of what rotate()
// (detail/azimuthal/rotation.h) computes. Lets tests express scenarios as "a point at
// angular distance D and bearing B from center": destinationPoint(center,
// d, b) constructs a raw point such that rotate(destinationPoint(center,
// d, b), center) yields (latRad = kHalfPi - d, lonRad = b).
GeoPoint destinationPoint(const GeoPoint &center, float distanceRad, float bearingRad)
{
    const float sinLat1 = sinf(center.latRad);
    const float cosLat1 = cosf(center.latRad);
    const float sinD = sinf(distanceRad);
    const float cosD = cosf(distanceRad);

    const float sinLat2 = sinLat1 * cosD + cosLat1 * sinD * cosf(bearingRad);
    const float lat2 = asinf(sinLat2 < -1.0f ? -1.0f : (sinLat2 > 1.0f ? 1.0f : sinLat2));

    const float y = sinf(bearingRad) * sinD * cosLat1;
    const float x = cosD - sinLat1 * sinLat2;
    const float lon2 = center.lonRad + atan2f(y, x);

    return GeoPoint{lat2, lon2};
}

// An arbitrary, non-degenerate center used throughout this file -- not the
// pole and not the equator/prime-meridian special cases, so these tests
// exercise the same rotate() code path pipeline.h actually uses.
constexpr float kTestCenterLat = 0.3f;
constexpr float kTestCenterLon = 0.5f;
GeoPoint testCenter()
{
    return GeoPoint{kTestCenterLat, kTestCenterLon};
}

// A small "square" ring: 4 points all at the same angular distance
// `radiusRad` from `center`, at the four cardinal bearings.
Buffer<GeoPoint, 8> makeSquareRing(const GeoPoint &center, float radiusRad)
{
    Buffer<GeoPoint, 8> ring;
    ring.pushBack(destinationPoint(center, radiusRad, 0.0f));     // north
    ring.pushBack(destinationPoint(center, radiusRad, kHalfPi));  // east
    ring.pushBack(destinationPoint(center, radiusRad, kPi));      // south
    ring.pushBack(destinationPoint(center, radiusRad, -kHalfPi)); // west
    return ring;
}

// Tolerance for round-tripping through destinationPoint() -> rotate()
// (both use several sin/cos/atan2 calls, so expect ordinary float error,
// not bit-exactness).
constexpr float kApproxTolerance = 1e-4f;

} // namespace

TEST_CASE("clip: polygon fully inside the clip radius is preserved unchanged")
{
    // Ring at 10 degrees from center, clip radius 20 degrees -- every vertex
    // is well inside.
    const GeoPoint center = testCenter();
    const float ringRadius = 10.0f * kPi / 180.0f;
    const float clipRadius = 20.0f * kPi / 180.0f;
    Buffer<GeoPoint, 8> ring = makeSquareRing(center, ringRadius);

    Buffer<GeoPoint, 16> output;
    std::size_t outputCount = 0;
    const Error err = clipRing(ring.data(), ring.size(), center, clipRadius, output, outputCount);

    CHECK(err == Error::Ok);
    REQUIRE(outputCount == ring.size());

    const float expectedLat = kHalfPi - ringRadius;
    for (std::size_t i = 0; i < outputCount; ++i) {
        CHECK(output[i].latRad == doctest::Approx(expectedLat).epsilon(kApproxTolerance));
    }
}

TEST_CASE("clip: polygon fully outside the clip radius is dropped entirely")
{
    // Ring at 80 degrees from center, clip radius 20 degrees -- every vertex
    // is well outside.
    const GeoPoint center = testCenter();
    const float ringRadius = 80.0f * kPi / 180.0f;
    const float clipRadius = 20.0f * kPi / 180.0f;
    Buffer<GeoPoint, 8> ring = makeSquareRing(center, ringRadius);

    Buffer<GeoPoint, 16> output;
    std::size_t outputCount = 0;
    const Error err = clipRing(ring.data(), ring.size(), center, clipRadius, output, outputCount);

    CHECK(err == Error::Ok);
    CHECK(outputCount == 0);
}

TEST_CASE("clip: an edge crossing the boundary once is split at the crossing point")
{
    // Three points forming a genuine (non-degenerate) triangle: two inside,
    // one outside, so exactly one excursion with two *distinct* crossing
    // points (entry and exit at different bearings). A 2-point "ring" can't
    // exercise this meaningfully: with only one real edge, interpolating it
    // forwards or backwards always lands on the same point, so entry and
    // exit necessarily coincide -- not a genuine test of bridging two
    // different crossing locations.
    const GeoPoint center = testCenter();
    const float clipRadius = 30.0f * kPi / 180.0f;
    Buffer<GeoPoint, 4> ring;
    ring.pushBack(destinationPoint(center, 10.0f * kPi / 180.0f, 0.0f));           // inside
    ring.pushBack(destinationPoint(center, 50.0f * kPi / 180.0f, kHalfPi * 0.5f)); // outside
    ring.pushBack(destinationPoint(center, 10.0f * kPi / 180.0f, kHalfPi));        // inside

    // The single excursion (around the one outside vertex) is bridged by an
    // arc tracing the clip circle in ~3 degree steps (detail/azimuthal/clip.h's
    // detail_clip::emitBoundaryArc), so the exact output size depends on
    // that step resolution -- generous capacity, structural checks below
    // instead of a brittle exact count.
    Buffer<GeoPoint, 128> output;
    std::size_t outputCount = 0;
    const Error err = clipRing(ring.data(), ring.size(), center, clipRadius, output, outputCount);

    CHECK(err == Error::Ok);
    REQUIRE(outputCount > 4); // at least the 2 inside vertices + 2 crossings

    const float threshold = kHalfPi - clipRadius;
    int crossingCount = 0;
    int nonBoundaryCount = 0;
    for (std::size_t i = 0; i < outputCount; ++i) {
        if (output[i].latRad == doctest::Approx(threshold).epsilon(kApproxTolerance)) {
            ++crossingCount;
        } else {
            ++nonBoundaryCount;
        }
    }
    // Every boundary-arc point (including the two actual crossings) sits
    // exactly at the threshold latitude; only the two real inside vertices
    // don't.
    CHECK(nonBoundaryCount == 2);
    CHECK(crossingCount == outputCount - 2);
}

TEST_CASE("clip: a ring crossing the boundary multiple times keeps every in-arc")
{
    // A ring that dips in and out of the clip circle twice: alternating
    // near/far points around center.
    const GeoPoint center = testCenter();
    const float clipRadius = 30.0f * kPi / 180.0f;
    const float nearDist = 10.0f * kPi / 180.0f; // inside
    const float farDist = 60.0f * kPi / 180.0f;  // outside

    Buffer<GeoPoint, 8> ring;
    ring.pushBack(destinationPoint(center, nearDist, 0.0f));
    ring.pushBack(destinationPoint(center, farDist, kHalfPi * 0.5f));
    ring.pushBack(destinationPoint(center, nearDist, kHalfPi));
    ring.pushBack(destinationPoint(center, farDist, kHalfPi * 1.5f));
    ring.pushBack(destinationPoint(center, nearDist, kPi));
    ring.pushBack(destinationPoint(center, farDist, -kHalfPi * 1.5f));
    ring.pushBack(destinationPoint(center, nearDist, -kHalfPi));
    ring.pushBack(destinationPoint(center, farDist, -kHalfPi * 0.5f));

    // 128 is generous: 4 inside vertices + 2 crossings per excursion (8) +
    // boundary-arc points bridging each excursion's exit/entry bearing gap
    // (detail/azimuthal/clip.h's detail_clip::emitBoundaryArc).
    Buffer<GeoPoint, 128> output;
    std::size_t outputCount = 0;
    const Error err = clipRing(ring.data(), ring.size(), center, clipRadius, output, outputCount);

    CHECK(err == Error::Ok);
    // At least the original 12 (4 inside vertices + 8 crossings) -- the
    // exact count now also depends on detail/azimuthal/clip.h's boundary-arc step size, so
    // this checks the property the fix is actually for instead of a
    // brittle magic number: no two consecutive output points should be
    // farther apart in bearing than a small margin over the arc step size,
    // i.e. the clipped shape's edge actually follows the clip circle
    // through each excursion instead of cutting straight across it.
    REQUIRE(outputCount > 12);

    const float threshold = kHalfPi - clipRadius;
    constexpr float kMaxAllowedBearingGapRad = 3.0f * (3.0f * kPi / 180.0f); // 3x detail/azimuthal/clip.h's arc step, generous margin
    for (std::size_t i = 0; i < outputCount; ++i) {
        const GeoPoint &a = output[i];
        const GeoPoint &b = output[(i + 1) % outputCount];
        // Only meaningful for points actually on the boundary (both
        // arc points and crossing points sit exactly at latRad ==
        // threshold) -- inside vertices can legitimately be far apart in
        // bearing from their neighbors.
        const bool aOnBoundary = std::fabs(a.latRad - threshold) < 1e-4f;
        const bool bOnBoundary = std::fabs(b.latRad - threshold) < 1e-4f;
        if (aOnBoundary && bOnBoundary) {
            float gap = b.lonRad - a.lonRad;
            while (gap > kPi) {
                gap -= 2.0f * kPi;
            }
            while (gap <= -kPi) {
                gap += 2.0f * kPi;
            }
            CHECK(std::fabs(gap) <= kMaxAllowedBearingGapRad);
        }
    }
}

TEST_CASE("clip: a point exactly on the boundary counts as inside")
{
    const GeoPoint center = testCenter();
    const float clipRadius = 30.0f * kPi / 180.0f;

    Buffer<GeoPoint, 4> ring;
    ring.pushBack(destinationPoint(center, clipRadius, 0.0f));          // exactly on the boundary
    ring.pushBack(destinationPoint(center, 5.0f * kPi / 180.0f, 1.0f)); // well inside

    Buffer<GeoPoint, 16> output;
    std::size_t outputCount = 0;
    const Error err = clipRing(ring.data(), ring.size(), center, clipRadius, output, outputCount);

    CHECK(err == Error::Ok);
    // Both points count as inside (>= threshold), so both survive with no
    // synthetic crossing point inserted. Checking that the boundary point
    // survives *somewhere* in the output, not specifically at output[0] --
    // detail/azimuthal/clip.h picks whichever vertex it finds inside first as the walk's
    // starting point (see its top comment), so which index ends up first
    // isn't part of this test's contract, only that both points are
    // present and unmodified.
    REQUIRE(outputCount == 2);
    const float threshold = kHalfPi - clipRadius;
    bool foundBoundaryPoint = false;
    for (std::size_t i = 0; i < outputCount; ++i) {
        if (output[i].latRad == doctest::Approx(threshold).epsilon(kApproxTolerance)) {
            foundBoundaryPoint = true;
        }
    }
    CHECK(foundBoundaryPoint);
}

TEST_CASE("clip: radius near zero keeps only points essentially at the center")
{
    const GeoPoint center = testCenter();
    const float clipRadius = 0.0001f; // near-zero clip radius
    Buffer<GeoPoint, 4> ring;
    ring.pushBack(center);                                              // exactly at center
    ring.pushBack(destinationPoint(center, 1.0f * kPi / 180.0f, 0.0f)); // 1 degree away -- outside

    Buffer<GeoPoint, 16> output;
    std::size_t outputCount = 0;
    const Error err = clipRing(ring.data(), ring.size(), center, clipRadius, output, outputCount);

    CHECK(err == Error::Ok);
    // A 2-point ring has only one real edge (A->B and the wrap-around B->A
    // interpolate the exact same two points, so the exit and entry
    // crossings necessarily coincide) -- just the center point (A) itself
    // survives, with no separate crossing point needed since the bridging
    // arc between coincident exit/entry bearings has zero length.
    REQUIRE(outputCount == 1);
    for (std::size_t i = 0; i < outputCount; ++i) {
        CHECK(output[i].latRad > kHalfPi - 1.0f * kPi / 180.0f);
    }
}

TEST_CASE("clip: radius near the antipodal maximum keeps everything")
{
    const GeoPoint center = testCenter();
    const float clipRadius = kPi - 0.0001f;                                 // just short of the true antipodal max (pi)
    Buffer<GeoPoint, 8> ring = makeSquareRing(center, 0.5f * kPi - 0.001f); // near-antipodal ring

    Buffer<GeoPoint, 16> output;
    std::size_t outputCount = 0;
    const Error err = clipRing(ring.data(), ring.size(), center, clipRadius, output, outputCount);

    CHECK(err == Error::Ok);
    CHECK(outputCount == ring.size());
}

TEST_CASE("clip: a ring mostly outside the clip circle traces the long way around, not the short way")
{
    // A ring that dips *inside* the clip circle for only a small notch
    // (~20 degrees of bearing), then stays *outside* for the rest of its
    // span (~340 degrees). The correct clipped shape traces the clip
    // circle's boundary the *long* way around through that 340-degree
    // excursion, not the short (~20 degree) way.
    const GeoPoint center = testCenter();
    const float clipRadius = 30.0f * kPi / 180.0f;
    const float nearDist = 10.0f * kPi / 180.0f; // inside
    const float farDist = 80.0f * kPi / 180.0f;  // outside

    const float degToRad = kPi / 180.0f;
    Buffer<GeoPoint, 16> ring;
    ring.pushBack(destinationPoint(center, nearDist, -10.0f * degToRad));
    ring.pushBack(destinationPoint(center, nearDist, 0.0f));
    ring.pushBack(destinationPoint(center, nearDist, 10.0f * degToRad));
    ring.pushBack(destinationPoint(center, farDist, 60.0f * degToRad));
    ring.pushBack(destinationPoint(center, farDist, 120.0f * degToRad));
    ring.pushBack(destinationPoint(center, farDist, 180.0f * degToRad));
    ring.pushBack(destinationPoint(center, farDist, 240.0f * degToRad));
    ring.pushBack(destinationPoint(center, farDist, 300.0f * degToRad));

    Buffer<GeoPoint, 256> output;
    std::size_t outputCount = 0;
    const Error err = clipRing(ring.data(), ring.size(), center, clipRadius, output, outputCount);

    CHECK(err == Error::Ok);
    // The short way (~20 degrees) would need only a handful of points; the
    // long way (~340 degrees) needs well over a hundred at the 3-degree
    // step detail/azimuthal/clip.h uses. This is the key regression guard: it fails if the
    // bug (always taking the short way) comes back.
    REQUIRE(outputCount > 80);

    // Same structural check as the "multiple crossings" test above: no two
    // consecutive boundary points should be farther apart in bearing than a
    // small margin over the arc step size, confirming the traced arc
    // actually follows the clip circle continuously rather than jumping.
    const float threshold = kHalfPi - clipRadius;
    constexpr float kMaxAllowedBearingGapRad = 3.0f * (3.0f * kPi / 180.0f);
    for (std::size_t i = 0; i < outputCount; ++i) {
        const GeoPoint &a = output[i];
        const GeoPoint &b = output[(i + 1) % outputCount];
        const bool aOnBoundary = std::fabs(a.latRad - threshold) < 1e-4f;
        const bool bOnBoundary = std::fabs(b.latRad - threshold) < 1e-4f;
        if (aOnBoundary && bOnBoundary) {
            float gap = b.lonRad - a.lonRad;
            while (gap > kPi) {
                gap -= 2.0f * kPi;
            }
            while (gap <= -kPi) {
                gap += 2.0f * kPi;
            }
            CHECK(std::fabs(gap) <= kMaxAllowedBearingGapRad);
        }
    }
}

TEST_CASE("clip: isCenterEnclosedByRings reports the center inside a surrounding ring")
{
    // A square ring surrounding `center` at 10 degrees -- center itself is
    // nowhere near the ring's own vertices/edges, so this exercises the
    // "well inside" case, not a boundary-adjacent edge case.
    const GeoPoint center = testCenter();
    Buffer<GeoPoint, 8> ring = makeSquareRing(center, 10.0f * kPi / 180.0f);

    Buffer<std::size_t, 4> ringSizes;
    ringSizes.pushBack(ring.size());

    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, center));
}

TEST_CASE("clip: isCenterEnclosedByRings reports the center outside a distant ring")
{
    // A square ring far from `center` (55 degrees away) -- center is
    // nowhere near it at all. Deliberately not 80 degrees: testCenter()
    // sits at 17.19N, and 17.19+80+10(ring radius) wraps past the north
    // pole, producing a ring that (by construction accident, not intent)
    // encloses the pole itself -- a case isCenterEnclosedByRings's own
    // comment explicitly documents as out of scope (no real coastline ring
    // ever passes through a pole). 55 degrees keeps every ring vertex
    // safely under 90 degrees latitude.
    const GeoPoint center = testCenter();
    const GeoPoint farAway = destinationPoint(center, 55.0f * kPi / 180.0f, 0.0f);
    Buffer<GeoPoint, 8> ring = makeSquareRing(farAway, 10.0f * kPi / 180.0f);

    Buffer<std::size_t, 4> ringSizes;
    ringSizes.pushBack(ring.size());

    CHECK_FALSE(isCenterEnclosedByRings(ring.data(), ringSizes, center));
}

TEST_CASE("clip: sink overflow is reported as Error::CapacityExceeded, not UB")
{
    const GeoPoint center = testCenter();
    const float ringRadius = 10.0f * kPi / 180.0f;
    const float clipRadius = 20.0f * kPi / 180.0f;
    Buffer<GeoPoint, 8> ring = makeSquareRing(center, ringRadius);

    // Output buffer far too small to hold the whole (fully-inside) ring.
    Buffer<GeoPoint, 2> output;
    std::size_t outputCount = 0;
    const Error err = clipRing(ring.data(), ring.size(), center, clipRadius, output, outputCount);

    CHECK(err == Error::CapacityExceeded);
    CHECK(output.size() == output.capacity());
}

// ---- clipLineToSink: border-line (open polyline) clipping, detail/azimuthal/clip.h ----
//
// A thin, separate helper mirroring clipRing's convenience-wrapper role
// above, but collecting runBoundary sizes too (unlike clipRing, callers of
// clipLineToSink genuinely care where each disjoint run starts/ends, since
// an open polyline's runs are independent subpaths, not one ring).
namespace {

struct LineClipResult
{
    Buffer<GeoPoint, 256> points;
    Buffer<std::size_t, 32> runSizes;
};

LineClipResult clipLine(const GeoPoint *rawPoints, std::size_t pointCount, const GeoPoint &center, float clipRadiusRad)
{
    LineClipResult result;
    std::size_t outputCount = 0;
    const Error err = clipLineToSink(
        rawPoints, pointCount, center, clipRadiusRad,
        [&result](const GeoPoint &p) { return result.points.pushBack(p); },
        [&result](std::size_t runSize) { return result.runSizes.pushBack(runSize); },
        outputCount);
    REQUIRE(err == Error::Ok);
    CHECK(outputCount == result.points.size());
    return result;
}

} // namespace

TEST_CASE("clipLineToSink: a polyline fully inside the clip radius is preserved unchanged")
{
    const GeoPoint center = testCenter();
    const float clipRadius = 30.0f * kPi / 180.0f;
    const float degToRad = kPi / 180.0f;

    Buffer<GeoPoint, 4> line;
    line.pushBack(destinationPoint(center, 5.0f * degToRad, 0.0f));
    line.pushBack(destinationPoint(center, 8.0f * degToRad, 30.0f * degToRad));
    line.pushBack(destinationPoint(center, 10.0f * degToRad, 60.0f * degToRad));

    const LineClipResult result = clipLine(line.data(), line.size(), center, clipRadius);

    REQUIRE(result.runSizes.size() == 1);
    CHECK(result.runSizes[0] == line.size());
    CHECK(result.points.size() == line.size());
}

TEST_CASE("clipLineToSink: a polyline fully outside the clip radius produces nothing")
{
    const GeoPoint center = testCenter();
    const float clipRadius = 20.0f * kPi / 180.0f;
    const float degToRad = kPi / 180.0f;

    Buffer<GeoPoint, 4> line;
    line.pushBack(destinationPoint(center, 60.0f * degToRad, 0.0f));
    line.pushBack(destinationPoint(center, 65.0f * degToRad, 10.0f * degToRad));

    const LineClipResult result = clipLine(line.data(), line.size(), center, clipRadius);

    CHECK(result.runSizes.size() == 0);
    CHECK(result.points.size() == 0);
}

TEST_CASE("clipLineToSink: exiting then re-entering produces two separate runs, not one connected chord")
{
    // inside -> outside -> inside: an open polyline has no fill-rule/
    // enclosure concept at all (unlike clipRingToSink), so the correct
    // result is two independent runs (entry..exit, entry..end), never a
    // single loop bridged across the outside excursion.
    const GeoPoint center = testCenter();
    const float clipRadius = 30.0f * kPi / 180.0f;
    const float degToRad = kPi / 180.0f;

    Buffer<GeoPoint, 4> line;
    line.pushBack(destinationPoint(center, 10.0f * degToRad, 0.0f));             // inside
    line.pushBack(destinationPoint(center, 60.0f * degToRad, 45.0f * degToRad)); // outside
    line.pushBack(destinationPoint(center, 10.0f * degToRad, 90.0f * degToRad)); // inside

    const LineClipResult result = clipLine(line.data(), line.size(), center, clipRadius);

    REQUIRE(result.runSizes.size() == 2);
    CHECK(result.runSizes[0] == 2); // first inside vertex + exit crossing
    CHECK(result.runSizes[1] == 2); // entry crossing + last inside vertex

    const float threshold = kHalfPi - clipRadius;
    // No boundary-following arc is ever inserted for an open line (unlike
    // clipRingToSink) -- both runs' boundary-adjacent point sits exactly at
    // the crossing latitude, with nothing else in between.
    CHECK(result.points[1].latRad == doctest::Approx(threshold).epsilon(kApproxTolerance));
    CHECK(result.points[2].latRad == doctest::Approx(threshold).epsilon(kApproxTolerance));
}

TEST_CASE("clipLineToSink: a polyline that starts outside and ends inside yields one run")
{
    const GeoPoint center = testCenter();
    const float clipRadius = 30.0f * kPi / 180.0f;
    const float degToRad = kPi / 180.0f;

    Buffer<GeoPoint, 4> line;
    line.pushBack(destinationPoint(center, 60.0f * degToRad, 0.0f));             // outside
    line.pushBack(destinationPoint(center, 10.0f * degToRad, 30.0f * degToRad)); // inside

    const LineClipResult result = clipLine(line.data(), line.size(), center, clipRadius);

    REQUIRE(result.runSizes.size() == 1);
    CHECK(result.runSizes[0] == 2); // entry crossing + the inside endpoint
}

TEST_CASE("clipLineToSink: sink overflow is reported as Error::CapacityExceeded, not UB")
{
    const GeoPoint center = testCenter();
    const float clipRadius = 30.0f * kPi / 180.0f;
    const float degToRad = kPi / 180.0f;

    Buffer<GeoPoint, 4> line;
    line.pushBack(destinationPoint(center, 5.0f * degToRad, 0.0f));
    line.pushBack(destinationPoint(center, 8.0f * degToRad, 30.0f * degToRad));
    line.pushBack(destinationPoint(center, 10.0f * degToRad, 60.0f * degToRad));

    Buffer<GeoPoint, 2> output;
    std::size_t outputCount = 0;
    const Error err = clipLineToSink(
        line.data(), line.size(), center, clipRadius,
        [&output](const GeoPoint &p) { return output.pushBack(p); },
        [](std::size_t) { return Error::Ok; },
        outputCount);

    CHECK(err == Error::CapacityExceeded);
    CHECK(output.size() == output.capacity());
}

// ---- detail_clip::unrotate: pole singularity regression ----
//
// When center is exactly at a pole, unrotate()'s general formula hits a
// singularity (see detail/azimuthal/clip.h's own comment) and can return the wrong
// longitude by 180 degrees. Since clipRingToSink relies on this function
// for its single rejoin anchor point, that error flips every crossing's
// inside/outside assignment for the whole ring.
TEST_CASE("clip: unrotate recovers the correct longitude when center is exactly at the north pole (real bug regression)")
{
    const float degToRad = kPi / 180.0f;
    const GeoPoint center{kHalfPi, 25.74f * degToRad};
    const float clipRadiusRad = 8000.0f / kEarthRadiusKm;

    // The exact reference point clipRingToSink's anchor fact uses: bearing
    // 0, at the clip radius.
    const GeoPoint rotated{kHalfPi - clipRadiusRad, 0.0f};
    const GeoPoint raw = detail_clip::unrotate(rotated, center);

    // At the north pole, every point at the same central angle shares the
    // same colatitude regardless of bearing.
    CHECK(raw.latRad == doctest::Approx(kHalfPi - clipRadiusRad).epsilon(1e-3));

    // Correct answer is center.lonRad + 180 degrees -- not center.lonRad
    // itself, which is what the singularity actually produces.
    float expectedLon = center.lonRad + kPi;
    while (expectedLon > kPi) {
        expectedLon -= 2.0f * kPi;
    }
    CHECK(raw.lonRad == doctest::Approx(expectedLon).epsilon(1e-2));
}

TEST_CASE("clip: unrotate stays correct across several bearings at both poles, matching the near-pole limit")
{
    // Confirms the pole special case matches the general formula's own
    // limit as it approaches the pole, not just self-consistency.
    const float degToRad = kPi / 180.0f;
    const float clipRadiusRad = 20.0f * degToRad;

    for (const float centerLatDeg : {90.0f, -90.0f}) {
        const float nearPoleLatDeg = centerLatDeg > 0.0f ? 89.999f : -89.999f;
        const GeoPoint centerAtPole{centerLatDeg * degToRad, 25.74f * degToRad};
        const GeoPoint centerNearPole{nearPoleLatDeg * degToRad, 25.74f * degToRad};

        for (const float bearingDeg : {0.0f, 45.0f, 90.0f, 180.0f, 270.0f}) {
            const GeoPoint rotated{kHalfPi - clipRadiusRad, bearingDeg * degToRad};

            const GeoPoint atPole = detail_clip::unrotate(rotated, centerAtPole);
            const GeoPoint nearPole = detail_clip::unrotate(rotated, centerNearPole);

            CAPTURE(centerLatDeg);
            CAPTURE(bearingDeg);
            CHECK(atPole.latRad == doctest::Approx(nearPole.latRad).epsilon(1e-2));

            float lonDiff = atPole.lonRad - nearPole.lonRad;
            while (lonDiff > kPi) {
                lonDiff -= 2.0f * kPi;
            }
            while (lonDiff <= -kPi) {
                lonDiff += 2.0f * kPi;
            }
            CHECK(std::fabs(lonDiff) < 0.01f);
        }
    }
}

// ---- isCenterEnclosedByRings: wide-ring ray-cast correctness ----
//
// A ring spanning more than half the globe's longitude needs the same
// per-edge meridian-crossing test as any other ring -- a shortest-
// angular-distance shortcut gets "which arc is the interior" backwards
// for a ring this wide. This rectangle exercises exactly that property,
// with every edge kept safely short; its expected answer is independently
// verified against a textbook from-scratch ray-cast implementation.
TEST_CASE("clip: isCenterEnclosedByRings correctly classifies a ring spanning most of the globe's longitude")
{
    const float degToRad = kPi / 180.0f;

    Buffer<GeoPoint, 16> ring;
    // Top edge, west to east: lon -100 -> 100, five points, ~50 deg/edge.
    for (float lonDeg : {-100.0f, -50.0f, 0.0f, 50.0f, 100.0f}) {
        ring.pushBack(GeoPoint{10.0f * degToRad, lonDeg * degToRad});
    }
    // Bottom edge, east to west: lon 100 -> -100, five points.
    for (float lonDeg : {100.0f, 50.0f, 0.0f, -50.0f, -100.0f}) {
        ring.pushBack(GeoPoint{-10.0f * degToRad, lonDeg * degToRad});
    }
    // Ring closes implicitly back to its own first point (matching every
    // other ring in this codebase's convention -- no duplicated closing
    // vertex).

    Buffer<std::size_t, 4> ringSizes;
    ringSizes.pushBack(ring.size());

    // Inside: comfortably within the -100..100 longitude band.
    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, 0.0f}));
    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{5.0f * degToRad, 80.0f * degToRad}));
    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{-5.0f * degToRad, -80.0f * degToRad}));

    // Outside: in the ~160-degree exterior gap on the *other* side of the
    // globe (100 to -100 the short way, through +-180) -- exactly the
    // configuration ("is a point in the minority arc, or the majority
    // arc") a shortest-angular-distance heuristic gets backwards for a
    // ring spanning more than half the globe.
    CHECK_FALSE(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, 170.0f * degToRad}));
    CHECK_FALSE(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, -150.0f * degToRad}));
    CHECK_FALSE(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, 180.0f * degToRad}));

    // Well outside in latitude too (above/below the ring's own band), at a
    // longitude comfortably inside its span -- should never be inside
    // regardless of longitude.
    CHECK_FALSE(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{50.0f * degToRad, 0.0f}));
}

// A ring whose first point sits at the antimeridian's edge and whose
// path drifts past it: a query's meridian offset from the ring's first
// point can land outside the ring's own drift range even though the
// query is genuinely inside the covered area, which is why the per-edge
// test (not a whole-ring embedding) is needed.
TEST_CASE("clip: isCenterEnclosedByRings correctly classifies a ring whose first point sits at the antimeridian and drifts past it")
{
    const float degToRad = kPi / 180.0f;

    Buffer<GeoPoint, 16> ring;
    // Top edge: 0 -> -35 -> -70 -> -105 -> -140 -> -175 -> 150, each edge a
    // short (35 degree) local step; the last edge crosses the antimeridian,
    // continuing the same consistent direction rather than re-normalizing.
    for (float lonDeg : {0.0f, -35.0f, -70.0f, -105.0f, -140.0f, -175.0f, 150.0f}) {
        ring.pushBack(GeoPoint{10.0f * degToRad, lonDeg * degToRad});
    }
    // Bottom edge: the same longitudes, retraced in reverse, closing the
    // ring back to its own first point (0, 10 degrees).
    for (float lonDeg : {150.0f, -175.0f, -140.0f, -105.0f, -70.0f, -35.0f, 0.0f}) {
        ring.pushBack(GeoPoint{-10.0f * degToRad, lonDeg * degToRad});
    }

    Buffer<std::size_t, 4> ringSizes;
    ringSizes.pushBack(ring.size());

    // Inside: comfortably within the covered arc (0 back through -175 to
    // 150, i.e. everything except the ~150-degree gap from 0 to 150 going
    // the other, direct way).
    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, -90.0f * degToRad}));
    // Inside, *far side* of the ring's own first point -- exactly the
    // configuration that broke: the query's wrapPi'd offset from the
    // ring's first point (150 degrees) is undefined relative to that
    // point's own cumulative range without crossing the seam again.
    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, 155.0f * degToRad}));
    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, -178.0f * degToRad}));

    // Outside: the ~150-degree gap between 0 and 150 the direct way.
    CHECK_FALSE(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, 75.0f * degToRad}));
    CHECK_FALSE(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, 10.0f * degToRad}));
    CHECK_FALSE(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, 140.0f * degToRad}));
}

// A ring vertex sitting exactly on the query's own meridian needs the
// half-open crossing interval to be attributed to exactly one of its two
// adjacent edges -- never both (silently cancels out) or neither (missed
// crossing, wrong parity).
TEST_CASE("clip: isCenterEnclosedByRings handles a query meridian passing exactly through a ring vertex")
{
    const float degToRad = kPi / 180.0f;

    Buffer<GeoPoint, 8> ring;
    ring.pushBack(GeoPoint{10.0f * degToRad, -50.0f * degToRad});
    ring.pushBack(GeoPoint{10.0f * degToRad, 0.0f * degToRad}); // vertex exactly on the tested meridian
    ring.pushBack(GeoPoint{10.0f * degToRad, 50.0f * degToRad});
    ring.pushBack(GeoPoint{-10.0f * degToRad, 50.0f * degToRad});
    ring.pushBack(GeoPoint{-10.0f * degToRad, 0.0f * degToRad}); // vertex exactly on the tested meridian
    ring.pushBack(GeoPoint{-10.0f * degToRad, -50.0f * degToRad});

    Buffer<std::size_t, 4> ringSizes;
    ringSizes.pushBack(ring.size());

    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, 0.0f}));
}

// A ring vertex and the query can sit on the exact same meridian while
// being *different* float values (e.g. a vertex at exactly -180 degrees,
// a query at exactly +180 degrees) -- the crossing test must still
// attribute the vertex to exactly one adjacent edge in that case, not
// let both (or neither) claim it.
TEST_CASE("clip: isCenterEnclosedByRings handles a ring vertex and query on the same meridian via different float representations")
{
    const float degToRad = kPi / 180.0f;

    // Same symmetric-hexagon shape as the previous test (vertex on both
    // top and bottom edges, so the tested meridian is genuinely interior
    // to the ring), shifted so the shared vertex sits at exactly -180
    // degrees.
    Buffer<GeoPoint, 6> ring;
    ring.pushBack(GeoPoint{10.0f * degToRad, 130.0f * degToRad});
    ring.pushBack(GeoPoint{10.0f * degToRad, -kPi}); // exactly -180 degrees
    ring.pushBack(GeoPoint{10.0f * degToRad, -130.0f * degToRad});
    ring.pushBack(GeoPoint{-10.0f * degToRad, -130.0f * degToRad});
    ring.pushBack(GeoPoint{-10.0f * degToRad, -kPi}); // exactly -180 degrees
    ring.pushBack(GeoPoint{-10.0f * degToRad, 130.0f * degToRad});

    Buffer<std::size_t, 4> ringSizes;
    ringSizes.pushBack(ring.size());

    // Query at exactly +180 degrees -- the same meridian as the ring's own
    // -180-degree vertices, via the opposite float representation. Must
    // agree with immediately neighboring queries, not flip only at this
    // exact value.
    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, kPi}));
    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, kPi - 0.001f}));
    CHECK(isCenterEnclosedByRings(ring.data(), ringSizes, GeoPoint{0.0f, -kPi + 0.001f}));
}
