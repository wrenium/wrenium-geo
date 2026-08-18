// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes

import wrenium_geo_qt_bridge

ApplicationWindow {
    id: root

    // Auto-fits the toolbar's actual content width instead of a hardcoded
    // guess -- so adding/removing a toolbar control (like the projection
    // toggle) can never silently overflow the window again. The 700 floor
    // just keeps the map area itself from being squeezed too small when
    // the toolbar alone would fit narrower.
    width: Math.max(700, toolbarRow.implicitWidth + captureArea.anchors.margins * 2)
    height: 760
    visible: true
    title: qsTr("wrenium-geo Azimuth Map")
    color: "#12151c"

    property real initialLat: 0.0
    property real initialLon: 0.0
    property real initialRange: 8000.0
    property string screenshotPath: ""

    property real centerLatDeg: initialLat
    property real centerLonDeg: initialLon
    property real rangeKm: initialRange
    property string currentCoastlineSvgPath: ""
    property string currentBorderSvgPath: ""
    // Selects the radial-distance formula (WreniumGeoBridge's
    // AzimuthalProjection enum): equidistant (distance-preserving range
    // rings), orthographic ("viewed from space" -- the disk edge is the
    // horizon), or gnomonic (every great circle is a straight line).
    property int projection: WreniumGeoBridge.Equidistant
    // Orthographic only makes geometric sense up to the horizon
    // (centralAngle == 90 deg); beyond that it folds back on itself.
    // Gnomonic's own radius diverges approaching the horizon rather than
    // folding back, so it needs a tighter practical clamp -- 75 degrees
    // leaves the disc's edge still a finite, readable size rather than
    // the extreme foreshortening the last few degrees before 90 produce.
    // rangeKm can be set well past either (up to the antipode, 20020 km,
    // which is meaningful for equidistant) so clamp only what's actually
    // sent to the projection, not the slider/field itself.
    readonly property real horizonKm: wreniumGeoBridge.earthRadiusKm() * Math.PI / 2
    readonly property real gnomonicMaxKm: wreniumGeoBridge.earthRadiusKm() * (75.0 * Math.PI / 180.0)
    readonly property real effectiveRangeKm: {
        if (projection === WreniumGeoBridge.Orthographic)
            return Math.min(rangeKm, horizonKm)
        if (projection === WreniumGeoBridge.Gnomonic)
            return Math.min(rangeKm, gnomonicMaxKm)
        return rangeKm
    }

    WreniumGeoBridge {
        id: wreniumGeoBridge
    }

    function recomputePath() {
        const viewportRadiusPx = Math.min(mapArea.width, mapArea.height) / 2
        currentCoastlineSvgPath = wreniumGeoBridge.computeCoastlineSvgPath(
            centerLatDeg, centerLonDeg, effectiveRangeKm, viewportRadiusPx, false, projection)
        currentBorderSvgPath = wreniumGeoBridge.computeBorderSvgPath(
            centerLatDeg, centerLonDeg, effectiveRangeKm, viewportRadiusPx, false, projection)
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

    // Drag-to-pan: same inverted-projection technique as the rotator demo
    // (wrenium::geo::project() is locally near-flat around the current
    // center), applied per mouse-move step so it stays accurate over a
    // long drag.
    function dragPan(dxPx, dyPx) {
        const viewportRadiusPx = Math.min(mapArea.width, mapArea.height) / 2
        if (viewportRadiusPx <= 0 || rangeKm <= 0) {
            return
        }
        const scale = viewportRadiusPx / rangeKm
        const pxPerRad = scale * wreniumGeoBridge.earthRadiusKm()

        const latRad = centerLatDeg * Math.PI / 180
        const cosLat = Math.max(0.05, Math.cos(latRad))

        let newLatDeg = centerLatDeg + (dyPx / pxPerRad) * 180 / Math.PI
        let newLonDeg = centerLonDeg - (dxPx / (pxPerRad * cosLat)) * 180 / Math.PI

        newLatDeg = Math.max(-89, Math.min(89, newLatDeg))
        newLonDeg = ((newLonDeg + 180) % 360 + 360) % 360 - 180

        centerLatDeg = newLatDeg
        centerLonDeg = newLonDeg
        recomputePathThrottled()
    }

    function zoomBy(factor) {
        rangeKm = Math.max(10, Math.min(20020, rangeKm * factor))
        recomputePathThrottled()
    }

    function projectionName() {
        if (projection === WreniumGeoBridge.Orthographic)
            return qsTr("orthographic")
        if (projection === WreniumGeoBridge.Gnomonic)
            return qsTr("gnomonic")
        return qsTr("equidistant")
    }

    // The clamp limit for the current projection, or 0 if it has none
    // (equidistant) -- shared by statusLine() below.
    function clampLimitKm() {
        if (projection === WreniumGeoBridge.Orthographic)
            return horizonKm
        if (projection === WreniumGeoBridge.Gnomonic)
            return gnomonicMaxKm
        return 0
    }

    function statusLine() {
        const base = qsTr("center=(%1, %2) deg  range=%3 km  projection=%4")
            .arg(centerLatDeg.toFixed(4))
            .arg(centerLonDeg.toFixed(4))
            .arg(rangeKm.toFixed(0))
            .arg(projectionName())

        const limitKm = clampLimitKm()
        if (limitKm > 0 && rangeKm > limitKm) {
            return base + qsTr(" (clamped to %1 km)").arg(limitKm.toFixed(0))
        }
        return base
    }

    onCenterLatDegChanged: latField.text = centerLatDeg.toFixed(4)
    onCenterLonDegChanged: lonField.text = centerLonDeg.toFixed(4)
    onRangeKmChanged: rangeField.text = rangeKm.toFixed(0)

    function takeScreenshot(path) {
        screenshotPathField.text = path
        screenshotTimer.start()
    }

    Component.onCompleted: {
        latField.text = centerLatDeg.toString()
        lonField.text = centerLonDeg.toString()
        rangeField.text = rangeKm.toString()
        recomputePath()
        if (screenshotPath.length > 0) {
            screenshotPathField.text = screenshotPath
            screenshotTimer.start()
        }
    }

    // A short delay after recomputePath() so the Shape has a chance to
    // actually paint the new geometry before grabToImage() captures a
    // frame -- grabbing on the same tick as the data change can otherwise
    // catch the previous frame.
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

            Label { text: qsTr("Lat"); color: "#c7d0da" }
            TextField {
                id: latField
                Layout.preferredWidth: 90
                // locale: "C" forces "." as the decimal point regardless of
                // the system locale (e.g. Finnish, which uses ","): without
                // it, DoubleValidator both rejects "." and accepts "," --
                // but the field is read with plain JS parseFloat()
                // (goButton.onClicked below), which always expects "." and
                // silently truncates at a "," (parseFloat("20,015") is 20,
                // not 20015) -- see MercatorMap.qml's identical fix.
                validator: DoubleValidator { bottom: -90; top: 90; locale: "C" }
                onAccepted: goButton.clicked()
            }
            Label { text: qsTr("Lon"); color: "#c7d0da" }
            TextField {
                id: lonField
                Layout.preferredWidth: 90
                validator: DoubleValidator { bottom: -180; top: 180; locale: "C" }
                onAccepted: goButton.clicked()
            }
            Label { text: qsTr("Range (km)"); color: "#c7d0da" }
            TextField {
                id: rangeField
                Layout.preferredWidth: 90
                validator: DoubleValidator { bottom: 1; top: 20020; locale: "C" }
                onAccepted: goButton.clicked()
            }
            Button {
                id: goButton
                text: qsTr("Go")
                onClicked: {
                    root.centerLatDeg = parseFloat(latField.text)
                    root.centerLonDeg = parseFloat(lonField.text)
                    root.rangeKm = parseFloat(rangeField.text)
                    root.recomputePath()
                }
            }

            ComboBox {
                id: projectionCombo
                Layout.preferredWidth: 130
                model: [qsTr("Equidistant"), qsTr("Orthographic"), qsTr("Gnomonic")]
                currentIndex: root.projection
                onActivated: (index) => {
                    root.projection = index
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
            text: root.statusLine()
        }

        Item {
            id: mapArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            onWidthChanged: root.recomputePathThrottled()
            onHeightChanged: root.recomputePathThrottled()

            readonly property real discRadius: Math.min(width, height) / 2

            // The round "disc" backdrop -- doubles as discMask's own mask
            // shape below, so the coastline is cropped to exactly the
            // same circle this paints, not just an approximation of it.
            Rectangle {
                id: discBackground
                anchors.centerIn: parent
                width: mapArea.discRadius * 2
                height: width
                radius: width / 2
                color: "#132a3d"
                border.color: "#0a1420"
            }

            // Equidistant/orthographic never project outside this disc (a
            // provable property of each formula -- orthographic's
            // sin(centralAngle) <= centralAngle, equidistant's radius is
            // exactly linear in it), but gnomonic's tan(centralAngle)
            // grows faster than linear, so its own projected points
            // routinely land outside it even from well inside the clip
            // circle. A plain `clip: true` only crops to a rectangle, not
            // this disc's own round shape, so gnomonic needs an actual
            // circular mask -- MultiEffect against discBackground's own
            // shape, applied uniformly to all three projections (a no-op
            // for the two that never reach the edge anyway).
            Item {
                id: coastlineSource
                anchors.fill: parent

                Shape {
                    id: coastlineShape
                    anchors.fill: parent
                    transform: Translate {
                        x: coastlineShape.width / 2
                        y: coastlineShape.height / 2
                    }

                    ShapePath {
                        fillColor: "#4a5c3c"
                        strokeColor: "#c9d4bd"
                        strokeWidth: 1
                        fillRule: ShapePath.OddEvenFill
                        PathSvg { path: root.currentCoastlineSvgPath }
                    }
                    ShapePath {
                        fillColor: "transparent"
                        strokeColor: "#a8935f"
                        strokeWidth: 1
                        PathSvg { path: root.currentBorderSvgPath }
                    }
                }
            }

            MultiEffect {
                anchors.fill: coastlineSource
                source: coastlineSource
                maskEnabled: true
                maskSource: discBackground
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
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
                }

                onWheel: (wheel) => {
                    // One notch (120 units of angleDelta.y) is a 10% zoom
                    // step -- scroll up (positive delta) zooms in (smaller
                    // range), scroll down zooms out.
                    const steps = wheel.angleDelta.y / 120
                    root.zoomBy(Math.pow(0.9, steps))
                }
            }
        }
    }
}
