add_library(lib_utils STATIC 
  src/uuid_factory.cpp
  src/logger.cpp
)

target_include_directories(lib_utils
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lib_utils PUBLIC 
  spdlog::spdlog 
  unofficial-sodium::sodium
)
