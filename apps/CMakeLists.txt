option(BUILD_SERVER "Build the server application" ON)
option(BUILD_CLIENT "Build the client application" ON)

if(BUILD_SERVER)
    add_executable(blank_chat_server server/main.cpp)
    target_link_libraries(blank_chat_server PRIVATE
        blankchat::crypto
        ext::sodium
  )
    install(TARGETS blank_chat_server DESTINATION bin)

    set_project_warnings(blank_chat_server)
    enable_project_sanitizers(blank_chat_server)
    enable_clang_tidy(blank_chat_server)
    enable_coverage(blank_chat_server)
    apply_yocto_linker_flags(blank_chat_server)
endif()

if(BUILD_CLIENT)
    add_executable(blank_chat_client client/main.cpp)
    target_link_libraries(blank_chat_client PRIVATE
        blankchat::crypto
        ext::sodium
  )

    set_project_warnings(blank_chat_client)
    enable_project_sanitizers(blank_chat_client)
    enable_clang_tidy(blank_chat_client)
    enable_coverage(blank_chat_client)
    apply_yocto_linker_flags(blank_chat_client)
endif()
