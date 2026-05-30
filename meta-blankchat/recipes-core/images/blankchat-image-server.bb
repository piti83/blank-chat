inherit core-image

SUMMARY = "Bootable Server Image"

IMAGE_FSTYPES += "wic"

IMAGE_INSTALL += "packagegroup-base"

IMAGE_FEATURES:remove = "read-only-rootfs"
IMAGE_FEATURES += "allow-empty-password empty-root-password"

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
    tor \
"

TOOLCHAIN_TARGET_TASK:append = " \
    libasan-dev \
    libtsan-dev \
    libubsan-dev \
    valgrind \
    googletest-dev \
    boost-dev \
    simdjson-dev \
    tomlplusplus-dev \
"
