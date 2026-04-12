find_package(GTest CONFIG REQUIRED)

add_executable(blank_chat_unit_tests 
  gtest_main.cpp
  mailbox_id_test.cpp
  uuid_factory_test.cpp
  logger_test.cpp
)

target_link_libraries(blank_chat_unit_tests PRIVATE 
    GTest::gtest_main
    unofficial-sodium::sodium
    lib_protocol
    lib_crypto
    lib_network
    lib_utils
)

add_test(NAME AllTests COMMAND blank_chat_unit_tests)
