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
    parser.setApplicationDescription("wrenium-geo Lambert conformal conic integration example -- a regional chart (drag to pan, scroll to zoom, click to read off the coordinate under the cursor), driven by WreniumGeoBridge's conic methods. Defaults to the real ETRS89-LCC / EPSG:3034 definition (standard parallels 35N/65N, origin 52N/10E), the same convention this repo's other Europe-framed LCC demos use.");
    parser.addHelpOption();
    QCommandLineOption parallel1Option("parallel1", "First standard parallel (degrees)", "deg", "35.0");
    QCommandLineOption parallel2Option("parallel2", "Second standard parallel (degrees)", "deg", "65.0");
    QCommandLineOption originLatOption("origin-lat", "Origin latitude (degrees)", "deg", "52.0");
    QCommandLineOption originLonOption("origin-lon", "Origin/central-meridian longitude (degrees)", "deg", "10.0");
    QCommandLineOption halfWidthOption("halfwidth", "Initial half-width of the visible chart (km)", "halfwidth", "3000");
    QCommandLineOption screenshotOption("screenshot", "Render once, save a PNG to this path, then exit", "path");
    parser.addOption(parallel1Option);
    parser.addOption(parallel2Option);
    parser.addOption(originLatOption);
    parser.addOption(originLonOption);
    parser.addOption(halfWidthOption);
    parser.addOption(screenshotOption);
    parser.process(app);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {"initialStandardParallel1Deg", parser.value(parallel1Option).toDouble()},
        {"initialStandardParallel2Deg", parser.value(parallel2Option).toDouble()},
        {"initialOriginLatDeg", parser.value(originLatOption).toDouble()},
        {"initialOriginLonDeg", parser.value(originLonOption).toDouble()},
        {"initialHalfWidthKm", parser.value(halfWidthOption).toDouble()},
        {"screenshotPath", parser.value(screenshotOption)},
    });
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("wrenium_geo_lccmap", "LccMap");

    return app.exec();
}
