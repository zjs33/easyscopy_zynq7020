SUMMARY = "AC880 Qt oscilloscope boot startup"
DESCRIPTION = "Start the deployed AC880 Qt oscilloscope on the LCD at boot."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ac880-qt.init"

S = "${WORKDIR}"

do_install() {
        install -d ${D}${sysconfdir}/init.d
        install -m 0755 ac880-qt.init ${D}${sysconfdir}/init.d/S99ac880-qt
        install -d ${D}${sysconfdir}/rc5.d
        ln -sf ../init.d/S99ac880-qt ${D}${sysconfdir}/rc5.d/S99ac880-qt
}

FILES:${PN} += "${sysconfdir}/init.d/S99ac880-qt ${sysconfdir}/rc5.d/S99ac880-qt"
RDEPENDS:${PN} += "ac880-capture ac880-capture-dma ac880-qt-runtime"
