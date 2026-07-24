function(setup_format_target)
    find_program(CLANG_FORMAT_EXE NAMES "clang-format")
    if(CLANG_FORMAT_EXE)
        file(GLOB_RECURSE ALL_SOURCE_FILES CONFIGURE_DEPENDS
            "libs/*.cpp" "libs/*.h" "libs/*.hpp"
            "apps/*.cpp" "tests/*.cpp" "tests/*.h" "tests/*.hpp"
        )
        add_custom_target(format
            COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${ALL_SOURCE_FILES}
            COMMENT "Formatting all C++ files with clang-format..."
        )
    endif()
endfunction()

function(enable_clang_tidy target_name)
    if(ENABLE_CLANG_TIDY)
        find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
        if(CLANG_TIDY_EXE)
            set(CLANG_TIDY_ARGS
                "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
                "--header-filter=${CMAKE_SOURCE_DIR}/(libs|apps|tests)/.*"
            )

            if(DEFINED ENV{OECORE_TARGET_SYSROOT})
                set(YOCTO_SYSROOT "$ENV{OECORE_TARGET_SYSROOT}")
                file(GLOB YOCTO_CXX_DIRS "${YOCTO_SYSROOT}/usr/include/c++/*")
                if(YOCTO_CXX_DIRS)
                    list(GET YOCTO_CXX_DIRS 0 YOCTO_CXX_INC)
                    list(APPEND CLANG_TIDY_ARGS
                        "--extra-arg=-isystem${YOCTO_CXX_INC}"
                        "--extra-arg=-isystem${YOCTO_CXX_INC}/x86_64-poky-linux"
                    )
                endif()
            endif()

            set_target_properties(${target_name} PROPERTIES
                CXX_CLANG_TIDY "${CLANG_TIDY_EXE};${CLANG_TIDY_ARGS}"
            )
        endif()
    endif()
endfunction()
