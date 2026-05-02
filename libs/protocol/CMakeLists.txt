add_library(lib_protocol STATIC
    src/frame.cpp
    src/mailbox_id.cpp
)

set_project_warnings(lib_protocol)
enable_project_sanitizers(lib_protocol)
enable_clang_tidy(lib_protocol)
enable_coverage(lib_protocol)

target_include_directories(lib_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(blankchat::protocol ALIAS lib_protocol)
