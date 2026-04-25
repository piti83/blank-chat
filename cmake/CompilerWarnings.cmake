function(set_project_warnings target_name)
    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fstack-protector-all
    )

    if(BC_ENABLE_LOGS)
        target_compile_definitions(${target_name} PRIVATE BC_ENABLE_LOGS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target_name} PRIVATE
                "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=."
            )
        endif()
    endif()
endfunction()
