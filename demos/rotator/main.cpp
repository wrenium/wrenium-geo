// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Fusion: a clean, native-feeling desktop look for the Slider/Switch/
    // Label controls -- the default "Basic" style those otherwise fall
    // back to (QtQuick.Controls doesn't apply a real style unless one is
    // explicitly requested) looks noticeably flatter/rougher.
    QQuickStyle::setStyle("Fusion");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("wrenium_geo_demo", "Main");

    return app.exec();
}
