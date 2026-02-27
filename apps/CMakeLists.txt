add_executable(blank_chat_server server/main.cpp)
target_link_libraries(blank_chat_server PRIVATE 
    lib_protocol 
    lib_network 
    lib_crypto
    spdlog::spdlog
)

add_executable(blank_chat_client client/main.cpp)
target_link_libraries(blank_chat_client PRIVATE 
    lib_protocol 
    lib_network 
    lib_crypto
    spdlog::spdlog
)
