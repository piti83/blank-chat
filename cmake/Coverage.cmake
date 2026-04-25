function(enable_coverage target_name)
    if(USE_COVERAGE)
        target_compile_options(${target_name} PRIVATE --coverage -O0 -g -U_FORTIFY_SOURCE)
        target_link_options(${target_name} PRIVATE --coverage)
    endif()
endfunction()

function(setup_coverage_target)
    if(USE_COVERAGE)
        find_program(GCOVR_EXE NAMES gcovr)
        if(DEFINED ENV{CROSS_COMPILE})
            set(GCOV_CMD "$ENV{CROSS_COMPILE}gcov")
        else()
            set(GCOV_CMD "gcov")
        endif()

        if(GCOVR_EXE)
            add_custom_target(coverage
                COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage_report
                COMMAND ${GCOVR_EXE}
                    --root ${CMAKE_SOURCE_DIR}
                    --filter "${CMAKE_SOURCE_DIR}/libs"
                    --exclude-unreachable-branches
                    --txt-metric branch
                    --html-details ${CMAKE_BINARY_DIR}/coverage_report/index.html
                    --print-summary
                    --gcov-executable ${GCOV_CMD}
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Running tests and generating HTML coverage report using ${GCOV_CMD}..."
            )
        else()
            message(WARNING "gcovr not found! 'coverage' target will not be created.")
        endif()
    endif()
endfunction()
