inherit core-image

SUMMARY = "Bootable Client Image"
IMAGE_FSTYPES += "wic"
WKS_FILE = "blankchat-client.wks"

IMAGE_INSTALL += "packagegroup-core-boot"

IMAGE_FEATURES:remove = "read-only-rootfs"

IMAGE_INSTALL += " \
    blank-chat-client \
    libsodium \
    spdlog \
    blankchat-tor-config-client \
    libseccomp \
"

IMAGE_INSTALL += " \
    busybox \
    iproute2 \
    linux-firmware \
    tor \
"
