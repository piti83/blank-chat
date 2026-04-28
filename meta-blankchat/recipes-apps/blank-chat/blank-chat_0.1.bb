SUMMARY = "Blank Chat Application"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://LICENSE;md5=81525c65625e3ded655268549f061212"

SRC_URI = "git:///home/piti83/Dev/cpp/blank-chat;protocol=file;branch=main"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

DEPENDS = "libsodium spdlog googletest boost"

inherit cmake pkgconfig

EXTRA_OECMAKE = "-DBUILD_TESTING=OFF -DBC_ENABLE_LOGS=ON -DBUILD_CLIENT=ON -DBUILD_SERVER=ON"

PACKAGES =+ "${PN}-server ${PN}-client"

FILES:${PN}-server = "${bindir}/blank_chat_server"
FILES:${PN}-client = "${bindir}/blank_chat_client"
