inherit core-image

SUMMARY = "Bootable ISO Client Image (Standard Linux)"

IMAGE_FSTYPES += "wic"

IMAGE_INSTALL += "packagegroup-base"

IMAGE_FEATURES:remove = "read-only-rootfs"
IMAGE_FEATURES += "allow-empty-password empty-root-password"

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
    tor \
"
