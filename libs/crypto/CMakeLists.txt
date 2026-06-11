add_library(crypto STATIC

)

set_project_warnings(crypto)
enable_project_sanitizers(crypto)
enable_clang_tidy(crypto)
enable_coverage(crypto)

target_include_directories(crypto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(crypto PUBLIC
    ext::sodium
)

add_library(blankchat::crypto ALIAS crypto)
