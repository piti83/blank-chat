if(BUILD_SERVER)
    add_executable(blank_chat_server_tests gtest_main.cpp)
    target_link_libraries(blank_chat_server_tests PRIVATE
        GTest::gtest
        ext::sodium
        blankchat::crypto
    )
    add_test(NAME ServerTests COMMAND blank_chat_server_tests)

    set_project_warnings(blank_chat_server_tests)
    enable_project_sanitizers(blank_chat_server_tests)
    enable_coverage(blank_chat_server_tests)
    apply_yocto_linker_flags(blank_chat_server_tests)
endif()


if(BUILD_CLIENT)
    add_executable(blank_chat_client_tests gtest_main.cpp)
    target_link_libraries(blank_chat_client_tests PRIVATE
        GTest::gtest
        ext::sodium
        blankchat::crypto
    )
    add_test(NAME ClientTests COMMAND blank_chat_client_tests)
    set_project_warnings(blank_chat_client_tests)
    enable_project_sanitizers(blank_chat_client_tests)
    enable_coverage(blank_chat_client_tests)
    apply_yocto_linker_flags(blank_chat_client_tests)
endif()
