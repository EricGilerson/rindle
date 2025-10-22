cmake_minimum_required(VERSION 3.22)
project(rindle LANGUAGES CXX)

include(GNUInstallDirs)

# Default to Release unless the user sets something else
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(RINDLE_ENABLE_WARNINGS "Enable strict warnings" ON)
option(RINDLE_ENABLE_ASAN "Enable AddressSanitizer in Debug" ON)
option(RINDLE_BUILD_TESTS "Build unit tests" ON)
option(RINDLE_BUILD_EXAMPLES "Build example executables" ON)
option(RINDLE_BUILD_PYTHON "Build Python bindings" ON)

# Warnings (Clang/GCC/MSVC)
if(RINDLE_ENABLE_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow
                -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
                -Wnull-dereference -Wdouble-promotion
        )
    elseif(MSVC)
        add_compile_options(/W4 /permissive-)
    endif()
endif()

# AddressSanitizer in Debug
if(RINDLE_ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
    endif()
endif()

# Dependencies
include(FetchContent)

FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Library target
add_library(rindle STATIC
        # Public headers
        include/rindle/types.hpp
        include/rindle/dataset_types.hpp
        include/rindle/scaler.hpp
        include/rindle.hpp

        # Private headers
        src/internal/csv_io.hpp
        src/internal/manifest.hpp
        src/internal/window_maker.hpp
        src/internal/catalog.hpp
        src/internal/driver.hpp
        src/internal/window_manifest.hpp
        src/internal/dataset_builder.hpp

        # Sources
        src/scaler.cpp
        src/csv_io.cpp
        src/manifest.cpp
        src/window_maker.cpp
        src/catalog.cpp
        src/driver.cpp
        src/types.cpp
        src/window_manifest.cpp
        src/dataset_builder.cpp
        src/rindle_api.cpp
)

target_include_directories(rindle
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rindle
        PUBLIC nlohmann_json::nlohmann_json
)

set_target_properties(rindle PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Tests
if(RINDLE_BUILD_TESTS)
    FetchContent_Declare(
            catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.6.0
    )
    FetchContent_MakeAvailable(catch2)

    add_executable(rindle_tests
            tests/engine_basic_tests.cpp
    )
    target_link_libraries(rindle_tests PRIVATE rindle Catch2::Catch2WithMain)
    include(CTest)
    include(Catch)
    catch_discover_tests(rindle_tests)
endif()

# Examples
if(RINDLE_BUILD_EXAMPLES)
    add_executable(example_usage examples/example_usage.cpp)
    target_link_libraries(example_usage PRIVATE rindle)
endif()


if(RINDLE_BUILD_PYTHON)
    FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.12.0
    )
    FetchContent_MakeAvailable(pybind11)

    pybind11_add_module(rindle_python MODULE
            src/python/bindings.cpp
    )

    target_link_libraries(rindle_python PRIVATE rindle)
    target_include_directories(rindle_python PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(rindle_python PROPERTIES
            OUTPUT_NAME "rindle"
            ARCHIVE_OUTPUT_NAME "rindle_python"
    )

    install(
            TARGETS rindle_python
            LIBRARY DESTINATION rindle
            ARCHIVE DESTINATION rindle
            RUNTIME DESTINATION rindle
    )
endif()
