inherit core-image

SUMMARY = "Minimal Yocto image for blank-chat project"

IMAGE_INSTALL += " \
    blank-chat \
    libsodium \
    spdlog \
"

TOOLCHAIN_TARGET_TASK += "libsodium-dev spdlog-dev googletest-dev"
