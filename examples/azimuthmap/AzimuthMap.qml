// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

import QtQuick
import QtQuick.Controls
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
    // useOrthographic parameter): equidistant (false, distance-preserving
    // range rings) or orthographic (true, "viewed from space" -- the disk
    // edge is the horizon).
    property bool orthographic: false
    // Orthographic only makes geometric sense up to the horizon
    // (centralAngle == 90 deg); beyond that it folds back on itself.
    // rangeKm can be set well past that (up to the antipode, 20020 km,
    // which is meaningful for equidistant) so clamp only what's actually
    // sent to the projection, not the slider/field itself.
    readonly property real horizonKm: wreniumGeoBridge.earthRadiusKm() * Math.PI / 2
    readonly property real effectiveRangeKm: orthographic ? Math.min(rangeKm, horizonKm) : rangeKm

    WreniumGeoBridge {
        id: wreniumGeoBridge
    }

    function recomputePath() {
        const viewportRadiusPx = Math.min(mapArea.width, mapArea.height) / 2
        currentCoastlineSvgPath = wreniumGeoBridge.computeCoastlineSvgPath(
            centerLatDeg, centerLonDeg, effectiveRangeKm, viewportRadiusPx, false, orthographic)
        currentBorderSvgPath = wreniumGeoBridge.computeBorderSvgPath(
            centerLatDeg, centerLonDeg, effectiveRangeKm, viewportRadiusPx, false, orthographic)
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
                validator: DoubleValidator { bottom: -90; top: 90 }
                onAccepted: goButton.clicked()
            }
            Label { text: qsTr("Lon"); color: "#c7d0da" }
            TextField {
                id: lonField
                Layout.preferredWidth: 90
                validator: DoubleValidator { bottom: -180; top: 180 }
                onAccepted: goButton.clicked()
            }
            Label { text: qsTr("Range (km)"); color: "#c7d0da" }
            TextField {
                id: rangeField
                Layout.preferredWidth: 90
                validator: DoubleValidator { bottom: 1; top: 20020 }
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

            Button {
                id: projectionButton
                checkable: true
                checked: root.orthographic
                text: checked ? qsTr("Orthographic") : qsTr("Equidistant")
                onToggled: {
                    root.orthographic = checked
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
            text: root.orthographic && root.rangeKm > root.horizonKm
                ? qsTr("center=(%1, %2) deg  range=%3 km  projection=orthographic (clamped to horizon %4 km)")
                    .arg(root.centerLatDeg.toFixed(4))
                    .arg(root.centerLonDeg.toFixed(4))
                    .arg(root.rangeKm.toFixed(0))
                    .arg(root.horizonKm.toFixed(0))
                : qsTr("center=(%1, %2) deg  range=%3 km  projection=%4")
                    .arg(root.centerLatDeg.toFixed(4))
                    .arg(root.centerLonDeg.toFixed(4))
                    .arg(root.rangeKm.toFixed(0))
                    .arg(root.orthographic ? qsTr("orthographic") : qsTr("equidistant"))
        }

        Item {
            id: mapArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            onWidthChanged: root.recomputePathThrottled()
            onHeightChanged: root.recomputePathThrottled()

            Rectangle {
                readonly property real discRadius: Math.min(mapArea.width, mapArea.height) / 2
                anchors.centerIn: parent
                width: discRadius * 2
                height: width
                radius: width / 2
                color: "#132a3d"
                border.color: "#0a1420"
            }

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
