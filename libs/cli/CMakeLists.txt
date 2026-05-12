add_library(lib_cli STATIC
    src/repl.cpp
)

set_project_warnings(lib_core)
enable_project_sanitizers(lib_cli)
enable_clang_tidy(lib_cli)
enable_coverage(lib_cli)

target_include_directories(lib_cli
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lib_cli PUBLIC
    blankchat::network
    blankchat::protocol
)

add_library(blankchat::cli ALIAS lib_cli)
