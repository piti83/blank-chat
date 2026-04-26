add_library(ext_sodium INTERFACE)
add_library(ext::sodium ALIAS ext_sodium)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)
target_link_libraries(ext_sodium INTERFACE ${SODIUM_LIBRARIES})
target_include_directories(ext_sodium INTERFACE ${SODIUM_INCLUDE_DIRS})

find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
