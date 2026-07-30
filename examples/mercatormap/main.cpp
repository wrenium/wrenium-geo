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
    parser.setApplicationDescription("wrenium-geo Web Mercator integration example -- interactive whole-world map (drag to pan, scroll to zoom toward the cursor), driven by WreniumGeoBridge's Web Mercator methods");
    parser.addHelpOption();
    QCommandLineOption latOption("lat", "Initial center latitude (degrees)", "lat", "0.0");
    QCommandLineOption lonOption("lon", "Initial center longitude (degrees)", "lon", "0.0");
    QCommandLineOption halfWidthOption("halfwidth", "Initial half-width of the visible map (km)", "halfwidth", "8000");
    QCommandLineOption screenshotOption("screenshot", "Render once, save a PNG to this path, then exit", "path");
    parser.addOption(latOption);
    parser.addOption(lonOption);
    parser.addOption(halfWidthOption);
    parser.addOption(screenshotOption);
    parser.process(app);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {"initialLat", parser.value(latOption).toDouble()},
        {"initialLon", parser.value(lonOption).toDouble()},
        {"initialHalfWidthKm", parser.value(halfWidthOption).toDouble()},
        {"screenshotPath", parser.value(screenshotOption)},
    });
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("wrenium_geo_mercatormap", "MercatorMap");

    return app.exec();
}
