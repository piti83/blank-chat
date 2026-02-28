add_library(lib_protocol STATIC 
  src/placeholder.cpp
  src/mailbox_id.cpp
)

target_include_directories(lib_protocol
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lib_protocol 
  PUBLIC 
    lib_utils
  PRIVATE
    spdlog::spdlog
)
