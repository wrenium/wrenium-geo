// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

import QtQuick
import QtQuick.Shapes

import wrenium_geo_radar_demo
import wrenium_geo_qt_bridge

// Classic PPI ("Plan Position Indicator") radar scope demo: dark/green
// phosphor theme, a continuously rotating sweep with a fading trail, and
// synthetic target blips that light up as the sweep passes over them and
// fade until the next pass. Built on the same wrenium-geo pipeline (via
// WreniumGeoBridge) as the rotator-controller demo, just styled and
// driven differently.
Window {
    id: root

    width: 900
    height: 900
    visible: true
    title: qsTr("wrenium-geo — Radar Scope Demo")
    color: "#000500"

    // ---- Station / view configuration ----
    // Oulu Airport (EFOU) -- roughly the midpoint of Finland's north-south
    // domestic trunk routes, so traffic between Helsinki and Lapland
    // naturally passes through this station's coverage.
    readonly property real centerLatDeg: 64.9297
    readonly property real centerLonDeg: 25.3706
    // The radar's range limit, i.e. the clip radius: nothing beyond this
    // distance is ever painted. WreniumGeoBridge.projectPoint's `visible`
    // flag applies the same clip-circle test to targets as it does to
    // coastline points, so both leave coverage under the same rule.
    // 600 km matches a real long-range en-route secondary surveillance
    // radar, with enough margin around the seeded targets that the view
    // doesn't feel cramped.
    readonly property real rangeKm: 600.0

    property string currentCoastlineSvgPath: ""
    property string currentBorderSvgPath: ""

    WreniumGeoBridge {
        id: wreniumGeoBridge
    }

    function recomputePath() {
        const viewportRadiusPx = Math.min(scope.width, scope.height) / 2
        currentCoastlineSvgPath = wreniumGeoBridge.computeCoastlineSvgPath(
            centerLatDeg, centerLonDeg, rangeKm, viewportRadiusPx, false)
        currentBorderSvgPath = wreniumGeoBridge.computeBorderSvgPath(
            centerLatDeg, centerLonDeg, rangeKm, viewportRadiusPx, false)
    }

    property double _lastRecomputeMs: 0
    Timer {
        id: recomputeThrottleTimer
        interval: 16
        repeat: false
        onTriggered: root.recomputePathThrottled()
    }
    function recomputePathThrottled() {
        const now = Date.now()
        if (now - _lastRecomputeMs >= 16) {
            _lastRecomputeMs = now
            recomputePath()
        } else if (!recomputeThrottleTimer.running) {
            recomputeThrottleTimer.start()
        }
    }

    Component.onCompleted: recomputePath()

    // ---- Continuously rotating sweep angle ----
    // Drives both the visible sweep line/trail below and every target
    // blip's "time since last swept" brightness (the PPI afterglow
    // effect), all derived from this one value instead of a per-blip timer.
    property real sweepAngleDeg: 0.0
    NumberAnimation on sweepAngleDeg {
        from: 0
        to: 360
        duration: 4000
        loops: Animation.Infinite
        running: true
    }

    // ---- Synthetic targets ----
    // Demo-only simulated contacts, not real traffic/schedule data --
    // positions/bearings are seeded along the real great-circle bearings
    // from Oulu to Finland's other domestic airports, and speeds match
    // real aircraft cruise performance (ATR72 turboprop ~500 km/h,
    // Embraer E190/A320 jet ~800-830 km/h), so the traffic mix looks like
    // genuine Finnish domestic routes rather than arbitrary dots. Each
    // starts with a position, bearing, and cruise speed; the Timer below
    // moves them with a flat-Earth approximation (fine at these speeds/
    // tick intervals) and a time-compression factor (kTimeScaleFactor) so
    // motion is visible within a normal viewing session.
    readonly property var targetSeed: [
        { lat: 62.9537, lon: 25.1429, bearingDeg: 6, speedKmh: 800 },   // HEL-RVN, south of Oulu, inbound
        { lat: 66.1855, lon: 25.1531, bearingDeg: 356, speedKmh: 750 }, // HEL-KTT, past Oulu, outbound north
        { lat: 67.5726, lon: 26.7196, bearingDeg: 11, speedKmh: 820 },  // HEL-IVL, long northern route
        { lat: 65.7282, lon: 28.2389, bearingDeg: 55, speedKmh: 500 },  // HEL-KAO, turboprop
        { lat: 64.5909, lon: 26.6149, bearingDeg: 122, speedKmh: 480 }, // OUL-KAJ, short regional hop
        { lat: 61.3368, lon: 24.9784, bearingDeg: 3, speedKmh: 830 }    // HEL-OUL, near the range's edge, inbound
    ]
    readonly property real kTimeScaleFactor: 25.0

    Item {
        id: scope
        anchors.centerIn: parent
        width: Math.min(root.width, root.height) - 60
        height: width

        onWidthChanged: root.recomputePathThrottled()

        // Scope face: a near-black disc (barely distinct from the
        // window's own background -- "in range" vs. void beyond it,
        // exactly like a physical CRT scope's own faceplate) with a
        // bright green outline ring.
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "#001400"
            border.color: "#1fae55"
            border.width: 2
        }

        // Range rings at 1/4, 1/2, 3/4, and the outer edge itself is the
        // radar's hard range limit (rangeKm) -- azimuthal-equidistant
        // projection means these are always perfect circles regardless of
        // zoom (Main.qml's identical technique/comment).
        Repeater {
            model: 3
            delegate: Rectangle {
                readonly property real ringRadius: (0.25 * (index + 1)) * (scope.width / 2)
                anchors.centerIn: parent
                width: ringRadius * 2
                height: width
                radius: width / 2
                color: "transparent"
                border.color: "#1fae55"
                border.width: 1
                opacity: 0.35
            }
        }
        Repeater {
            model: 3
            delegate: Text {
                readonly property real ringKm: 0.25 * (index + 1) * root.rangeKm
                readonly property real ringRadius: (0.25 * (index + 1)) * (scope.width / 2)
                text: qsTr("%1 km").arg(Math.round(ringKm))
                color: "#33ff66"
                font.family: "monospace"
                font.pixelSize: 11
                opacity: 0.6
                x: scope.width / 2 + ringRadius * Math.sin(Math.PI / 4) - width / 2
                y: scope.height / 2 - ringRadius * Math.cos(Math.PI / 4) - height / 2
            }
        }

        // Compass points around the edge.
        Repeater {
            model: ["N", "E", "S", "W"]
            delegate: Text {
                readonly property real angleDeg: index * 90
                readonly property real ringRadius: scope.width / 2 + 16
                text: modelData
                color: "#33ff66"
                font.family: "monospace"
                font.pixelSize: 16
                font.bold: true
                x: scope.width / 2 + ringRadius * Math.sin(angleDeg * Math.PI / 180) - width / 2
                y: scope.height / 2 - ringRadius * Math.cos(angleDeg * Math.PI / 180) - height / 2
            }
        }

        // Coastline + borders, tinted green -- afterglow-style "land
        // return" rather than a colorful map, closer to what a real radar
        // clutter/land-mass echo reads like on a phosphor scope.
        Shape {
            id: coastlineShape
            anchors.fill: parent
            transform: Translate {
                x: coastlineShape.width / 2
                y: coastlineShape.height / 2
            }

            ShapePath {
                fillColor: "#123d1a"
                strokeColor: "#2f8f4f"
                strokeWidth: 1
                fillRule: ShapePath.OddEvenFill
                PathSvg {
                    path: root.currentCoastlineSvgPath
                }
            }
            ShapePath {
                fillColor: "transparent"
                strokeColor: "#2a5f3a"
                strokeWidth: 1
                PathSvg {
                    path: root.currentBorderSvgPath
                }
            }
        }

        // ---- Targets ----
        // Each blip projects its own live (lat, lon) through
        // wrenium::geo::projectPoint -- `visible` (projection[2]) goes false
        // once the target's distance from center exceeds rangeKm, so it
        // simply stops being painted, like real clutter leaving coverage.
        Repeater {
            id: targets
            model: root.targetSeed.length
            delegate: Item {
                id: blipItem

                property real lat: root.targetSeed[index].lat
                property real lon: root.targetSeed[index].lon
                property real bearingDeg: root.targetSeed[index].bearingDeg
                property real speedKmh: root.targetSeed[index].speedKmh

                readonly property var projection: wreniumGeoBridge.projectPoint(
                    lat, lon, root.centerLatDeg, root.centerLonDeg, root.rangeKm,
                    scope.width / 2)
                visible: projection[2]
                x: scope.width / 2 + projection[0] - width / 2
                y: scope.height / 2 + projection[1] - height / 2
                width: 12
                height: 12

                // This blip's bearing from the scope's center, in the same
                // 0=north/clockwise convention sweepAngleDeg uses, derived
                // from its already-projected (x, y).
                readonly property real blipBearingDeg: {
                    const deg = Math.atan2(projection[0], -projection[1]) * 180 / Math.PI
                    return (deg + 360) % 360
                }

                // Degrees of sweep travel since it last crossed this
                // blip's bearing: 0 right after being swept, growing to
                // 360 just before the next pass. This is the whole
                // "afterglow" effect -- a pure function of the sweep's
                // current angle and the blip's bearing, no per-blip state.
                readonly property real angleSincePass: {
                    let diff = root.sweepAngleDeg - blipBearingDeg
                    diff = ((diff % 360) + 360) % 360
                    return diff
                }
                readonly property real brightness: 1.0 - (angleSincePass / 360.0)

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "#66ff99"
                    opacity: 0.10 + 0.90 * blipItem.brightness
                }
            }
        }

        // ---- Sweep: bright leading edge + translucent trailing wedge ----
        // Local (0, 0) is the scope's center; the Item rotates around
        // that point first, then translates out to the scope's actual
        // center on screen -- continuously animated here rather than
        // driven by a rotator heading.
        Item {
            id: sweep
            anchors.fill: parent
            transform: [
                Rotation {
                    origin.x: 0
                    origin.y: 0
                    angle: root.sweepAngleDeg
                },
                Translate {
                    x: sweep.width / 2
                    y: sweep.height / 2
                }
            ]

            Shape {
                anchors.fill: parent
                ShapePath {
                    id: trailPath
                    fillColor: Qt.rgba(0.2, 1.0, 0.4, 0.14)
                    strokeColor: "transparent"
                    fillRule: ShapePath.OddEvenFill

                    readonly property real radius: scope.width / 2
                    // Rounded off near the pivot (innerRadius) instead of
                    // tapering to a sharp point, matching the already-
                    // rounded outer edge so the trail reads as one soft
                    // wedge, not a hard-cornered pie slice.
                    readonly property real innerRadius: radius * 0.06
                    // Trails behind the current sweep bearing (the
                    // negative-angle side): local angle 0 is always
                    // "wherever the sweep currently points" since this
                    // whole Item is already rotated by sweepAngleDeg.
                    readonly property real trailAngleRad: 25.0 * Math.PI / 180
                    // Negative angle places the wedge at local bearing -25
                    // (behind the sweep, the side it just left) rather
                    // than +25 (ahead of it).
                    readonly property real outerTrailX: radius * Math.sin(-trailAngleRad)
                    readonly property real outerTrailY: -radius * Math.cos(-trailAngleRad)
                    readonly property real innerTrailX: innerRadius * Math.sin(-trailAngleRad)
                    readonly property real innerTrailY: -innerRadius * Math.cos(-trailAngleRad)

                    startX: innerTrailX
                    startY: innerTrailY
                    PathLine {
                        x: trailPath.outerTrailX
                        y: trailPath.outerTrailY
                    }
                    PathArc {
                        x: 0
                        y: -trailPath.radius
                        radiusX: trailPath.radius
                        radiusY: trailPath.radius
                        useLargeArc: false
                        direction: PathArc.Clockwise
                    }
                    PathLine {
                        x: 0
                        y: -trailPath.innerRadius
                    }
                    PathArc {
                        x: trailPath.innerTrailX
                        y: trailPath.innerTrailY
                        radiusX: trailPath.innerRadius
                        radiusY: trailPath.innerRadius
                        useLargeArc: false
                        direction: PathArc.Clockwise
                    }
                }
            }

            // Bright leading-edge line, straight up (local bearing 0)
            // before the Item's own rotation is applied above.
            Rectangle {
                x: -1
                y: -scope.width / 2
                width: 2
                height: scope.width / 2
                color: "#33ff66"
            }
        }

        Text {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 8
            color: "#33ff66"
            font.family: "monospace"
            font.pixelSize: 13
            font.bold: true
            text: qsTr("WRENIUM GEO — RADAR SCOPE  |  RANGE %1 km").arg(Math.round(root.rangeKm))
        }
    }

    // ---- Movement simulation ----
    Timer {
        interval: 200
        running: true
        repeat: true
        onTriggered: {
            const dtHours = (interval / 1000.0 / 3600.0) * root.kTimeScaleFactor

            for (let i = 0; i < targets.count; ++i) {
                const item = targets.itemAt(i)
                if (!item) {
                    continue
                }

                const distKm = item.speedKmh * dtHours
                const advanced = wreniumGeoBridge.destinationPoint(item.lat, item.lon, item.bearingDeg, distKm)
                const newLat = advanced[0]
                const newLon = advanced[1]

                // Real spherical distance from center (not a flat-Earth
                // approximation), only used to decide when to respawn a
                // target that's flown out of coverage -- what actually
                // gets drawn is still governed by projectPoint's own
                // `visible` flag above.
                const distFromCenterKm = wreniumGeoBridge.distanceKm(newLat, newLon, root.centerLatDeg, root.centerLonDeg)

                if (distFromCenterKm > root.rangeKm * 1.15) {
                    // Lost off the edge of the scope -- respawn as a
                    // fresh contact entering coverage from a random
                    // bearing, heading generally back toward the center,
                    // so the demo keeps running indefinitely instead of
                    // every target eventually flying off scope for good.
                    const entryBearingDeg = Math.random() * 360
                    const entryDistKm = root.rangeKm * (0.85 + 0.1 * Math.random())
                    const entryPoint = wreniumGeoBridge.destinationPoint(root.centerLatDeg, root.centerLonDeg, entryBearingDeg, entryDistKm)

                    item.lat = entryPoint[0]
                    item.lon = entryPoint[1]
                    // Head back roughly toward the center (opposite of
                    // the entry bearing), with some spread so it doesn't
                    // fly a perfectly straight radial line every time.
                    item.bearingDeg = (entryBearingDeg + 180 + (Math.random() * 60 - 30)) % 360
                } else {
                    item.lat = newLat
                    item.lon = newLon
                }
            }
        }
    }
}
