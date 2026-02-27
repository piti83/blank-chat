add_library(lib_crypto STATIC src/placeholder.cpp)

target_include_directories(lib_crypto
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lib_crypto PUBLIC spdlog::spdlog)
