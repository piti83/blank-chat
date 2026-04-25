cmake_minimum_required(VERSION 3.25)
project(blank-chat LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(USE_ASAN "Enable Address Sanitizer" OFF)
option(USE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(USE_TSAN "Enable Thread Sanitizer" OFF)
option(USE_SAFE_STL "Enable Debugging Mode for STL Containers" OFF)
option(USE_COVERAGE "Enable Code Coverage reporting" OFF)
option(BC_ENABLE_LOGS "Enable detailed diagnostic logging via spdlog" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" ON)
option(BUILD_TESTING "Build the testing tree" ON)

include(CompilerWarnings)
include(Sanitizers)
include(StaticAnalysis)
include(Coverage)
include(YoctoHelpers)

setup_format_target()
setup_coverage_target()

add_subdirectory(libs)
add_subdirectory(apps)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
    message(STATUS "Unit tests are ENABLED.")
else()
    message(STATUS "Unit tests are DISABLED.")
endif()
