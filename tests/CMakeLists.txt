find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(blank_chat_ut
    gtest_main.cpp
    libs/cli/repl_test.cpp
    libs/core/boost_exception_handler_test.cpp
    libs/core/logger_test.cpp
    libs/core/secure_buffer_test.cpp
    libs/crypto/identity_key_test.cpp
    libs/crypto/mailbox_derivation_test.cpp
    libs/domain/client/address_book_test.cpp
    libs/domain/client/config_test.cpp
    libs/domain/client/contact_storage_test.cpp
    libs/domain/server/config_test.cpp
    libs/domain/server/message_broker_test.cpp
    libs/network/tcp_client_test.cpp
    libs/network/tcp_server_test.cpp
    libs/network/tcp_session_test.cpp
    libs/protocol/frame_parser_test.cpp
    libs/protocol/frame_test.cpp
    libs/protocol/mailbox_id_test.cpp
)

target_link_libraries(blank_chat_ut PRIVATE
    GTest::gtest
    blankchat::core
    blankchat::crypto
    blankchat::protocol
    blankchat::network
    blankchat::domain-server
    blankchat::domain-client
    blankchat::cli
)

target_compile_definitions(blank_chat_ut PRIVATE
    GTEST_HAS_RTTI=0
    GTEST_HAS_EXCEPTIONS=0
)

gtest_discover_tests(blank_chat_ut)

set_project_warnings(blank_chat_ut)
enable_project_sanitizers(blank_chat_ut)
enable_coverage(blank_chat_ut)
apply_yocto_linker_flags(blank_chat_ut)
