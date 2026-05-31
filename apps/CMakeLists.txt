option(BUILD_SERVER "Build the server application" ON)
option(BUILD_CLIENT "Build the client application" ON)

if(BUILD_SERVER)
    add_executable(blank_chat_server server/main.cpp)
    target_link_libraries(blank_chat_server PRIVATE
        blankchat::core
        blankchat::protocol
        blankchat::network
        blankchat::domain-server
        ext::sodium
        ext::boost
    )

    configure_and_install_config(
        ${CMAKE_CURRENT_SOURCE_DIR}/server/bc_server_config.toml.in
        bc_server_config.toml
    )

    install(TARGETS blank_chat_server DESTINATION ${CMAKE_INSTALL_BINDIR})

    set_project_warnings(blank_chat_server)
    enable_project_sanitizers(blank_chat_server)
    enable_clang_tidy(blank_chat_server)
    enable_coverage(blank_chat_server)
    apply_yocto_linker_flags(blank_chat_server)
endif()

if(BUILD_CLIENT)
    add_executable(blank_chat_client client/main.cpp)
    target_link_libraries(blank_chat_client PRIVATE
        blankchat::core
        blankchat::protocol
        blankchat::cli
        ext::sodium
    )

    configure_and_install_config(
        ${CMAKE_CURRENT_SOURCE_DIR}/client/bc_client_config.toml.in
        client_config.toml
    )

    install(TARGETS blank_chat_client DESTINATION ${CMAKE_INSTALL_BINDIR})

    set_project_warnings(blank_chat_client)
    enable_project_sanitizers(blank_chat_client)
    enable_clang_tidy(blank_chat_client)
    enable_coverage(blank_chat_client)
    apply_yocto_linker_flags(blank_chat_client)
endif()
