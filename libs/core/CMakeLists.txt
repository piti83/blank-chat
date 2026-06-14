add_library(core STATIC
    src/boost_exception_handler.cpp
    src/logger.cpp
    src/secure_buffer.cpp
    src/secure_string.cpp
)

set_project_warnings(core)
enable_project_sanitizers(core)
enable_clang_tidy(core)
enable_coverage(core)

target_include_directories(core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(core PUBLIC
    spdlog::spdlog
    ext::sodium
)

add_library(blankchat::core ALIAS core)
