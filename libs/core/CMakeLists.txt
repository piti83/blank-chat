add_subdirectory(logger)

add_library(lib_core INTERFACE)

target_link_libraries(lib_core INTERFACE
    core_logger
)

add_library(blankchat::core ALIAS lib_core)
