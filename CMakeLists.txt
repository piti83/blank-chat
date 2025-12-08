cmake_minimum_required(VERSION 3.28)
project(blank-chat VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(BUILD_TESTS "Build Unit Tests" ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

if(ENABLE_ASAN)
  message(STATUS "Build with AddressSanitizer enabled")
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
elseif(ENABLE_TSAN)
  message(STATUS "Build with ThreadSanitizer enabled")
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread)
endif()

include(FetchContent)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.16.0
)
FetchContent_MakeAvailable(spdlog)

if(BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

file(GLOB_RECURSE CORE_SOURCES "core/src/*.cpp")
add_library(blank-chat-core
  ${CORE_SOURCES}
)
target_link_libraries(blank-chat-core PUBLIC spdlog::spdlog)
target_include_directories(blank-chat-core PUBLIC core/include)


file(GLOB_RECURSE SERVER_SOURCES "server/src/*.cpp")
add_executable(blank-chat-server
  ${SERVER_SOURCES}
)
target_link_libraries(blank-chat-server PRIVATE blank-chat-core)
target_include_directories(blank-chat-server PRIVATE server/include)

file(GLOB_RECURSE CLIENT_SOURCES "client/src/*.cpp")
add_executable(blank-chat-client
  ${CLIENT_SOURCES}
)

target_link_libraries(blank-chat-client PRIVATE blank-chat-core spdlog::spdlog)
target_include_directories(blank-chat-client PRIVATE client/include)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
