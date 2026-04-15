add_subdirectory(primitives)

add_library(lib_crypto INTERFACE)

target_link_libraries(lib_crypto INTERFACE
    crypto_primitives
)

add_library(blankchat::crypto ALIAS lib_crypto)
