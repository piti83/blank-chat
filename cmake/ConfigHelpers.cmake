function(configure_and_install_config template_path output_filename)
    configure_file(
        ${template_path}
        ${CMAKE_CURRENT_BINARY_DIR}/${output_filename}
        @ONLY
    )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${output_filename}
            DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/blank-chat)
endfunction()
