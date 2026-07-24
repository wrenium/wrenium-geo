# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

add_library(wrenium_geo_binary_path_decoder_example INTERFACE)

target_include_directories(wrenium_geo_binary_path_decoder_example INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(wrenium_geo_binary_path_decoder_example INTERFACE Wrenium::geo)
