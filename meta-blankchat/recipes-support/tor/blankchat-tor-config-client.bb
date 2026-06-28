SUMMARY = "Tor configuration for Blank Chat Client"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://torrc-client"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/tor
    install -m 0644 ${WORKDIR}/torrc-client ${D}${sysconfdir}/tor/torrc.custom
}

FILES:${PN} = "${sysconfdir}/tor/torrc.custom"

pkg_postinst:${PN}() {
    #!/bin/sh
    if [ -n "$D" ]; then
        cp $D${sysconfdir}/tor/torrc.custom $D${sysconfdir}/tor/torrc
    else
        cp ${sysconfdir}/tor/torrc.custom ${sysconfdir}/tor/torrc
    fi
}
