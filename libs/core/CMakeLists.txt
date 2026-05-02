add_library(lib_core STATIC
    src/logger.cpp
)

set_project_warnings(lib_core)
enable_project_sanitizers(lib_core)
enable_clang_tidy(lib_core)
enable_coverage(lib_core)

target_include_directories(lib_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lib_core PUBLIC
    spdlog::spdlog
)

add_library(blankchat::core ALIAS lib_core)
