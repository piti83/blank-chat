inherit core-image

SUMMARY = "Bootable Server Image"

IMAGE_FSTYPES += "wic"

IMAGE_INSTALL += "packagegroup-core-boot"

IMAGE_FEATURES += "read-only-rootfs"

IMAGE_INSTALL += " \
    blank-chat-server \
    libsodium \
    spdlog \
"

IMAGE_INSTALL += " \
    volatile-binds \
    busybox \
    iproute2 \
    linux-firmware \
    tor \
    blankchat-tor-config-server \
"

TOOLCHAIN_TARGET_TASK:append = " \
    libasan-dev \
    libtsan-dev \
    libubsan-dev \
    valgrind \
    googletest-dev \
    boost-dev \
    simdjson-dev \
    simdjson-staticdev \
    tomlplusplus-dev \
"

VOLATILE_BINDS:append = " /var/volatile/blank-chat /etc/blank-chat\n"

mask_hibernation_services() {
    ln -sf /dev/null ${IMAGE_ROOTFS}/etc/systemd/system/systemd-hibernate.service
    ln -sf /dev/null ${IMAGE_ROOTFS}/etc/systemd/system/systemd-suspend.service
    ln -sf /dev/null ${IMAGE_ROOTFS}/etc/systemd/system/systemd-hybrid-sleep.service
}

ROOTFS_POSTPROCESS_COMMAND += "mask_hibernation_services; "
