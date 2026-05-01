add_library(protocol_frame STATIC
    src/frame.cpp
    src/mailbox_id.cpp
)

set_project_warnings(protocol_frame)
enable_project_sanitizers(protocol_frame)
enable_clang_tidy(protocol_frame)
enable_coverage(protocol_frame)

target_include_directories(protocol_frame
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
