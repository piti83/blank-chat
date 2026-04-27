find_package(PkgConfig REQUIRED)

add_library(ext_sodium INTERFACE)
add_library(ext::sodium ALIAS ext_sodium)
pkg_check_modules(SODIUM REQUIRED libsodium)
target_link_libraries(ext_sodium INTERFACE ${SODIUM_LIBRARIES})
target_include_directories(ext_sodium INTERFACE ${SODIUM_INCLUDE_DIRS})

find_package(Boost REQUIRED COMPONENTS system)
add_library(ext_boost INTERFACE)
add_library(ext::boost ALIAS ext_boost)
target_include_directories(ext_boost INTERFACE ${Boost_INCLUDE_DIRS})
target_link_libraries(ext_boost INTERFACE ${Boost_LIBRARIES})

find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
