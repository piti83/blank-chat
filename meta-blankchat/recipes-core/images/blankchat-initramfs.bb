SUMMARY = "Initramfs image for unlocking LUKS"
LICENSE = "MIT"

inherit image

IMAGE_NAME_SUFFIX = ""

INITRAMFS_IMAGE = ""
INITRAMFS_IMAGE_BUNDLE = ""

IMAGE_FSTYPES = "cpio.gz"
IMAGE_FEATURES = ""

IMAGE_INSTALL = " \
    busybox \
    cryptsetup \
    util-linux-blkid \
    e2fsprogs \
    initramfs-crypt \
"

export IMAGE_BASENAME = "blankchat-initramfs"
