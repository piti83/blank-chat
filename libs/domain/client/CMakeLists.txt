add_library(domain_client STATIC
    src/config.cpp
)

set_project_warnings(domain_client)
enable_project_sanitizers(domain_client)
enable_clang_tidy(domain_client)
enable_coverage(domain_client)

target_link_libraries(domain_client PUBLIC
    blankchat::protocol
    simdjson::simdjson
    tomlplusplus::tomlplusplus
)

target_include_directories(domain_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(blankchat::domain-client ALIAS domain_client)
