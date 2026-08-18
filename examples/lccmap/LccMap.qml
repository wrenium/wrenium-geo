// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

import wrenium_geo_qt_bridge

// Minimal Lambert conformal conic integration example: drag to pan,
// scroll to zoom, click to read off the coordinate under the cursor --
// the same interaction set and dark palette as azimuthmap/mercatormap,
// so all three read as one family. Shows WreniumGeoBridge's conic
// methods: computeConicCoastlineSvgPath/computeConicBorderSvgPath for the
// map itself, projectConicPoint for a live cursor readout, and
// unprojectConicPoint to resolve a click back to a real (lat, lon).
//
// The two standard parallels stay launch-time-only (CLI flags, shown
// read-only in the toolbar) -- they have no "pan" semantics of their
// own; a real printed chart's standard parallels are fixed at print
// time the same way. Origin is what dragging moves.
ApplicationWindow {
    id: root

    width: Math.max(760, toolbarRow.implicitWidth + captureArea.anchors.margins * 2)
    height: 760
    visible: true
    title: qsTr("wrenium-geo — Lambert Conformal Conic Map")
    color: "#12151c"

    property real initialStandardParallel1Deg: 35.0
    property real initialStandardParallel2Deg: 65.0
    readonly property real standardParallel1Deg: initialStandardParallel1Deg
    readonly property real standardParallel2Deg: initialStandardParallel2Deg
    property real initialOriginLatDeg: 52.0
    property real initialOriginLonDeg: 10.0
    property real initialHalfWidthKm: 3000.0
    property string screenshotPath: ""

    property real originLatDeg: initialOriginLatDeg
    property real originLonDeg: initialOriginLonDeg
    property real halfWidthKm: initialHalfWidthKm
    property string coastlinePath: ""
    property string borderPath: ""
    property string cursorReadout: qsTr("move the cursor over the chart")

    WreniumGeoBridge {
        id: bridge
    }

    function recomputePath() {
        const w = mapArea.width
        const h = mapArea.height
        if (w <= 0 || h <= 0) {
            return
        }
        coastlinePath = bridge.computeConicCoastlineSvgPath(
            standardParallel1Deg, standardParallel2Deg, originLatDeg, originLonDeg,
            halfWidthKm, w, h, false)
        borderPath = bridge.computeConicBorderSvgPath(
            standardParallel1Deg, standardParallel2Deg, originLatDeg, originLonDeg,
            halfWidthKm, w, h, false)
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

    // Keeps the origin from panning/zooming close enough to this frame's
    // own finite pole (the cone's own apex) to expose the wedge's real
    // angular gap -- WreniumGeoBridge's own clampConicOriginLatDeg
    // (detail/conic/lambert_conformal.h's clampOriginLatForApexSafety has
    // the geometry). A genuine domain limit of Lambert conformal conic
    // itself: this projection was never meant for a near-polar,
    // near-global view, the same way gnomonic's own 75-degree
    // clamp (azimuthmap) keeps that projection inside its own valid
    // domain.
    function applyApexSafety() {
        originLatDeg = bridge.clampConicOriginLatDeg(
            standardParallel1Deg, standardParallel2Deg, originLatDeg, halfWidthKm,
            mapArea.width, mapArea.height)
    }

    function zoomBy(factor) {
        halfWidthKm = Math.max(300, Math.min(10000, halfWidthKm * factor))
        applyApexSafety()
        recomputePathThrottled()
    }

    // Drag-to-pan: a local-linear approximation around the current
    // origin, same technique (and same caveat) as azimuthmap's own
    // dragPan -- LCC has no closed-form "recenter keeping this point
    // fixed" the way Mercator does (WreniumGeoBridge.h's
    // recenterKeepingPointFixed is Mercator-only), so this re-derives the
    // local km-per-pixel and km-per-degree scale on every move step
    // instead. Good enough for interactive panning at any reasonable
    // zoom, with accuracy tapering off over a very long drag far from
    // the origin.
    function dragPan(dxPx, dyPx) {
        if (mapArea.width <= 0 || halfWidthKm <= 0) {
            return
        }
        const pxPerKm = mapArea.width / (2 * halfWidthKm)
        const dxKm = dxPx / pxPerKm
        const dyKm = dyPx / pxPerKm

        const originLatRad = originLatDeg * Math.PI / 180
        const cosOriginLat = Math.max(0.05, Math.cos(originLatRad))
        const kmPerDeg = (Math.PI / 180) * bridge.earthRadiusKm()

        let newOriginLat = originLatDeg + (dyKm / kmPerDeg)
        let newOriginLon = originLonDeg - (dxKm / (kmPerDeg * cosOriginLat))

        newOriginLat = Math.max(-89, Math.min(89, newOriginLat))
        newOriginLon = ((newOriginLon + 180) % 360 + 360) % 360 - 180

        originLatDeg = newOriginLat
        originLonDeg = newOriginLon
        applyApexSafety()
        recomputePathThrottled()
    }

    onOriginLatDegChanged: originLatField.text = originLatDeg.toFixed(4)
    onOriginLonDegChanged: originLonField.text = originLonDeg.toFixed(4)
    onHalfWidthKmChanged: halfWidthField.text = halfWidthKm.toFixed(0)

    function takeScreenshot(path) {
        screenshotPathField.text = path
        screenshotTimer.start()
    }

    Component.onCompleted: {
        applyApexSafety() // in case --origin-lat/--halfwidth were passed in an unsafe combination
        originLatField.text = originLatDeg.toFixed(4)
        originLonField.text = originLonDeg.toFixed(4)
        halfWidthField.text = halfWidthKm.toFixed(0)
        recomputePath()
        if (screenshotPath.length > 0) {
            screenshotPathField.text = screenshotPath
            screenshotTimer.start()
        }
    }

    // Same "let the Shape actually paint before grabbing" delay as
    // azimuthmap/mercatormap's own screenshot mechanism.
    Timer {
        id: screenshotTimer
        interval: 200
        repeat: false
        onTriggered: {
            captureArea.grabToImage(function(result) {
                result.saveToFile(screenshotPathField.text)
                if (root.screenshotPath.length > 0) {
                    Qt.quit()
                }
            })
        }
    }

    ColumnLayout {
        id: captureArea
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            id: toolbarRow
            Layout.fillWidth: true
            spacing: 10

            Label { text: qsTr("Origin lat"); color: "#c7d0da" }
            TextField {
                id: originLatField
                Layout.preferredWidth: 90
                // locale: "C" forces "." as the decimal point regardless
                // of the system locale -- see mercatormap's own comment
                // on this field for why (parseFloat() below always
                // expects ".").
                validator: DoubleValidator { bottom: -89; top: 89; locale: "C" }
                onAccepted: goButton.clicked()
            }
            Label { text: qsTr("Origin lon"); color: "#c7d0da" }
            TextField {
                id: originLonField
                Layout.preferredWidth: 90
                validator: DoubleValidator { bottom: -180; top: 180; locale: "C" }
                onAccepted: goButton.clicked()
            }
            Label { text: qsTr("Half-width (km)"); color: "#c7d0da" }
            TextField {
                id: halfWidthField
                Layout.preferredWidth: 90
                validator: DoubleValidator { bottom: 300; top: 10000; locale: "C" }
                onAccepted: goButton.clicked()
            }
            Button {
                id: goButton
                text: qsTr("Go")
                onClicked: {
                    root.halfWidthKm = Math.max(300, Math.min(10000, parseFloat(halfWidthField.text)))
                    root.originLatDeg = Math.max(-89, Math.min(89, parseFloat(originLatField.text)))
                    root.originLonDeg = parseFloat(originLonField.text)
                    root.applyApexSafety()
                    root.recomputePath()
                }
            }

            Item { Layout.fillWidth: true }

            TextField {
                id: screenshotPathField
                Layout.preferredWidth: 220
                placeholderText: qsTr("screenshot output path")
            }
            Button {
                text: qsTr("Screenshot")
                onClicked: root.takeScreenshot(screenshotPathField.text)
            }
        }

        Label {
            Layout.fillWidth: true
            color: "#8a97a6"
            font.pixelSize: 11
            text: qsTr("std. parallels %1°/%2° (fixed)  origin=(%3, %4) deg  half-width=%5 km  drag to pan, scroll to zoom — %6")
                .arg(root.standardParallel1Deg.toFixed(1))
                .arg(root.standardParallel2Deg.toFixed(1))
                .arg(root.originLatDeg.toFixed(4))
                .arg(root.originLonDeg.toFixed(4))
                .arg(root.halfWidthKm.toFixed(0))
                .arg(root.cursorReadout)
        }

        Item {
            id: mapArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Lambert conformal conic has no geometric clip the way
            // azimuthmap's disc does -- crop here instead (mercatormap's
            // identical fix, same reasoning: a rectangular projection's
            // own data can extend past the visible window).
            clip: true

            onWidthChanged: root.recomputePathThrottled()
            onHeightChanged: root.recomputePathThrottled()

            Rectangle {
                anchors.fill: parent
                color: "#132a3d"
                border.color: "#0a1420"
            }

            Shape {
                id: chartShape
                anchors.fill: parent
                transform: Translate {
                    x: chartShape.width / 2
                    y: chartShape.height / 2
                }

                ShapePath {
                    fillColor: "#4a5c3c"
                    strokeColor: "#c9d4bd"
                    strokeWidth: 1
                    fillRule: ShapePath.OddEvenFill
                    PathSvg { path: root.coastlinePath }
                }
                ShapePath {
                    fillColor: "transparent"
                    strokeColor: "#a8935f"
                    strokeWidth: 1
                    PathSvg { path: root.borderPath }
                }
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                hoverEnabled: true

                property real lastX: 0
                property real lastY: 0

                onPressed: (mouse) => {
                    lastX = mouse.x
                    lastY = mouse.y
                }
                onPositionChanged: (mouse) => {
                    if (pressed) {
                        root.dragPan(mouse.x - lastX, mouse.y - lastY)
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                    const localX = mouse.x - mapArea.width / 2
                    const localY = mouse.y - mapArea.height / 2
                    const geo = bridge.unprojectConicPoint(
                        localX, localY,
                        root.standardParallel1Deg, root.standardParallel2Deg,
                        root.originLatDeg, root.originLonDeg,
                        root.halfWidthKm, mapArea.width)
                    root.cursorReadout = qsTr("%1°, %2°").arg(geo[0].toFixed(3)).arg(geo[1].toFixed(3))
                }
                onWheel: (wheel) => root.zoomBy(Math.pow(0.9, wheel.angleDelta.y / 120))
            }
        }
    }
}
