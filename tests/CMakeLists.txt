include(GoogleTest)

add_executable(blank_chat_ut
    gtest_main.cpp
    libs/core/logger_test.cpp
    libs/core/secure_buffer_test.cpp
    libs/domain/server/message_broker_test.cpp
    libs/protocol/frame_parser_test.cpp
    libs/protocol/frame_test.cpp
    libs/protocol/mailbox_id_test.cpp
)
target_link_libraries(blank_chat_ut PRIVATE
    GTest::gtest
    blankchat::core
    blankchat::protocol
    blankchat::domain-server
)
gtest_discover_tests(blank_chat_ut)

set_project_warnings(blank_chat_ut)
enable_project_sanitizers(blank_chat_ut)
enable_coverage(blank_chat_ut)
apply_yocto_linker_flags(blank_chat_ut)
