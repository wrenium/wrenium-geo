# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

qt_add_executable(azimuthmap
    main.cpp
)

qt_add_qml_module(azimuthmap
    URI wrenium_geo_azimuthmap
    VERSION 1.0
    QML_FILES
        AzimuthMap.qml
)

# Excluded from `all` -- build via the `examples` target or by name.
set_target_properties(azimuthmap PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(azimuthmap
    PRIVATE Qt6::Quick Qt6::QuickControls2 Wrenium::geo wrenium_geo_qt_bridgeplugin
)

include(GNUInstallDirs)
install(TARGETS azimuthmap
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
