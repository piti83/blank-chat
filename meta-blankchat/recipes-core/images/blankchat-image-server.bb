inherit core-image

SUMMARY = "Bootable ISO Server Image (Standard Linux)"

IMAGE_FSTYPES += "iso"

IMAGE_INSTALL += "packagegroup-base"

IMAGE_INSTALL += " \
    blank-chat-server \
    libsodium \
    spdlog \
"

IMAGE_INSTALL += " \
    bash \
    coreutils \
    nano \
    iproute2 \
    net-tools \
    linux-firmware \
"

TOOLCHAIN_TARGET_TASK:append = " libasan-dev libtsan-dev libubsan-dev valgrind googletest-dev"
