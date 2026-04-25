add_library(core_logger STATIC
    src/logger.cpp
)

set_project_warnings(core_logger)
enable_project_sanitizers(core_logger)
enable_clang_tidy(core_logger)
enable_coverage(core_logger)

target_include_directories(core_logger
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(core_logger PUBLIC
    spdlog::spdlog
)
