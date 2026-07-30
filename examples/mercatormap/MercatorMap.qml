// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

import wrenium_geo_qt_bridge

ApplicationWindow {
    id: root

    width: Math.max(700, toolbarRow.implicitWidth + captureArea.anchors.margins * 2)
    height: 760
    visible: true
    title: qsTr("wrenium-geo Web Mercator Map")
    color: "#12151c"

    property real initialLat: 0.0
    property real initialLon: 0.0
    property real initialHalfWidthKm: 8000.0
    property string screenshotPath: ""

    property real centerLatDeg: initialLat
    property real centerLonDeg: initialLon
    // Half the currently-visible map width, in km -- the rectangular
    // counterpart to azimuthmap's clip-radius-based rangeKm, since
    // Mercator has no clip radius at all (WreniumGeoBridge.h's own
    // comment on computeMercatorCoastlineSvgPath). Bounds mirror
    // azimuthmap's rangeKm bounds: ~half the equatorial circumference at
    // the wide end.
    property real halfWidthKm: initialHalfWidthKm
    property string currentCoastlineSvgPath: ""
    property string currentBorderSvgPath: ""

    WreniumGeoBridge {
        id: wreniumGeoBridge
    }

    function recomputePath() {
        if (mapArea.width <= 0 || mapArea.height <= 0) {
            return
        }
        currentCoastlineSvgPath = wreniumGeoBridge.computeMercatorCoastlineSvgPath(
            centerLatDeg, centerLonDeg, halfWidthKm, mapArea.width, mapArea.height, false)
        currentBorderSvgPath = wreniumGeoBridge.computeMercatorBorderSvgPath(
            centerLatDeg, centerLonDeg, halfWidthKm, mapArea.width, mapArea.height, false)
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

    // Drag-to-pan and scroll-to-zoom-toward-cursor both keep a specific
    // geo point fixed under a specific screen position -- grab the point
    // under the cursor once (unprojectMercatorPoint, the old state), then
    // ask WreniumGeoBridge what center makes that point land back at the
    // target screen position under the new state
    // (recenterKeepingPointFixed). Exact at every step (no drift over a
    // long drag), unlike azimuthmap's own dragPan, which uses a local
    // linear approximation because inverting the full azimuthal
    // rotate+clip pipeline isn't as simple as Mercator's closed-form
    // inverse -- see WreniumGeoBridge.h's own comments on both methods.
    //
    // Grabbing the anchor "once" is essential, not just an optimization:
    // project()/unproject() are each other's inverse only up to
    // f32math's tanh/asin approximation error (a few thousandths of a
    // radian, worse near high latitude -- see mercator.h). Re-deriving
    // the anchor from the live center on every step (as zoomTowardCursor
    // used to) feeds each step's small error back into the next step's
    // anchor, compounding roughly exponentially over a long scroll --
    // confirmed by measurement: an 18-notch zoom-in over a cursor near
    // 65 degrees latitude drifted the anchored point by ~36px (multiple
    // degrees of true position) with re-derivation, vs ~4px of ordinary
    // bounded per-step error once the anchor is cached like drag's.
    property real dragAnchorLatDeg: 0.0
    property real dragAnchorLonDeg: 0.0

    function beginDrag(offsetX, offsetY) {
        const anchor = wreniumGeoBridge.unprojectMercatorPoint(
            offsetX, offsetY, centerLatDeg, centerLonDeg, halfWidthKm, mapArea.width)
        dragAnchorLatDeg = anchor[0]
        dragAnchorLonDeg = anchor[1]
    }

    // Zoom-aware clamp (WreniumGeoBridge.h's own comment on
    // clampMercatorCenterLatDeg()): keeps the *viewport's* own top/bottom
    // edge within project()'s valid latitude range, not just the center
    // point. A flat +-mercatorMaxLatDeg() clamp on the center alone (the
    // old approach here) is too permissive at a wide zoom -- it lets the
    // center get dragged so close to a pole that half the viewport shows
    // nothing but that pole's own dead zone, with real content (e.g.
    // Antarctica's coastline) pulled to the middle of the screen instead
    // of stopping near the edge, confirmed by direct measurement.
    function clampCenterLat(latDeg) {
        return wreniumGeoBridge.clampMercatorCenterLatDeg(latDeg, halfWidthKm, mapArea.width, mapArea.height)
    }

    function dragTo(offsetX, offsetY) {
        const newCenter = wreniumGeoBridge.recenterKeepingPointFixed(
            offsetX, offsetY, dragAnchorLatDeg, dragAnchorLonDeg, halfWidthKm, mapArea.width)
        centerLatDeg = clampCenterLat(newCenter[0])
        centerLonDeg = newCenter[1]
        recomputePathThrottled()
    }

    // Cached the same way dragAnchorLatDeg/dragAnchorLonDeg are: grabbed
    // once per gesture and reused across every wheel notch that keeps
    // scrolling over the same screen position, instead of being
    // re-derived from the live (already reprojected) center on every
    // notch -- see the comment above dragAnchorLatDeg for why
    // re-deriving compounds error over a long scroll. Keyed on the
    // cursor offset so a genuine cursor move mid-scroll still grabs a
    // fresh anchor for the new position, exactly as it should.
    property real zoomAnchorLatDeg: 0.0
    property real zoomAnchorLonDeg: 0.0
    property real zoomAnchorOffsetX: NaN
    property real zoomAnchorOffsetY: NaN

    function zoomTowardCursor(offsetX, offsetY, factor) {
        if (offsetX !== zoomAnchorOffsetX || offsetY !== zoomAnchorOffsetY) {
            const anchor = wreniumGeoBridge.unprojectMercatorPoint(
                offsetX, offsetY, centerLatDeg, centerLonDeg, halfWidthKm, mapArea.width)
            zoomAnchorLatDeg = anchor[0]
            zoomAnchorLonDeg = anchor[1]
            zoomAnchorOffsetX = offsetX
            zoomAnchorOffsetY = offsetY
        }
        // Half the equatorial circumference at the wide end -- beyond
        // that the whole world already fits, same spirit as azimuthmap's
        // own rangeKm upper bound.
        const newHalfWidthKm = Math.max(10, Math.min(20015, halfWidthKm * factor))
        const newCenter = wreniumGeoBridge.recenterKeepingPointFixed(
            offsetX, offsetY, zoomAnchorLatDeg, zoomAnchorLonDeg, newHalfWidthKm, mapArea.width)
        halfWidthKm = newHalfWidthKm
        centerLatDeg = clampCenterLat(newCenter[0])
        centerLonDeg = newCenter[1]
        recomputePathThrottled()
    }

    onCenterLatDegChanged: latField.text = centerLatDeg.toFixed(4)
    onCenterLonDegChanged: lonField.text = centerLonDeg.toFixed(4)
    onHalfWidthKmChanged: halfWidthField.text = halfWidthKm.toFixed(0)

    function takeScreenshot(path) {
        screenshotPathField.text = path
        screenshotTimer.start()
    }

    Component.onCompleted: {
        centerLatDeg = clampCenterLat(centerLatDeg) // in case --lat was past the limit
        // .toFixed(), matching onCenterLatDegChanged/onCenterLonDegChanged/
        // onHalfWidthKmChanged below -- .toString() on centerLatDeg here
        // could show excess float precision (e.g. right after the clamp
        // above snaps it to the exact kMercatorMaxLatRad value) that
        // overflows the field's width, showing only the tail end of the
        // number.
        latField.text = centerLatDeg.toFixed(4)
        lonField.text = centerLonDeg.toFixed(4)
        halfWidthField.text = halfWidthKm.toFixed(0)
        recomputePath()
        if (screenshotPath.length > 0) {
            screenshotPathField.text = screenshotPath
            screenshotTimer.start()
        }
    }

    // Same "let the Shape actually paint before grabbing" delay as
    // azimuthmap's own screenshot mechanism.
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
                // the system locale (e.g. Finnish, which uses ","):
                // without it, DoubleValidator both rejects "." and accepts
                // "," -- but the field is read with plain JS parseFloat()
                // (goButton.onClicked below), which always expects "." and
                // silently truncates at a "," (parseFloat("20,015") is 20,
                // not 20015) -- so a system-locale validator wouldn't just
                // block typing ".", it would silently corrupt values typed
                // the way it demands.
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
            Label { text: qsTr("Half-width (km)"); color: "#c7d0da" }
            TextField {
                id: halfWidthField
                Layout.preferredWidth: 90
                validator: DoubleValidator { bottom: 10; top: 20015; locale: "C" }
                onAccepted: goButton.clicked()
            }
            Button {
                id: goButton
                text: qsTr("Go")
                onClicked: {
                    // halfWidthKm must be set before the clamp below --
                    // clampCenterLat is zoom-aware (WreniumGeoBridge.h's
                    // clampMercatorCenterLatDeg) and would otherwise clamp
                    // against the half-width being replaced, not the one
                    // being requested.
                    root.halfWidthKm = parseFloat(halfWidthField.text)
                    root.centerLatDeg = root.clampCenterLat(parseFloat(latField.text))
                    root.centerLonDeg = parseFloat(lonField.text)
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
            text: qsTr("center=(%1, %2) deg  half-width=%3 km  drag to pan, scroll to zoom toward cursor")
                .arg(root.centerLatDeg.toFixed(4))
                .arg(root.centerLonDeg.toFixed(4))
                .arg(root.halfWidthKm.toFixed(0))
        }

        Item {
            id: mapArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Mercator has no geometric clip, unlike azimuthmap's disc -- crop here instead.
            clip: true

            onWidthChanged: root.recomputePathThrottled()
            onHeightChanged: root.recomputePathThrottled()

            Rectangle {
                anchors.fill: parent
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

                onPressed: (mouse) => {
                    root.beginDrag(mouse.x - mapArea.width / 2, mouse.y - mapArea.height / 2)
                }
                onPositionChanged: (mouse) => {
                    if (pressed) {
                        root.dragTo(mouse.x - mapArea.width / 2, mouse.y - mapArea.height / 2)
                    }
                }

                onWheel: (wheel) => {
                    // One notch (120 units of angleDelta.y) is a 10% zoom
                    // step -- scroll up (positive delta) zooms in
                    // (smaller half-width), scroll down zooms out, same
                    // convention as azimuthmap's own zoomBy.
                    const steps = wheel.angleDelta.y / 120
                    const factor = Math.pow(0.9, steps)
                    root.zoomTowardCursor(wheel.x - mapArea.width / 2, wheel.y - mapArea.height / 2, factor)
                }
            }
        }
    }
}
