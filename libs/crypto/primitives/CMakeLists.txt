add_library(crypto_primitives STATIC
    src/primitives.cpp
)

target_include_directories(crypto_primitives
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(crypto_primitives PUBLIC
    ext::sodium
)
