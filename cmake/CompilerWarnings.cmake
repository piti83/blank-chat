function(set_project_warnings target_name)
    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fstack-protector-all
    )

    if(NOT target_name STREQUAL "blank_chat_ut")
        target_compile_options(${target_name} PRIVATE
                -fno-exceptions
                -fno-unwind-tables
                -fno-rtti
            )

        target_compile_definitions(${target_name} PRIVATE
                SPDLOG_NO_EXCEPTIONS
                BOOST_NO_EXCEPTIONS
            )
    endif()

    if(BC_ENABLE_LOGS)
        target_compile_definitions(${target_name} PRIVATE
            SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
            BC_ENABLE_LOGS_ACTIVE=1
        )

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target_name} PRIVATE
                "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=."
            )
        endif()
    else()
        target_compile_definitions(${target_name} PRIVATE
            SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_OFF
            BC_ENABLE_LOGS_ACTIVE=0
        )
    endif()
endfunction()
