SUMMARY = "Blank Chat Application"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://LICENSE;md5=81525c65625e3ded655268549f061212"

S = "${WORKDIR}/git"

DEPENDS = "libsodium spdlog boost simdjson tomlplusplus"

inherit cmake pkgconfig

EXTRA_OECMAKE = "-DBUILD_TESTING=OFF -DBC_ENABLE_LOGS=OFF -DBUILD_CLIENT=ON -DBUILD_SERVER=ON"

PACKAGES =+ "${PN}-server ${PN}-client"

FILES:${PN}-server = " \
    ${bindir}/blank_chat_server \
    ${sysconfdir}/blank-chat/server_config.toml \
"

FILES:${PN}-client = " \
    ${bindir}/blank_chat_client \
    ${sysconfdir}/blank-chat/client_config.toml \
"
