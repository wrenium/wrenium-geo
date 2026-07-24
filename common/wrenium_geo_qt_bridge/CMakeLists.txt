# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

qt_add_library(wrenium_geo_qt_bridge STATIC)

qt_add_qml_module(wrenium_geo_qt_bridge
    URI wrenium_geo_qt_bridge
    VERSION 1.0
    SOURCES
        WreniumGeoBridge.h
        WreniumGeoBridge.cpp
)

target_include_directories(wrenium_geo_qt_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/data)

target_link_libraries(wrenium_geo_qt_bridge PUBLIC Qt6::Quick Wrenium::geo)
target_link_libraries(wrenium_geo_qt_bridge PRIVATE wrenium_geo_binary_path_decoder_example)
