add_library(domain_server STATIC
    src/message_broker.cpp
)

set_project_warnings(domain_server)
enable_project_sanitizers(domain_server)
enable_clang_tidy(domain_server)
enable_coverage(domain_server)

target_link_libraries(domain_server PUBLIC
    blankchat::protocol
    simdjson::simdjson
    tomlplusplus::tomlplusplus
)

target_include_directories(domain_server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(blankchat::domain-server ALIAS domain_server)
