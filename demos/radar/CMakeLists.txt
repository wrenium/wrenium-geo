# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

qt_add_executable(radar
    main.cpp
)

qt_add_qml_module(radar
    URI wrenium_geo_radar_demo
    VERSION 1.0
    QML_FILES
        Main.qml
)

# Excluded from `all` -- build via the `demos` target or by name.
set_target_properties(radar PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(radar
    PRIVATE Qt6::Quick Wrenium::geo wrenium_geo_qt_bridgeplugin wrenium_geo_warnings
)

include(GNUInstallDirs)
install(TARGETS radar
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
