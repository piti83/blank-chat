add_subdirectory(frame)

add_library(lib_protocol INTERFACE)

target_link_libraries(lib_protocol INTERFACE
    protocol_frame
)

add_library(blankchat::protocol ALIAS lib_protocol)
