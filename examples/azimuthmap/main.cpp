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
    QCommandLineOption orthographicOption("orthographic", "Use the orthographic projection instead of the default equidistant");
    parser.addOption(latOption);
    parser.addOption(lonOption);
    parser.addOption(rangeOption);
    parser.addOption(screenshotOption);
    parser.addOption(orthographicOption);
    parser.process(app);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {"initialLat", parser.value(latOption).toDouble()},
        {"initialLon", parser.value(lonOption).toDouble()},
        {"initialRange", parser.value(rangeOption).toDouble()},
        {"screenshotPath", parser.value(screenshotOption)},
        {"orthographic", parser.isSet(orthographicOption)},
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
