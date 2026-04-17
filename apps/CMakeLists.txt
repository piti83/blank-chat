add_executable(blank_chat_server server/main.cpp)
target_link_libraries(blank_chat_server PRIVATE
    #blankchat::domain_server
    #blankchat::network
    #blankchat::protocol
    blankchat::crypto
    #blankchat::core
    unofficial-sodium::sodium

)

add_executable(blank_chat_client client/main.cpp)
target_link_libraries(blank_chat_client PRIVATE
    #blankchat::domain_client
    #blankchat::network
    #blankchat::protocol
    blankchat::crypto
    #blankchat::core
    unofficial-sodium::sodium
)
