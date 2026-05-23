add_library(network STATIC
    src/tcp_client.cpp
    src/tcp_server.cpp
    src/tcp_session.cpp
)

set_project_warnings(network)
enable_project_sanitizers(network)
enable_clang_tidy(network)
enable_coverage(network)

target_include_directories(network
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(network
    PUBLIC
        ext::boost
        blankchat::protocol
    PRIVATE
        blankchat::core
)

add_library(blankchat::network ALIAS network)
