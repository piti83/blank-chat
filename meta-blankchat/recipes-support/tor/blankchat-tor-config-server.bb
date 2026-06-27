SUMMARY = "Tor configuration for Blank Chat Server"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://torrc-server"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/tor

    install -m 0644 ${WORKDIR}/torrc-server ${D}${sysconfdir}/tor/torrc
}

FILES:${PN} = "${sysconfdir}/tor/torrc"
