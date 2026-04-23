option(USE_FETCH_CONTENT "Download dependencies via FetchContent" ON)

if(USE_FETCH_CONTENT)
  message(STATUS "Using FetchContent to download dependencies locally...")
  include(FetchContent)

  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

  FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.13.0
    )

  FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )

  FetchContent_Declare(libsodium
        GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
        GIT_TAG 1.0.19-RELEASE
    )

  FetchContent_MakeAvailable(spdlog googletest libsodium)
else()
  message(STATUS "FetchContent OFF - using system/Yocto provided packages...")
  find_package(spdlog CONFIG REQUIRED)
  find_package(GTest CONFIG REQUIRED)

  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SODIUM REQUIRED libsodium)
endif()

add_library(ext_sodium INTERFACE)
add_library(ext::sodium ALIAS ext_sodium)

if(USE_FETCH_CONTENT)
  target_link_libraries(ext_sodium INTERFACE sodium)
  target_include_directories(ext_sodium INTERFACE "${libsodium_SOURCE_DIR}/src/libsodium/include")
else()
  target_link_libraries(ext_sodium INTERFACE ${SODIUM_LIBRARIES})
  target_include_directories(ext_sodium INTERFACE ${SODIUM_INCLUDE_DIRS})
endif()
