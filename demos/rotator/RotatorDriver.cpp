// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "RotatorDriver.h"

#include <cmath>

#include <QDebug>
#include <QString>

namespace {
// Tuned for a demo: visibly "motor-like" (not instant, not glacial) rather
// than matched to any real rotator's actual slew rate.
constexpr int kTickIntervalMs = 50;     // 20 Hz
constexpr double kDegreesPerTick = 1.0; // 20 deg/sec at 20 Hz
} // namespace

RotatorDriver::RotatorDriver(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(kTickIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &RotatorDriver::tick);
}

double RotatorDriver::wrapDegrees(double degrees)
{
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees;
}

void RotatorDriver::setTargetHeading(double headingDeg)
{
    // m_heading/m_target stay unwrapped (allowed to grow past 360 or go
    // negative) so QML's `Behavior on angle` animation can interpolate
    // linearly between old and new values without ever crossing the
    // 0/360 seam -- a wrapped 359 -> 1 step would otherwise animate the
    // long way around (358 degrees) instead of the intended short step.
    // Only the requested heading and the shortest-path delta need wrapping.
    const double requestedWrapped = wrapDegrees(headingDeg);
    double delta = requestedWrapped - wrapDegrees(m_heading);
    while (delta > 180.0) {
        delta -= 360.0;
    }
    while (delta <= -180.0) {
        delta += 360.0;
    }
    m_target = m_heading + delta;
    m_jogDirection = 0;

    qDebug().noquote() << QString("RotatorDriver: target heading set to %1 deg").arg(requestedWrapped, 0, 'f', 1);
    emit targetChanged(m_target);

    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void RotatorDriver::jogClockwise()
{
    m_jogDirection = 1;
    m_target = m_heading + kDegreesPerTick;
    qDebug().noquote() << "RotatorDriver: jogging CW";
    emit targetChanged(m_target);
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void RotatorDriver::jogCounterClockwise()
{
    m_jogDirection = -1;
    m_target = m_heading - kDegreesPerTick;
    qDebug().noquote() << "RotatorDriver: jogging CCW";
    emit targetChanged(m_target);
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void RotatorDriver::stop()
{
    m_jogDirection = 0;
    m_target = m_heading;
    m_timer.stop();
    qDebug().noquote() << QString("RotatorDriver: stopped at %1 deg").arg(wrapDegrees(m_heading), 0, 'f', 1);
    emit targetChanged(m_target);
}

void RotatorDriver::tick()
{
    if (m_jogDirection != 0) {
        // Keep target exactly one step ahead of heading in the jog
        // direction, every tick -- the motor advances by kDegreesPerTick
        // indefinitely (never "arrives"), and target/heading stay close
        // together the whole time instead of target sitting at some huge,
        // visually arbitrary absolute value (see jogClockwise()'s comment).
        m_heading += kDegreesPerTick * m_jogDirection;
        m_target = m_heading + kDegreesPerTick * m_jogDirection;
        emit headingChanged(m_heading);
        emit targetChanged(m_target);
        return;
    }

    const double delta = m_target - m_heading;

    if (std::fabs(delta) <= kDegreesPerTick) {
        m_heading = m_target;
        m_timer.stop();
        qDebug().noquote() << QString("RotatorDriver: reached heading %1 deg").arg(wrapDegrees(m_heading), 0, 'f', 1);
    } else {
        m_heading += (delta > 0.0 ? kDegreesPerTick : -kDegreesPerTick);
    }

    emit headingChanged(m_heading);
}
