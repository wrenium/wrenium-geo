# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

add_executable(tests
    test_main.cpp
    test_buffer.cpp
    test_geo_point.cpp
    test_projection.cpp
    test_orthographic.cpp
    test_mercator.cpp
    test_clip.cpp
    test_pipeline.cpp
    test_input_format.cpp
    test_binary_emitter.cpp
    test_svg_emitter.cpp
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
