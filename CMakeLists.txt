cmake_minimum_required(VERSION 3.25)
project(blank-chat LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

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
option(BC_ENABLE_LOGS "Enable detailed diagnostic logging via spdlog" OFF)

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
  add_compile_options(--coverage -O0 -g -U_FORTIFY_SOURCE)
  add_link_options(--coverage)
  message(STATUS "Code Coverage enabled (--coverage)")

  find_program(GCOVR_EXE NAMES gcovr)

  if(DEFINED ENV{CROSS_COMPILE})
    set(GCOV_CMD "$ENV{CROSS_COMPILE}gcov")
  else()
    set(GCOV_CMD "gcov")
  endif()
  # ---------------------------------------

  if(GCOVR_EXE)
    add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage_report
            COMMAND ${GCOVR_EXE}
                --root ${CMAKE_SOURCE_DIR}
                --filter "${CMAKE_SOURCE_DIR}/libs"
                --exclude-unreachable-branches
                --txt-metric branch
                --html-details ${CMAKE_BINARY_DIR}/coverage_report/index.html
                --print-summary
                --gcov-executable ${GCOV_CMD}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests and generating HTML coverage report using ${GCOV_CMD}..."
        )
  else()
    message(WARNING "gcovr not found! 'coverage' target will not be created.")
  endif()
endif()

option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" ON)

find_program(CLANG_TIDY_EXE NAMES "clang-tidy")

find_program(CLANG_TIDY_EXE NAMES "clang-tidy")

if(CLANG_TIDY_EXE AND ENABLE_CLANG_TIDY)
  set(CLANG_TIDY_ARGS
    "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy"
    "--header-filter=${CMAKE_CURRENT_SOURCE_DIR}/(libs|apps|tests)/.*"
  )

  if(DEFINED ENV{OECORE_TARGET_SYSROOT})
    set(YOCTO_SYSROOT "$ENV{OECORE_TARGET_SYSROOT}")

    file(GLOB YOCTO_CXX_DIRS "${YOCTO_SYSROOT}/usr/include/c++/*")
    if(YOCTO_CXX_DIRS)
      list(GET YOCTO_CXX_DIRS 0 YOCTO_CXX_INC)
      list(APPEND CLANG_TIDY_ARGS
        "--extra-arg=-isystem${YOCTO_CXX_INC}"
        "--extra-arg=-isystem${YOCTO_CXX_INC}/x86_64-poky-linux"
      )
      message(STATUS "Yocto SDK detected. Forwarding C++ headers to clang-tidy: ${YOCTO_CXX_INC}")
    endif()

    file(GLOB LD_LINUX "${YOCTO_SYSROOT}/lib/ld-linux-x86-64.so.2" "${YOCTO_SYSROOT}/lib64/ld-linux-x86-64.so.2")
    if(LD_LINUX)
      list(GET LD_LINUX 0 LD_LINUX_PATH)
      add_link_options(
        "-Wl,-dynamic-linker=${LD_LINUX_PATH}"
        "-Wl,-rpath=${YOCTO_SYSROOT}/lib:${YOCTO_SYSROOT}/usr/lib"
      )
      message(STATUS "Yocto SDK Out-of-Tree detected. ELF interpreter set to: ${LD_LINUX_PATH}")
    endif()
  endif()

  set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE} ${CLANG_TIDY_ARGS})
elseif(NOT ENABLE_CLANG_TIDY)
  message(STATUS "clang-tidy is explicitly disabled via ENABLE_CLANG_TIDY.")
endif()

find_program(CLANG_FORMAT_EXE NAMES "clang-format")
if(CLANG_FORMAT_EXE)
  file(GLOB_RECURSE ALL_SOURCE_FILES
        "libs/*.cpp" "libs/*.h" "libs/*.hpp"
        "apps/*.cpp" "tests/*.cpp"
    )
  add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${ALL_SOURCE_FILES}
        COMMENT "Formatting all C++ files with clang-format..."
    )
endif()

if(BC_ENABLE_LOGS)
  add_compile_definitions(BC_ENABLE_LOGS)
  message(STATUS "Diagnostic logging enabled (BC_ENABLE_LOGS)")

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options("-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.")
  endif()
endif()

add_subdirectory(libs)
add_subdirectory(apps)

option(BUILD_TESTING "Build the testing tree" ON)

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
  message(STATUS "Unit tests are ENABLED.")
else()
  message(STATUS "Unit tests are DISABLED.")
endif()
