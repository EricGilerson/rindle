cmake_minimum_required(VERSION 3.22)
project(rivulet LANGUAGES CXX)

# Default to Release unless the user sets something else
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings and sanitizers (toggle via options)
option(RIVULET_ENABLE_WARNINGS "Enable strict warnings" ON)
option(RIVULET_ENABLE_ASAN "Enable AddressSanitizer in Debug" ON)
option(RIVULET_BUILD_TESTS "Build unit tests" ON)
option(RIVULET_BUILD_CLI "Build CLI tool" ON)

# Global warnings (Clang/GCC)
if(RIVULET_ENABLE_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion)
    elseif(MSVC)
        add_compile_options(/W4 /permissive-)
    endif()
endif()

# AddressSanitizer in Debug
if(RIVULET_ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
    endif()
endif()

# Library target
add_library(rivulet STATIC
        src/engine.cpp
        include/rivulet/engine.hpp
        include/rivulet/types.hpp
        src/engine.cpp
        include/rivulet/scaler.hpp
        src/scaler.cpp
)
target_include_directories(rivulet PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Dependencies (add real ones later; Arrow/Parquet/pybind11/FAISS are future steps)
# For now, just standard library.

# CLI
if(RIVULET_BUILD_CLI)
    add_executable(rivulet_cli src/main_cli.cpp
            src/main_cli.cpp)
    target_link_libraries(rivulet_cli PRIVATE rivulet)
    set_target_properties(rivulet_cli PROPERTIES OUTPUT_NAME "rivulet")
endif()

# Tests
if(RIVULET_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
            catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.6.0
    )
    FetchContent_MakeAvailable(catch2)

    add_executable(rivulet_tests tests/engine_basic_tests.cpp
            tests/engine_basic_tests.cpp)
    target_link_libraries(rivulet_tests PRIVATE rivulet Catch2::Catch2WithMain)
    include(CTest)
    include(Catch)
    catch_discover_tests(rivulet_tests)
endif()
