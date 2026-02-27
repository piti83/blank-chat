find_package(GTest CONFIG REQUIRED)

add_executable(blank_chat_unit_tests gtest_main.cpp)
target_link_libraries(blank_chat_unit_tests PRIVATE 
    GTest::gtest_main
    lib_protocol
    lib_crypto
    lib_network
)

add_test(NAME AllTests COMMAND blank_chat_unit_tests)
