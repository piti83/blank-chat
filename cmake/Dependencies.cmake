include(ExternalProject)
include(FetchContent)

option(USE_FETCH_CONTENT "Download dependencies locally" ON)

if(USE_FETCH_CONTENT)
  message(STATUS "Using FetchContent/ExternalProject for dependencies...")

  FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.13.0
    )
  FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
  FetchContent_MakeAvailable(spdlog googletest)

  set(LIBSODIUM_PREFIX "${CMAKE_BINARY_DIR}/external/libsodium")
  set(LIBSODIUM_INCLUDE_DIR "${LIBSODIUM_PREFIX}/include")
  set(LIBSODIUM_LIB "${LIBSODIUM_PREFIX}/lib/libsodium.a")

  ExternalProject_Add(libsodium_external
      GIT_REPOSITORY      https://github.com/jedisct1/libsodium.git
      GIT_TAG             1.0.19-RELEASE
      PREFIX              ${LIBSODIUM_PREFIX}
      CONFIGURE_COMMAND   <SOURCE_DIR>/configure --prefix=${LIBSODIUM_PREFIX} --enable-static --disable-shared --with-pic

      BUILD_COMMAND       make -j4

      INSTALL_COMMAND     make install
      UPDATE_COMMAND      ""
      BUILD_BYPRODUCTS    ${LIBSODIUM_LIB}
  )

  add_library(sodium_static STATIC IMPORTED GLOBAL)
  add_dependencies(sodium_static libsodium_external)
  set_target_properties(sodium_static PROPERTIES
      IMPORTED_LOCATION "${LIBSODIUM_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${LIBSODIUM_INCLUDE_DIR}"
  )
endif()

add_library(ext_sodium INTERFACE)
add_library(ext::sodium ALIAS ext_sodium)

if(USE_FETCH_CONTENT)
  target_link_libraries(ext_sodium INTERFACE sodium_static)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SODIUM REQUIRED libsodium)
  target_link_libraries(ext_sodium INTERFACE ${SODIUM_LIBRARIES})
  target_include_directories(ext_sodium INTERFACE ${SODIUM_INCLUDE_DIRS})

  find_package(spdlog CONFIG REQUIRED)
  find_package(GTest CONFIG REQUIRED)
endif()
