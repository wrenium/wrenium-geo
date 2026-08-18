// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("wrenium-geo integration example -- minimal Qt Quick app showing how to drive WreniumGeoBridge from lat/lon/range inputs");
    parser.addHelpOption();
    QCommandLineOption latOption("lat", "Initial center latitude (degrees)", "lat", "0.0");
    QCommandLineOption lonOption("lon", "Initial center longitude (degrees)", "lon", "0.0");
    QCommandLineOption rangeOption("range", "Initial range / clip radius (km)", "range", "8000");
    QCommandLineOption screenshotOption("screenshot", "Render once, save a PNG to this path, then exit", "path");
    QCommandLineOption projectionOption("projection", "Radial-distance formula: equidistant (default), orthographic, or gnomonic", "projection", "equidistant");
    parser.addOption(latOption);
    parser.addOption(lonOption);
    parser.addOption(rangeOption);
    parser.addOption(screenshotOption);
    parser.addOption(projectionOption);
    parser.process(app);

    // Matches WreniumGeoBridge::AzimuthalProjection's own declaration
    // order (WreniumGeoBridge.h) -- QML's initialProperties sets the
    // plain int property AzimuthMap.qml reads, not the enum type itself
    // (not visible to this file without pulling in the bridge's own
    // header just for one CLI option).
    int projection = 0;
    const QString projectionValue = parser.value(projectionOption).toLower();
    if (projectionValue == QLatin1String("orthographic")) {
        projection = 1;
    } else if (projectionValue == QLatin1String("gnomonic")) {
        projection = 2;
    }

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {"initialLat", parser.value(latOption).toDouble()},
        {"initialLon", parser.value(lonOption).toDouble()},
        {"initialRange", parser.value(rangeOption).toDouble()},
        {"screenshotPath", parser.value(screenshotOption)},
        {"projection", projection},
    });
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("wrenium_geo_azimuthmap", "AzimuthMap");

    return app.exec();
}
