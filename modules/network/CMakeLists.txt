add_library(lib_network STATIC src/placeholder.cpp)

target_include_directories(lib_network
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lib_network PUBLIC spdlog::spdlog)
