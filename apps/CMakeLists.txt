option(BUILD_SERVER "Build the server application" ON)
option(BUILD_CLIENT "Build the client application" ON)

if(BUILD_SERVER)
  add_executable(blank_chat_server server/main.cpp)
  target_link_libraries(blank_chat_server PRIVATE
        blankchat::crypto
        ext::sodium
    )
  install(TARGETS blank_chat_server DESTINATION bin)
endif()

if(BUILD_CLIENT)
  add_executable(blank_chat_client client/main.cpp)
  target_link_libraries(blank_chat_client PRIVATE
        blankchat::crypto
        ext::sodium
    )
endif()
