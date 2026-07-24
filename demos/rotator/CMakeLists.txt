# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

qt_add_executable(rotator
    main.cpp
)

qt_add_qml_module(rotator
    URI wrenium_geo_demo
    VERSION 1.0
    QML_FILES
        Main.qml
    SOURCES
        RotatorDriver.h
        RotatorDriver.cpp
)

# Excluded from `all` -- build via the `demos` target or by name.
set_target_properties(rotator PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(rotator
    PRIVATE Qt6::Quick Qt6::QuickControls2 Wrenium::geo wrenium_geo_qt_bridgeplugin
)

include(GNUInstallDirs)
install(TARGETS rotator
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
