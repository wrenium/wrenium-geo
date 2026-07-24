# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Tommi Tauriainen

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(topojson2bin_lib STATIC
    src/topojson_decoder.cpp
    src/writer.cpp
)

target_include_directories(topojson2bin_lib PUBLIC src)
target_link_libraries(topojson2bin_lib PUBLIC Wrenium::geo nlohmann_json::nlohmann_json)
target_compile_features(topojson2bin_lib PUBLIC cxx_std_17)

add_executable(topojson2bin src/main.cpp)
target_link_libraries(topojson2bin PRIVATE topojson2bin_lib)

if(BUILD_TESTING)
    # Reuse extern/doctest if the parent build already vendored it.
    if(NOT TARGET doctest::doctest)
        FetchContent_Declare(
            doctest
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG v2.4.11
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(doctest)
    endif()

    add_executable(topojson2bin_tests tests/test_topojson_decoder.cpp)
    target_link_libraries(topojson2bin_tests PRIVATE topojson2bin_lib doctest::doctest)

    target_compile_definitions(topojson2bin_tests PRIVATE
        WRENIUM_GEO_CONVERT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    )

    set_target_properties(topojson2bin_tests PROPERTIES EXCLUDE_FROM_ALL TRUE)

    add_test(NAME topojson2bin_tests COMMAND topojson2bin_tests)
endif()
