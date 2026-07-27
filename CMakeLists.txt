# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

cmake_minimum_required(VERSION 3.21)

project(wrenium-geo
    VERSION 0.2.3
    DESCRIPTION "Header-only geometry library projecting geographic coastline and border data from an arbitrary center point"
    LANGUAGES CXX
)

add_library(wrenium-geo INTERFACE)
add_library(Wrenium::geo ALIAS wrenium-geo)

target_include_directories(wrenium-geo INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(wrenium-geo INTERFACE cxx_std_17)

include(FetchContent)
FetchContent_Declare(
    wrenium-f32math
    GIT_REPOSITORY https://github.com/wrenium/wrenium-f32math.git
    GIT_TAG v0.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(wrenium-f32math)
target_link_libraries(wrenium-geo INTERFACE Wrenium::f32math)

# Shared warning flags for this project's own targets (not FetchContent'd
# third-party code) -- link PRIVATE rather than duplicating the MSVC/else
# block in every subdirectory's CMakeLists.txt.
add_library(wrenium_geo_warnings INTERFACE)
if(MSVC)
    target_compile_options(wrenium_geo_warnings INTERFACE /W4)
else()
    target_compile_options(wrenium_geo_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.5.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(doctest)

    add_subdirectory(tests)
endif()

# tools/, examples/, demos/ only build at the top level.
if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tools/wrenium_geo_convert)

    # QUIET + version-pinned: don't force a Qt install for library-only builds.
    find_package(Qt6 6.8 QUIET COMPONENTS Quick QuickControls2)
    if(Qt6_FOUND)
        qt_standard_project_setup(REQUIRES 6.8)

        add_subdirectory(common/binary_path_decoder_example)
        add_subdirectory(common/wrenium_geo_qt_bridge)

        add_subdirectory(examples/azimuthmap)
        add_custom_target(examples)
        add_dependencies(examples azimuthmap)

        add_subdirectory(demos/rotator)
        add_subdirectory(demos/radar)
        add_custom_target(demos)
        add_dependencies(demos rotator radar)
    else()
        message(STATUS "Qt6 >= 6.8 not found -- examples/ and demos/ Qt apps will not be available (tests and topojson2bin are unaffected)")
    endif()
endif()
