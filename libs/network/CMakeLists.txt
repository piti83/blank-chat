add_library(lib_network STATIC
    src/tcp_session.cpp
)

set_project_warnings(lib_network)
enable_project_sanitizers(lib_network)
enable_clang_tidy(lib_network)
enable_coverage(lib_network)

target_include_directories(lib_network
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lib_network
    PUBLIC
        ext::boost
        blankchat::protocol
    PRIVATE
        blankchat::core
)

add_library(blankchat::network ALIAS lib_network)
