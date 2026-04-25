inherit core-image

SUMMARY = "Bootable ISO Client Image (Standard Linux)"

IMAGE_FSTYPES += "iso"

IMAGE_INSTALL += "packagegroup-base"

IMAGE_INSTALL += " \
    blank-chat-client \
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
