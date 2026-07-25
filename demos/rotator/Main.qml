// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

import wrenium_geo_demo
import wrenium_geo_qt_bridge

ApplicationWindow {
    id: root

    width: 1140
    height: 760
    visible: true
    title: qsTr("wrenium-geo — Rotator Controller Demo")

    // Instrument-panel theme: dark graphite chassis + amber accent, like a
    // real antenna rotator controller's front panel. Palette drives the
    // Fusion style's Slider/Switch colors too.
    color: "#12151c"
    palette.window: "#12151c"
    palette.windowText: "#e5eaf0"
    palette.base: "#1b212b"
    palette.text: "#e5eaf0"
    palette.button: "#232a35"
    palette.buttonText: "#e5eaf0"
    palette.highlight: "#e8a33d"
    palette.highlightedText: "#0b0d11"
    palette.mid: "#3a4452"
    palette.dark: "#0b0d11"
    palette.light: "#2e3642"

    // recomputePath() is the single choke point into wrenium-geo (via
    // WreniumGeoBridge below). No debouncing needed: the pipeline runs in
    // sub-millisecond time even on slider drags.
    //
    // headingDeg/targetHeadingDeg never trigger recomputePath() -- heading
    // is a cheap post-projection rotation, not a pipeline re-run. Both come
    // from RotatorDriver rather than being set directly, since a real
    // rotator's motor turns gradually: the needle/AZ readout reflect its
    // actual, lagging position, not the requested target.

    // Default antenna/QTH location: Oulu, Finland.
    property real centerLatDeg: 64.9297
    property real centerLonDeg: 25.3706
    readonly property real headingDeg: rotatorDriver.heading
    readonly property real targetHeadingDeg: rotatorDriver.target
    property real clipRadiusKm: 8000.0
    // Heading wedge's total angular width (half-power/-3dB antenna
    // beamwidth) -- adjustable since it varies a lot by antenna type.
    property real beamwidthDeg: 50.0
    property bool useBinaryEmitter: false
    property bool showBorders: true
    property string currentSvgPath: ""
    property string currentBorderSvgPath: ""
    // Dragging the map either pans it (false) or aims the heading wedge
    // at the drag position (true) -- see aimAt() below.
    property bool dragAimsAntenna: true

    // Demonstrates WreniumGeoBridge.projectPoint(): places amateur radio
    // DXCC/ITU-prefix reference markers at their exact (x, y) position in
    // the same coordinate space the SVG path output uses. Each marker's
    // own Repeater delegate (below) calls projectPoint() directly in a
    // property binding, so it stays live as the map moves/zooms.
    readonly property var amateurPrefixes: [
        // ---- Americas ----
        { prefix: "W", latDeg: 39.8283, lonDeg: -98.5795 },   // USA (contiguous)
        { prefix: "KH6", latDeg: 20.0, lonDeg: -157.5 },      // USA (Hawaii, separate DXCC entity)
        { prefix: "VE", latDeg: 62.4, lonDeg: -96.8 },        // Canada
        { prefix: "XE", latDeg: 23.6345, lonDeg: -102.5528 }, // Mexico
        { prefix: "CO", latDeg: 21.5, lonDeg: -79.0 },        // Cuba
        { prefix: "6Y", latDeg: 18.1, lonDeg: -77.3 },        // Jamaica
        { prefix: "KP4", latDeg: 18.2, lonDeg: -66.5 },       // Puerto Rico
        { prefix: "HP", latDeg: 8.5, lonDeg: -80.0 },         // Panama
        { prefix: "HK", latDeg: 4.5, lonDeg: -74.0 },         // Colombia
        { prefix: "YV", latDeg: 8.0, lonDeg: -66.0 },         // Venezuela
        { prefix: "OA", latDeg: -9.2, lonDeg: -75.0 },        // Peru
        { prefix: "CP", latDeg: -17.0, lonDeg: -65.0 },       // Bolivia
        { prefix: "PY", latDeg: -10.3333, lonDeg: -53.2 },    // Brazil
        { prefix: "CE", latDeg: -35.0, lonDeg: -71.0 },       // Chile
        { prefix: "LU", latDeg: -35.4, lonDeg: -65.0 },       // Argentina
        { prefix: "CX", latDeg: -32.5, lonDeg: -56.0 },       // Uruguay

        // ---- Europe ----
        { prefix: "G", latDeg: 52.8, lonDeg: -1.5 },          // England
        { prefix: "EI", latDeg: 53.4, lonDeg: -8.0 },         // Ireland
        { prefix: "F", latDeg: 46.8, lonDeg: 2.5 },           // France
        { prefix: "ON", latDeg: 50.6, lonDeg: 4.5 },          // Belgium
        { prefix: "PA", latDeg: 52.3, lonDeg: 5.75 },         // Netherlands
        { prefix: "LX", latDeg: 49.8, lonDeg: 6.1 },          // Luxembourg
        { prefix: "DL", latDeg: 51.0, lonDeg: 10.3 },         // Germany
        { prefix: "HB9", latDeg: 46.8, lonDeg: 8.2 },         // Switzerland
        { prefix: "OE", latDeg: 47.5, lonDeg: 14.5 },         // Austria
        { prefix: "I", latDeg: 42.5, lonDeg: 12.5 },          // Italy
        { prefix: "EA", latDeg: 40.0, lonDeg: -3.7 },         // Spain
        { prefix: "CT", latDeg: 39.5, lonDeg: -8.0 },         // Portugal
        { prefix: "LA", latDeg: 62.0, lonDeg: 10.0 },         // Norway
        { prefix: "OZ", latDeg: 56.0, lonDeg: 10.0 },         // Denmark
        { prefix: "OH", latDeg: 64.5, lonDeg: 26.0 },         // Finland
        { prefix: "SM", latDeg: 62.0, lonDeg: 15.0 },         // Sweden
        { prefix: "TF", latDeg: 65.0, lonDeg: -18.0 },        // Iceland
        { prefix: "SP", latDeg: 52.0, lonDeg: 19.5 },         // Poland
        { prefix: "OK", latDeg: 49.8, lonDeg: 15.5 },         // Czech Republic
        { prefix: "OM", latDeg: 48.7, lonDeg: 19.5 },         // Slovakia
        { prefix: "HA", latDeg: 47.2, lonDeg: 19.5 },         // Hungary
        { prefix: "9A", latDeg: 45.5, lonDeg: 16.0 },         // Croatia
        { prefix: "S5", latDeg: 46.1, lonDeg: 14.8 },         // Slovenia
        { prefix: "YU", latDeg: 44.0, lonDeg: 21.0 },         // Serbia
        { prefix: "YO", latDeg: 45.9, lonDeg: 24.9 },         // Romania
        { prefix: "LZ", latDeg: 42.7, lonDeg: 25.5 },         // Bulgaria
        { prefix: "SV", latDeg: 39.0, lonDeg: 22.0 },         // Greece
        { prefix: "ES", latDeg: 58.7, lonDeg: 25.5 },         // Estonia
        { prefix: "YL", latDeg: 56.9, lonDeg: 24.6 },         // Latvia
        { prefix: "LY", latDeg: 55.3, lonDeg: 23.9 },         // Lithuania
        { prefix: "UR", latDeg: 49.0, lonDeg: 32.0 },         // Ukraine
        { prefix: "EW", latDeg: 53.7, lonDeg: 28.0 },         // Belarus
        { prefix: "UA", latDeg: 66.4, lonDeg: 94.2 },         // Russia

        // ---- Middle East ----
        { prefix: "TA", latDeg: 39.0, lonDeg: 35.0 },         // Turkey
        { prefix: "4X", latDeg: 31.5, lonDeg: 34.8 },         // Israel
        { prefix: "YI", latDeg: 33.0, lonDeg: 44.0 },         // Iraq
        { prefix: "EP", latDeg: 32.0, lonDeg: 53.0 },         // Iran
        { prefix: "HZ", latDeg: 24.0, lonDeg: 45.0 },         // Saudi Arabia
        { prefix: "A6", latDeg: 24.0, lonDeg: 54.3 },         // United Arab Emirates

        // ---- Africa ----
        { prefix: "CN", latDeg: 32.0, lonDeg: -5.0 },         // Morocco
        { prefix: "7X", latDeg: 28.0, lonDeg: 3.0 },          // Algeria
        { prefix: "SU", latDeg: 26.8, lonDeg: 30.8 },         // Egypt
        { prefix: "5N", latDeg: 9.0, lonDeg: 8.0 },           // Nigeria
        { prefix: "ET", latDeg: 9.1, lonDeg: 40.5 },          // Ethiopia
        { prefix: "5Z", latDeg: 1.0, lonDeg: 38.0 },          // Kenya
        { prefix: "ZS", latDeg: -29.0, lonDeg: 24.0 },        // South Africa

        // ---- Asia ----
        { prefix: "UN", latDeg: 48.0, lonDeg: 68.0 },         // Kazakhstan
        { prefix: "AP", latDeg: 30.0, lonDeg: 70.0 },         // Pakistan
        { prefix: "VU", latDeg: 22.3511, lonDeg: 78.6677 },   // India
        { prefix: "S2", latDeg: 24.0, lonDeg: 90.0 },         // Bangladesh
        { prefix: "4S", latDeg: 7.0, lonDeg: 81.0 },          // Sri Lanka
        { prefix: "HS", latDeg: 15.0, lonDeg: 101.0 },        // Thailand
        { prefix: "XV", latDeg: 16.0, lonDeg: 108.0 },        // Vietnam
        { prefix: "9M2", latDeg: 4.0, lonDeg: 102.0 },        // Malaysia (West)
        { prefix: "9V", latDeg: 1.35, lonDeg: 103.8 },        // Singapore
        { prefix: "YB", latDeg: -2.5, lonDeg: 118.0 },        // Indonesia
        { prefix: "DU", latDeg: 13.0, lonDeg: 122.0 },        // Philippines
        { prefix: "BY", latDeg: 35.0, lonDeg: 103.0 },        // China
        { prefix: "BV", latDeg: 23.7, lonDeg: 121.0 },        // Taiwan
        { prefix: "VR2", latDeg: 22.3, lonDeg: 114.2 },       // Hong Kong
        { prefix: "HL", latDeg: 36.5, lonDeg: 127.8 },        // South Korea
        { prefix: "JA", latDeg: 36.5, lonDeg: 138.0 },        // Japan

        // ---- Oceania ----
        { prefix: "VK", latDeg: -25.0, lonDeg: 134.0 },       // Australia
        { prefix: "ZL", latDeg: -41.5, lonDeg: 172.8 }        // New Zealand
    ]

    WreniumGeoBridge {
        id: wreniumGeoBridge
    }

    // Hardware-interface stub -- see the note above headingDeg/
    // targetHeadingDeg. A real embedded build would swap this for an
    // actual serial/CAT/GPIO driver.
    RotatorDriver {
        id: rotatorDriver
        objectName: "rotatorDriver"
    }

    function recomputePath() {
        const viewportRadiusPx = Math.min(mapArea.width, mapArea.height) / 2
        currentSvgPath = wreniumGeoBridge.computeCoastlineSvgPath(
            centerLatDeg, centerLonDeg, clipRadiusKm, viewportRadiusPx, useBinaryEmitter)
        // Border lines are a fully separate, optional pipeline call --
        // skipped entirely, including its own recompute cost, whenever
        // showBorders is off.
        currentBorderSvgPath = showBorders
            ? wreniumGeoBridge.computeBorderSvgPath(centerLatDeg, centerLonDeg, clipRadiusKm, viewportRadiusPx, useBinaryEmitter)
            : ""
        // Amateur-prefix markers are *not* updated here -- each one's own
        // Repeater delegate binds projectPoint() directly (see below), so
        // they stay live without an imperative recompute step.
    }

    property double _lastRecomputeMs: 0

    Timer {
        id: recomputeThrottleTimer
        interval: 16
        repeat: false
        onTriggered: root.recomputePathThrottled()
    }

    // Rate-limits recomputePath() to at most ~60 times/second during
    // continuous high-frequency interaction (map drag, slider drag, live
    // window resize) -- those can fire much faster than that, and the
    // full pipeline recompute is expensive enough to stutter if called
    // unthrottled on every event. Always schedules one trailing call so
    // the map never looks "stuck" once the interaction pauses.
    function recomputePathThrottled() {
        const now = Date.now()
        if (now - _lastRecomputeMs >= 16) {
            _lastRecomputeMs = now
            recomputePath()
        } else if (!recomputeThrottleTimer.running) {
            recomputeThrottleTimer.start()
        }
    }

    // Drag-to-rotate: inverts wrenium-geo's own projection math (locally
    // near-flat around the current center) so it stays correct at any
    // zoom level. Applied per mouse-move step, not once from the drag's
    // start, so cos(centerLat) and scale stay accurate over a long drag.
    function dragRotate(dxPx, dyPx) {
        const viewportRadiusPx = Math.min(mapArea.width, mapArea.height) / 2
        if (viewportRadiusPx <= 0 || clipRadiusKm <= 0) {
            return
        }
        const scale = viewportRadiusPx / clipRadiusKm // output units per km, matches WreniumGeoBridge
        const earthRadiusKm = wreniumGeoBridge.earthRadiusKm()
        const pxPerRad = scale * earthRadiusKm

        const latRad = centerLatDeg * Math.PI / 180
        // Clamp near the poles -- cos(lat) -> 0 there would make the
        // longitude conversion blow up.
        const cosLat = Math.max(0.05, Math.cos(latRad))

        let newLatDeg = centerLatDeg + (dyPx / pxPerRad) * 180 / Math.PI
        let newLonDeg = centerLonDeg - (dxPx / (pxPerRad * cosLat)) * 180 / Math.PI

        newLatDeg = Math.max(-89, Math.min(89, newLatDeg))
        newLonDeg = ((newLonDeg + 180) % 360 + 360) % 360 - 180 // wrap to [-180, 180)

        centerLatDeg = newLatDeg
        centerLonDeg = newLonDeg
        recomputePathThrottled()
    }

    function zoomBy(factor) {
        clipRadiusKm = Math.max(100, Math.min(20000, clipRadiusKm * factor))
        recomputePathThrottled()
    }

    // RotatorDriver.heading/target are deliberately unwrapped (see
    // RotatorDriver.h) so the needle-rotation animations never jump across
    // the 0/360 seam -- anything that displays the value as a 0-360
    // compass bearing (the digital readout, the heading slider) needs to
    // wrap it back down first.
    function wrapDeg(value) {
        return ((value % 360) + 360) % 360
    }

    // Aim mode: points the heading wedge at the mouse position (bearing
    // from the map's own center) instead of panning the map. No
    // recomputePath() call -- only the wedge's rotation changes, not the
    // projected geometry. Goes through RotatorDriver rather than setting
    // headingDeg directly, so the wedge only moves as fast as the
    // simulated motor turns.
    function aimAt(mouseX, mouseY) {
        const dx = mouseX - mapArea.width / 2
        const dy = mouseY - mapArea.height / 2
        if (dx === 0 && dy === 0) {
            return
        }
        // atan2(dx, -dy): bearing measured clockwise from north (straight
        // up, -y), matching headingDeg's own convention (0 = up).
        let bearingDeg = Math.atan2(dx, -dy) * 180 / Math.PI
        bearingDeg = (bearingDeg + 360) % 360
        rotatorDriver.setTargetHeading(bearingDeg)
    }

    Component.onCompleted: recomputePath()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        // ----------------------------------------------------------------
        // Map area: a static coastline Shape (always north-up, never
        // rotated) with a separately-rotating heading needle layered on
        // top -- only the heading needle/indicator rotates, the coastline
        // map itself does not.
        // ----------------------------------------------------------------
        Item {
            id: mapArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 400
            Layout.minimumHeight: 400

            // viewportRadiusPx (passed to computeCoastlineSvgPath) depends on this
            // item's size, so a resize needs the same recompute a slider
            // move gets -- throttled the same way, since a live/interactive
            // window resize fires just as fast as a mouse drag.
            onWidthChanged: root.recomputePathThrottled()
            onHeightChanged: root.recomputePathThrottled()

            // Sea background: a disc matching the map's clip-circle radius
            // exactly, not a rectangle -- a rectangle would show through
            // as "leaking" blue in the corners outside the circular map
            // area.
            //
            // Bezel: two stacked circles behind the sea disc -- an outer
            // dark graphite ring and a thin amber accent ring -- framing
            // it like a physical instrument's dial bezel.
            Rectangle {
                readonly property real bezelRadius: Math.min(mapArea.width, mapArea.height) / 2 + 10
                anchors.centerIn: parent
                width: bezelRadius * 2
                height: width
                radius: width / 2
                color: "#232a35"
            }
            Rectangle {
                readonly property real bezelRadius: Math.min(mapArea.width, mapArea.height) / 2 + 3
                anchors.centerIn: parent
                width: bezelRadius * 2
                height: width
                radius: width / 2
                color: "#12151c"
                border.color: "#e8a33d"
                border.width: 1
            }
            Rectangle {
                readonly property real discRadius: Math.min(mapArea.width, mapArea.height) / 2
                anchors.centerIn: parent
                width: discRadius * 2
                height: width
                radius: width / 2
                color: "#132a3d" // sea -- deep, muted steel-blue, matching
                                 // the instrument panel's dark/desaturated
                                 // palette
                border.color: "#0a1420"
            }

            // Distance rings: three concentric circles at 1/4, 1/2, and 3/4
            // of the current range, purely geometric -- azimuthal
            // equidistant projection preserves true distance from center
            // exactly, so a circle of real angular radius d is always a
            // perfect circle of pixel radius d*scale, regardless of zoom.
            // Plain circle-shaped Rectangles (radius: width/2).
            Rectangle {
                readonly property real ringRadius: 0.25 * (Math.min(mapArea.width, mapArea.height) / 2)
                anchors.centerIn: parent
                width: ringRadius * 2
                height: width
                radius: width / 2
                color: "transparent"
                border.color: Qt.rgba(1, 1, 1, 0.22)
                border.width: 1
            }
            Rectangle {
                readonly property real ringRadius: 0.5 * (Math.min(mapArea.width, mapArea.height) / 2)
                anchors.centerIn: parent
                width: ringRadius * 2
                height: width
                radius: width / 2
                color: "transparent"
                border.color: Qt.rgba(1, 1, 1, 0.22)
                border.width: 1
            }
            Rectangle {
                readonly property real ringRadius: 0.75 * (Math.min(mapArea.width, mapArea.height) / 2)
                anchors.centerIn: parent
                width: ringRadius * 2
                height: width
                radius: width / 2
                color: "transparent"
                border.color: Qt.rgba(1, 1, 1, 0.22)
                border.width: 1
            }

            Shape {
                id: coastlineShape
                anchors.fill: parent
                // Intentionally never bound to headingDeg -- north-up
                // only. Heading-up mode, if ever added, would bind this
                // item's own rotation to -headingDeg instead.

                // wrenium::geo::project() centers its output at (0,0), the
                // antenna/QTH location, but PathSvg draws in the Shape's
                // own coordinate space where (0,0) is the top-left corner.
                // Shifting by half the Shape's width/height re-centers the
                // map in the visible area.
                transform: Translate {
                    x: coastlineShape.width / 2
                    y: coastlineShape.height / 2
                }

                ShapePath {
                    id: coastlinePath
                    fillColor: "#4a5c3c" // land -- muted olive/moss, not a
                                         // bright "cartoon map" green, to
                                         // match the sea color's same
                                         // desaturated, instrument-panel
                                         // treatment
                    strokeColor: "#c9d4bd" // dimmed warm ivory, not stark
                                            // white -- still a legible
                                            // coastline edge without
                                            // glaring against the dark
                                            // theme
                    strokeWidth: 1
                    // Even-odd fill rule -- chosen so the rotate/clip
                    // pipeline doesn't need to guarantee consistent
                    // outer-vs-hole ring winding order.
                    fillRule: ShapePath.OddEvenFill

                    PathSvg {
                        // The exact point where the SVG path-data string
                        // (whichever emitter produced it, WreniumGeoBridge
                        // decides based on useBinaryEmitter) reaches Qt
                        // Quick.
                        path: root.currentSvgPath
                    }
                }

                // Country border lines: a second ShapePath sharing
                // coastlineShape's re-centering transform, layered on top
                // of the land fill. fillColor is "transparent" and no
                // fillRule is set -- border data is open polylines with no
                // inside/outside concept, so only the stroke matters.
                ShapePath {
                    id: borderPath
                    fillColor: "transparent"
                    strokeColor: "#a8935f" // warm bronze-tan -- same family
                                           // as the amber accent elsewhere,
                                           // dimmed to sit quietly under the
                                           // coastline/wedge/needles
                    strokeWidth: 1

                    PathSvg {
                        path: root.currentBorderSvgPath
                    }
                }
            }

            // Distance ring labels: plain Items (not part of any Shape), so
            // their text is never distorted/dashed by ShapePath's own
            // stroke rendering. Placed along the northeast diagonal (45
            // degrees) so they never collide with the north-pointing
            // heading wedge's default position and stay clear of each
            // other regardless of zoom level.
            Label {
                readonly property real ringRadius: 0.25 * (Math.min(mapArea.width, mapArea.height) / 2)
                text: qsTr("%1 km").arg(Math.round(0.25 * root.clipRadiusKm))
                color: "#eef2f5"
                font.pixelSize: 11
                x: mapArea.width / 2 + ringRadius * Math.sin(Math.PI / 4) - width / 2
                y: mapArea.height / 2 - ringRadius * Math.cos(Math.PI / 4) - height / 2
            }
            Label {
                readonly property real ringRadius: 0.5 * (Math.min(mapArea.width, mapArea.height) / 2)
                text: qsTr("%1 km").arg(Math.round(0.5 * root.clipRadiusKm))
                color: "#eef2f5"
                font.pixelSize: 11
                x: mapArea.width / 2 + ringRadius * Math.sin(Math.PI / 4) - width / 2
                y: mapArea.height / 2 - ringRadius * Math.cos(Math.PI / 4) - height / 2
            }
            Label {
                readonly property real ringRadius: 0.75 * (Math.min(mapArea.width, mapArea.height) / 2)
                text: qsTr("%1 km").arg(Math.round(0.75 * root.clipRadiusKm))
                color: "#eef2f5"
                font.pixelSize: 11
                x: mapArea.width / 2 + ringRadius * Math.sin(Math.PI / 4) - width / 2
                y: mapArea.height / 2 - ringRadius * Math.cos(Math.PI / 4) - height / 2
            }

            // Target heading needle: a thin line at targetHeadingDeg (the
            // last requested heading), distinct from the solid wedge below
            // (headingDeg, the antenna's actual, lagging position) --
            // shows where the rotator is commanded to point while its
            // simulated motor catches up. Drawn first so the wedge sits on
            // top once they coincide.
            Item {
                id: targetNeedle
                anchors.fill: parent
                transform: [
                    Rotation {
                        origin.x: 0
                        origin.y: 0
                        angle: root.targetHeadingDeg
                        // Softens the otherwise-instant retargeting jump
                        // (a fresh drag/slider request can move the target
                        // needle by a large angle in one step, unlike the
                        // heading needle below which only ever moves in
                        // small simulated-motor increments already).
                        Behavior on angle {
                            NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
                        }
                    },
                    Translate { x: targetNeedle.width / 2; y: targetNeedle.height / 2 }
                ]

                Rectangle {
                    readonly property real length: Math.min(mapArea.width, mapArea.height) / 2
                    x: -1
                    y: -length
                    width: 2
                    height: length
                    color: "#e8a33d"
                    opacity: 0.75
                }
            }

            // Heading indicator: a directional beam wedge from the
            // antenna's location outward, narrow at the center and
            // widening toward the tip, like a real rotator display's
            // beam-pattern indicator. Rotates independently of the
            // always-north-up map below.
            //
            // Path coordinates are local, with (0,0) as the wedge's own
            // tip/pivot extending toward north (negative y). Rotation
            // happens around that local pivot first, then the rotated
            // wedge is translated to the map's actual center -- the same
            // half-width/half-height shift coastlineShape uses above.
            Shape {
                id: headingWedge
                anchors.fill: parent
                transform: [
                    Rotation {
                        origin.x: 0
                        origin.y: 0
                        angle: root.headingDeg
                        // Smooths RotatorDriver's discrete simulated-motor
                        // steps into continuous motion, rather than the
                        // needle visibly snapping on each tick. Duration
                        // matches the tick interval so each step finishes
                        // as the next arrives; Linear easing reads as
                        // constant-speed rotation, not a settling animation.
                        Behavior on angle {
                            NumberAnimation { duration: 50; easing.type: Easing.Linear }
                        }
                    },
                    Translate { x: headingWedge.width / 2; y: headingWedge.height / 2 }
                ]

                ShapePath {
                    id: wedgePath
                    // Muted burnt-orange/terracotta, in the same family as
                    // the amber accent used elsewhere (target needle,
                    // degree ticks, bezel ring).
                    fillColor: Qt.rgba(0.79, 0.42, 0.20, 0.35)
                    strokeColor: "#c9702f"
                    strokeWidth: 1.5
                    fillRule: ShapePath.OddEvenFill

                    // Radius matches the map's clip-circle edge, with a
                    // genuine circular arc (PathArc) rather than a straight
                    // chord. Angular width is root.beamwidthDeg (adjustable
                    // via the Beamwidth slider), so the wedge scales
                    // correctly at any zoom level.
                    readonly property real radius: Math.min(mapArea.width, mapArea.height) / 2
                    readonly property real halfAngleRad: root.beamwidthDeg / 2 * Math.PI / 180
                    readonly property real leftX: -radius * Math.sin(halfAngleRad)
                    readonly property real leftY: -radius * Math.cos(halfAngleRad)
                    readonly property real rightX: radius * Math.sin(halfAngleRad)
                    readonly property real rightY: -radius * Math.cos(halfAngleRad)

                    startX: 0
                    startY: 0
                    PathLine { x: wedgePath.leftX; y: wedgePath.leftY }
                    PathArc {
                        x: wedgePath.rightX
                        y: wedgePath.rightY
                        radiusX: wedgePath.radius
                        radiusY: wedgePath.radius
                        useLargeArc: false
                        direction: PathArc.Clockwise
                    }
                    PathLine { x: 0; y: 0 }
                }
            }

            // Degree ring: bearing tick marks and numeric labels around the
            // map's own edge, complementing the rotating heading wedge
            // above (never rotates itself: bearing 0/up is always true
            // north). Minor ticks every 10 degrees, major (longer) ticks
            // with a numeric label every 30 degrees. Built from plain
            // Rectangle ticks + Rotation/Translate transforms, the same
            // technique headingWedge uses for its own rotation.
            Repeater {
                model: 36
                delegate: Item {
                    id: tickItem
                    readonly property real angleDeg: index * 10
                    readonly property bool isMajor: angleDeg % 30 === 0
                    readonly property real ringRadius: Math.min(mapArea.width, mapArea.height) / 2
                    anchors.fill: parent
                    transform: [
                        Rotation { origin.x: 0; origin.y: 0; angle: tickItem.angleDeg },
                        Translate { x: mapArea.width / 2; y: mapArea.height / 2 }
                    ]

                    Rectangle {
                        x: -width / 2
                        y: -tickItem.ringRadius - height
                        width: 2
                        height: tickItem.isMajor ? 10 : 5
                        color: "#e8a33d"
                    }
                }
            }
            Repeater {
                model: 12
                delegate: Label {
                    readonly property real angleDeg: index * 30
                    readonly property real ringRadius: Math.min(mapArea.width, mapArea.height) / 2
                    text: qsTr("%1°").arg(angleDeg)
                    // Light, legible against the dark instrument-panel
                    // background these labels sit on (just outside the
                    // disc's edge).
                    color: "#c7d0da"
                    font.pixelSize: 12
                    font.bold: true
                    x: mapArea.width / 2 + (ringRadius + 16) * Math.sin(angleDeg * Math.PI / 180) - width / 2
                    y: mapArea.height / 2 - (ringRadius + 16) * Math.cos(angleDeg * Math.PI / 180) - height / 2
                }
            }

            // Amateur radio DXCC/ITU-prefix markers: demonstrates
            // WreniumGeoBridge.projectPoint(), placing arbitrary points
            // (not part of the coastline/border data) at their exact
            // (x, y) position using the same half-width/half-height shift
            // as the other map layers. Each delegate binds projectPoint()
            // directly, so markers stay live as the map moves/zooms with
            // no imperative recompute step, and are hidden whenever their
            // point falls outside the current clip circle.
            Repeater {
                model: root.amateurPrefixes

                // Text only, no dot -- a dot would misleadingly imply an
                // exact location, when this is a country-level reference
                // point placed near the country's center.
                //
                // Plain Text, not Label: cheaper to re-evaluate on every
                // map-drag move event across ~80 markers, since Label's
                // style/background/padding machinery goes unused here.
                delegate: Text {
                    id: prefixMarker
                    readonly property var projection: wreniumGeoBridge.projectPoint(
                        modelData.latDeg, modelData.lonDeg,
                        root.centerLatDeg, root.centerLonDeg, root.clipRadiusKm,
                        Math.min(mapArea.width, mapArea.height) / 2)
                    visible: projection[2]
                    x: mapArea.width / 2 + projection[0] - width / 2
                    y: mapArea.height / 2 + projection[1] - height / 2
                    text: modelData.prefix
                    color: "#e5eaf0"
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            Label {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 6
                color: "#e8a33d"
                font.pixelSize: 13
                font.bold: true
                font.letterSpacing: 0.5
                text: qsTr("WRENIUM GEO — ROTATOR CONTROLLER")
            }

            // Drag-to-rotate (or drag-to-aim, see root.dragAimsAntenna):
            // last child so it sits on top for input (it's invisible, so
            // paint order doesn't matter, only hit-test order). See
            // root.dragRotate()/root.aimAt() for the actual math.
            MouseArea {
                id: dragArea
                anchors.fill: parent
                property real lastX: 0
                property real lastY: 0

                onPressed: (mouse) => {
                    lastX = mouse.x
                    lastY = mouse.y
                    if (root.dragAimsAntenna) {
                        root.aimAt(mouse.x, mouse.y)
                    }
                }
                onPositionChanged: (mouse) => {
                    if (pressed) {
                        if (root.dragAimsAntenna) {
                            root.aimAt(mouse.x, mouse.y)
                        } else {
                            root.dragRotate(mouse.x - lastX, mouse.y - lastY)
                        }
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                }

                onWheel: (wheel) => {
                    const steps = wheel.angleDelta.y / 120
                    root.zoomBy(Math.pow(0.9, steps))
                }
            }

            // Manual rotator control buttons: momentary CCW/STOP/CW
            // pushbuttons (RotatorDriver::jogClockwise/jogCounterClockwise/
            // stop), distinct from the other heading controls which command
            // "go to this azimuth" rather than "keep turning while held".
            // Declared after dragArea so they're stacked on top and claim
            // their own clicks first.
            Row {
                id: rotatorButtons
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 14
                spacing: 8

                Button {
                    text: qsTr("CCW")
                    implicitWidth: 60
                    // Rotation only happens while the button is actually
                    // held down, like the physical momentary switch this
                    // simulates -- onCanceled covers a press that starts
                    // here but is dragged/interrupted away (e.g. losing
                    // grab to a window manager gesture) without a normal
                    // release, so the motor never gets stuck turning.
                    onPressed: rotatorDriver.jogCounterClockwise()
                    onReleased: rotatorDriver.stop()
                    onCanceled: rotatorDriver.stop()
                }

                Button {
                    text: qsTr("STOP")
                    implicitWidth: 60
                    onClicked: rotatorDriver.stop()
                    // Distinct terracotta styling (matches the heading
                    // wedge's own accent color) -- the one button here that
                    // should read as visually different, since it's the
                    // safety/override control rather than a directional one.
                    background: Rectangle {
                        radius: 4
                        color: parent.pressed ? "#a85a2a" : "#c9702f"
                        border.color: "#8f4a1f"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        font.bold: true
                        color: "#14100c"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    text: qsTr("CW")
                    implicitWidth: 60
                    onPressed: rotatorDriver.jogClockwise()
                    onReleased: rotatorDriver.stop()
                    onCanceled: rotatorDriver.stop()
                }
            }
        }

        // ----------------------------------------------------------------
        // Control panel: a dark instrument-panel card with labeled
        // sections and divider rules -- purely visual polish, no behavior
        // changes from the original flat control list.
        // ----------------------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            radius: 10
            color: "#171c24"
            border.color: "#2a3340"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                component SectionHeader: Label {
                    Layout.topMargin: 4
                    font.pixelSize: 12
                    font.bold: true
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                    color: "#e8a33d"
                }

                component Divider: Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    Layout.bottomMargin: 2
                    height: 1
                    color: "#2a3340"
                }

                component FieldLabel: Label {
                    color: "#c7d0da"
                    font.pixelSize: 13
                }

                // Digital azimuth readout: AZ is the antenna's actual
                // heading (lags behind while the simulated motor is still
                // moving); the small TARGET line below is the last
                // requested heading, matching the needle/target-needle
                // split on the map itself.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 76
                    Layout.bottomMargin: 6
                    radius: 6
                    color: "#0b0d11"
                    border.color: "#3a4452"
                    border.width: 1

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 2

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("AZ %1°").arg(root.wrapDeg(root.headingDeg).toFixed(1))
                            font.family: "monospace"
                            font.pixelSize: 26
                            font.bold: true
                            font.letterSpacing: 2
                            color: "#e8a33d"
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("TARGET %1°").arg(root.wrapDeg(root.targetHeadingDeg).toFixed(1))
                            font.family: "monospace"
                            font.pixelSize: 12
                            font.letterSpacing: 1
                            color: "#5b6b78"
                        }
                    }
                }

                SectionHeader {
                    text: qsTr("Position")
                }

                FieldLabel {
                    text: qsTr("Center latitude: %1 deg").arg(latSlider.value.toFixed(2))
                }
                Slider {
                    id: latSlider
                    Layout.fillWidth: true
                    from: -90
                    to: 90
                    value: root.centerLatDeg
                    onMoved: {
                        root.centerLatDeg = value
                        root.recomputePathThrottled()
                    }
                    // QML severs the "value: root.centerLatDeg" binding the
                    // moment the slider's drag machinery assigns to
                    // `value` -- without this Connections, dragging the map
                    // would stop updating the slider after it was touched
                    // once.
                    Connections {
                        target: root
                        function onCenterLatDegChanged() {
                            latSlider.value = root.centerLatDeg
                        }
                    }
                }

                FieldLabel {
                    text: qsTr("Center longitude: %1 deg").arg(lonSlider.value.toFixed(2))
                }
                Slider {
                    id: lonSlider
                    Layout.fillWidth: true
                    from: -180
                    to: 180
                    value: root.centerLonDeg
                    onMoved: {
                        root.centerLonDeg = value
                        root.recomputePathThrottled()
                    }
                    // Same binding-severed-by-assignment issue as latSlider above.
                    Connections {
                        target: root
                        function onCenterLonDegChanged() {
                            lonSlider.value = root.centerLonDeg
                        }
                    }
                }

                Divider {}

                SectionHeader {
                    text: qsTr("Orientation & zoom")
                }

                FieldLabel {
                    text: qsTr("Heading: %1 deg").arg(headingSlider.value.toFixed(1))
                }
                Slider {
                    id: headingSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 360
                    value: root.wrapDeg(root.headingDeg)
                    onMoved: {
                        // Requests the new heading from RotatorDriver
                        // rather than setting headingDeg directly -- see
                        // the note above headingDeg/targetHeadingDeg. No
                        // recomputePath() call: heading is a cheap
                        // post-projection rotation, not a pipeline re-run.
                        rotatorDriver.setTargetHeading(value)
                    }
                    // The slider's own handle tracks the *actual* (lagging)
                    // heading, not the target -- so once released, it
                    // visibly glides to match the simulated motor as it
                    // catches up, the same way latSlider/lonSlider stay in
                    // sync with map-drag panning. Guarded by !pressed --
                    // unlike centerLatDeg/centerLonDeg (which update
                    // synchronously), headingDeg lags behind the target
                    // while the simulated motor catches up, so syncing
                    // unconditionally would fight the user's own drag with
                    // the stale, still-catching-up value on every tick.
                    Connections {
                        target: root
                        function onHeadingDegChanged() {
                            if (!headingSlider.pressed) {
                                headingSlider.value = root.wrapDeg(root.headingDeg)
                            }
                        }
                    }
                }

                FieldLabel {
                    // "Range" -- matches the real-world term for this on a
                    // radar/rotator display (the distance rings just below
                    // it on the map are already labeled purely in km, no
                    // "zoom"), and reads more naturally than "zoom radius"
                    // for a value that's a physical distance, not an
                    // optical zoom level.
                    text: qsTr("Range: %1 km").arg(clipRadiusSlider.value.toFixed(0))
                }
                Slider {
                    id: clipRadiusSlider
                    Layout.fillWidth: true
                    from: 100
                    to: 20000
                    value: root.clipRadiusKm
                    onMoved: {
                        root.clipRadiusKm = value
                        root.recomputePathThrottled()
                    }
                    // Keeps the slider in sync with scroll-wheel zoom (see
                    // dragArea's onWheel/root.zoomBy()), same binding-severed-
                    // by-assignment issue as latSlider/lonSlider above.
                    Connections {
                        target: root
                        function onClipRadiusKmChanged() {
                            clipRadiusSlider.value = root.clipRadiusKm
                        }
                    }
                }

                FieldLabel {
                    text: qsTr("Beamwidth: %1 deg").arg(beamwidthSlider.value.toFixed(0))
                }
                Slider {
                    id: beamwidthSlider
                    Layout.fillWidth: true
                    from: 20
                    to: 90
                    value: root.beamwidthDeg
                    onMoved: {
                        // Purely a rendering property of the heading wedge
                        // (wedgePath's halfAngleRad) -- no recomputePath()
                        // call, same reasoning as the heading slider above.
                        root.beamwidthDeg = value
                    }
                }

                // ------------------------------------------------------------
                // Drag-mode selector: dragging the map either pans it
                // (original behavior) or just aims the heading wedge at the
                // drag position -- see root.dragAimsAntenna/root.aimAt().
                // ------------------------------------------------------------
                FieldLabel {
                    text: qsTr("Map drag")
                }
                RowLayout {
                    spacing: 8

                    FieldLabel {
                        text: qsTr("Pan map")
                    }
                    Switch {
                        id: dragModeSwitch
                        checked: root.dragAimsAntenna
                        onToggled: root.dragAimsAntenna = checked
                    }
                    FieldLabel {
                        text: qsTr("Aim antenna")
                    }
                }

                Divider {}

                SectionHeader {
                    text: qsTr("Display")
                }

                // Border lines toggle -- optional, enabled by default.
                // Skips the whole separate border pipeline call in
                // recomputePath() when off, not just the rendering.
                RowLayout {
                    spacing: 8

                    FieldLabel {
                        text: qsTr("Country borders")
                    }
                    Switch {
                        id: bordersSwitch
                        checked: root.showBorders
                        onToggled: {
                            root.showBorders = checked
                            root.recomputePath()
                        }
                    }
                }

                // Emitter toggle: exercise both emitters via a toggle so
                // any visual mismatch between the two states is
                // unambiguously an encode/decode bug, not a
                // rendering-backend difference -- both states must still
                // flow through the same Shape/ShapePath/PathSvg above.
                FieldLabel {
                    text: qsTr("Emitter path")
                }
                RowLayout {
                    spacing: 8

                    FieldLabel {
                        text: qsTr("Direct SVG")
                    }
                    Switch {
                        id: emitterSwitch
                        checked: root.useBinaryEmitter
                        onToggled: {
                            // WreniumGeoBridge.computeCoastlineSvgPath's last argument
                            // selects the binary-emit-then-decode path vs.
                            // the direct SVG emitter.
                            root.useBinaryEmitter = checked
                            root.recomputePath()
                        }
                    }
                    FieldLabel {
                        text: qsTr("Binary -> decoded SVG")
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }
}
