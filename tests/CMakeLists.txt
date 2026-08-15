# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

add_executable(tests
    test_main.cpp
    test_error.cpp
    test_buffer.cpp
    test_geo_point.cpp
    test_projection.cpp
    test_spherical.cpp
    test_orthographic.cpp
    test_mercator.cpp
    test_clip.cpp
    test_pipeline.cpp
    test_input_format.cpp
    test_binary_emitter.cpp
    test_binary_format.cpp
    test_svg_emitter.cpp
    test_float_format.cpp
    test_pipeline_benchmark.cpp
)

target_link_libraries(tests PRIVATE Wrenium::geo doctest::doctest wrenium_geo_warnings)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tests PRIVATE cxx_std_17)

# Matches the MCU target: no exceptions/RTTI.
target_compile_definitions(tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
if(MSVC)
    target_compile_options(tests PRIVATE /EHs-c- /GR-)
else()
    target_compile_options(tests PRIVATE -fno-exceptions -fno-rtti)
endif()

# Excluded from `all` -- build with an explicit target name.
set_target_properties(tests PROPERTIES EXCLUDE_FROM_ALL TRUE)

add_test(NAME tests COMMAND tests)

# A separate binary, not an extra source file folded into `tests` above:
# WRENIUM_GEO_TRACK_HIGH_WATER_MARK changes Buffer<T, Capacity>'s actual
# layout, so building some translation units of one binary with it and
# others without would be an ODR violation, not just an inconsistent
# build -- see buffer.h's own comment.
add_executable(tests_high_water_mark
    test_main.cpp
    test_buffer_high_water_mark.cpp
)

target_link_libraries(tests_high_water_mark PRIVATE Wrenium::geo doctest::doctest wrenium_geo_warnings)
target_include_directories(tests_high_water_mark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tests_high_water_mark PRIVATE cxx_std_17)
target_compile_definitions(tests_high_water_mark PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    WRENIUM_GEO_TRACK_HIGH_WATER_MARK
)
if(MSVC)
    target_compile_options(tests_high_water_mark PRIVATE /EHs-c- /GR-)
else()
    target_compile_options(tests_high_water_mark PRIVATE -fno-exceptions -fno-rtti)
endif()

set_target_properties(tests_high_water_mark PROPERTIES EXCLUDE_FROM_ALL TRUE)

add_test(NAME tests_high_water_mark COMMAND tests_high_water_mark)
