// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#ifndef WRENIUM_GEO_DEMO_ROTATOR_DRIVER_H
#define WRENIUM_GEO_DEMO_ROTATOR_DRIVER_H

#include <QObject>
#include <QTimer>
#include <qqmlintegration.h>

// Stand-in for the actual rotator hardware interface (serial/CAT control,
// GPIO step/direction lines, etc. on a real embedded target). This desktop
// stub simulates a physical rotator's slow motor movement (a fixed degrees-
// per-tick rate toward whatever heading was last requested, always via the
// shorter rotational direction) and logs each step to the console, so the
// demo app has a single, clearly-marked seam where a real driver would
// plug in instead.
class RotatorDriver : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double heading READ heading NOTIFY headingChanged)
    Q_PROPERTY(double target READ target NOTIFY targetChanged)

public:
    explicit RotatorDriver(QObject *parent = nullptr);

    // The rotator's actual, physical position -- what a real driver would
    // read back from the hardware, updated one simulated motor step at a
    // time. Feed this (not `target`) to anything meant to show where the
    // antenna is actually pointing right now, e.g. the map's heading
    // needle. Deliberately *not* wrapped to [0, 360) -- can grow past 360
    // or go negative as it accumulates -- so a consumer animating this
    // value (Main.qml's `Behavior on angle`) never has to special-case a
    // jump across the 0/360 seam; it's numerically equivalent to the
    // wrapped angle for anything that only cares about the direction a
    // Rotation transform points. Wrap with fmod (plus a +360/%360 fixup
    // for negative results) before displaying it as a 0-360 value.
    double heading() const { return m_heading; }

    // The last *requested* heading -- what the user asked for, available
    // immediately (unlike `heading`, which lags behind it while the
    // simulated motor is still moving). Feed this to anything meant to
    // show where the antenna is being commanded to point. Same unwrapped
    // convention as `heading()`.
    double target() const { return m_target; }

    // Requests a new target heading (0-360, any input value is wrapped) --
    // called whenever the user sets one (heading slider, or clicking/
    // dragging the map in "aim antenna" mode). Starts (or retargets) the
    // simulated slow rotation toward it; a real driver would send this to
    // the physical rotator here instead.
    Q_INVOKABLE void setTargetHeading(double headingDeg);

    // Manual jog controls, matching a real rotator control box's momentary
    // CW/CCW/STOP buttons (as opposed to setTargetHeading()'s "go to this
    // exact azimuth" preset behavior): each tick while jogging keeps
    // `target` exactly one step ahead of `heading` in the requested
    // direction (see tick()), so the motor keeps turning that way
    // indefinitely without ever "arriving". Kept close to `heading` rather
    // than set to a single far-off value, since `target` is rendered
    // directly as a Rotation angle in Main.qml and a huge absolute value
    // would render at an arbitrary-looking jump the instant a jog button
    // is pressed. Intended to be called from a button's onPressed, paired
    // with stop() on the same button's onReleased/onCanceled -- rotation
    // only happens while held, like the physical buttons this simulates.
    Q_INVOKABLE void jogClockwise();
    Q_INVOKABLE void jogCounterClockwise();

    // Halts the simulated motor immediately at its current position (sets
    // target = heading and stops the tick timer), regardless of whether it
    // was moving toward a setTargetHeading() preset or jogging. A real
    // driver would send an explicit stop command to the hardware here.
    Q_INVOKABLE void stop();

signals:
    void headingChanged(double headingDeg);
    void targetChanged(double targetDeg);

private slots:
    void tick();

private:
    static double wrapDegrees(double degrees);

    double m_heading = 0.0;
    double m_target = 0.0;
    // Set only by jogClockwise()/jogCounterClockwise() (+1/-1), cleared by
    // stop() and setTargetHeading() -- tick() branches on this to decide
    // between "keep turning indefinitely" and "seek this specific target".
    int m_jogDirection = 0;
    QTimer m_timer;
};

#endif // WRENIUM_GEO_DEMO_ROTATOR_DRIVER_H
