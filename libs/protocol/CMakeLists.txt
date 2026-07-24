add_library(protocol STATIC
    src/frame.cpp
    src/frame_parser.cpp
    src/mailbox_id.cpp
)

set_project_warnings(protocol)
enable_project_sanitizers(protocol)
enable_clang_tidy(protocol)
enable_coverage(protocol)

target_include_directories(protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(blankchat::protocol ALIAS protocol)
