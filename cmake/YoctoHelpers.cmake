function(apply_yocto_linker_flags target_name)
    if(DEFINED ENV{OECORE_TARGET_SYSROOT})
        set(YOCTO_SYSROOT "$ENV{OECORE_TARGET_SYSROOT}")
        file(GLOB LD_LINUX
            "${YOCTO_SYSROOT}/lib/ld-linux-x86-64.so.2" "${YOCTO_SYSROOT}/lib64/ld-linux-x86-64.so.2"
        )
        if(LD_LINUX)
            list(GET LD_LINUX 0 LD_LINUX_PATH)
            target_link_options(${target_name} PRIVATE
                "-Wl,-dynamic-linker=${LD_LINUX_PATH}"
                "-Wl,-rpath=${YOCTO_SYSROOT}/lib:${YOCTO_SYSROOT}/usr/lib"
            )
        endif()
    endif()
endfunction()
