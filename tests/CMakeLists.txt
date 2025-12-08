include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES "*.cpp")

add_executable(unit_tests ${TEST_SOURCES})

target_link_libraries(unit_tests
    PRIVATE
    blank-chat-core
    GTest::gtest_main
    spdlog::spdlog
)

target_compile_features(unit_tests PRIVATE cxx_std_20)

gtest_discover_tests(unit_tests)
