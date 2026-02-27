cmake_minimum_required(VERSION 3.25)
project(blank-chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
    -Wall 
    -Wextra 
    -Wpedantic 
    -Werror 
    -fstack-protector-all 
)

option(USE_ASAN "Enable Address Sanitizer" OFF)
option(USE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(USE_TSAN "Enable Thread Sanitizer" OFF)
option(USE_SAFE_STL "Enable Debugging Mode for STL Containers" OFF)
option(USE_COVERAGE "Enable Code Coverage reporting" OFF)

if(USE_SAFE_STL)
    add_compile_definitions(_GLIBCXX_DEBUG)
    message(STATUS "Safe STL (GLIBCXX_DEBUG) enabled")
endif()

if(USE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    message(STATUS "AddressSanitizer enabled")
endif()

if(USE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
    message(STATUS "UndefinedBehaviorSanitizer enabled")
endif()

if(USE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
    message(STATUS "ThreadSanitizer enabled")
endif()

if(USE_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
    message(STATUS "Code Coverage enabled (--coverage)")

    find_program(GCOVR_EXE NAMES gcovr)
    if(GCOVR_EXE)
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage_report
            COMMAND ${GCOVR_EXE} 
                --root ${CMAKE_SOURCE_DIR}
                --filter "${CMAKE_SOURCE_DIR}/modules" 
                --exclude-unreachable-branches
                --branches 
                --html-details ${CMAKE_BINARY_DIR}/coverage_report/index.html
                --print-summary
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests and generating HTML coverage report..."
        )
    else()
        message(WARNING "gcovr not found! 'coverage' target will not be created.")
    endif()
endif()

find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE} "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
endif()

find_program(CLANG_FORMAT_EXE NAMES "clang-format")
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE ALL_SOURCE_FILES 
        "modules/*.cpp" "modules/*.h" "modules/*.hpp"
        "apps/*.cpp" "tests/*.cpp"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${ALL_SOURCE_FILES}
        COMMENT "Formatting all C++ files with clang-format..."
    )
endif()

find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_subdirectory(modules)
add_subdirectory(apps)

enable_testing()
add_subdirectory(tests)
