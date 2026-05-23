add_library(cli STATIC
    src/repl.cpp
)

set_project_warnings(cli)
enable_project_sanitizers(cli)
enable_clang_tidy(cli)
enable_coverage(cli)

target_include_directories(cli
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cli PUBLIC
    blankchat::network
    blankchat::protocol
)

add_library(blankchat::cli ALIAS cli)
