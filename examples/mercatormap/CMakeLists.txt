# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

qt_add_executable(mercatormap
    main.cpp
)

qt_add_qml_module(mercatormap
    URI wrenium_geo_mercatormap
    VERSION 1.0
    QML_FILES
        MercatorMap.qml
)

# Excluded from `all` -- build via the `examples` target or by name.
set_target_properties(mercatormap PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(mercatormap
    PRIVATE Qt6::Quick Qt6::QuickControls2 Wrenium::geo wrenium_geo_qt_bridgeplugin wrenium_geo_warnings
)

include(GNUInstallDirs)
install(TARGETS mercatormap
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
