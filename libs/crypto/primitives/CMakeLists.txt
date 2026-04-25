add_library(crypto_primitives STATIC
    src/primitives.cpp
)

set_project_warnings(crypto_primitives)
enable_project_sanitizers(crypto_primitives)
enable_clang_tidy(crypto_primitives)
enable_coverage(crypto_primitives)

target_include_directories(crypto_primitives
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(crypto_primitives PUBLIC
    ext::sodium
)
