SUMMARY = "AC880 Qt 5 oscilloscope runtime"
DESCRIPTION = "Deploy the ARM Qt oscilloscope binary, Qt libraries, plugins and application font."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ac880_qt_deploy.tar.gz;unpack=0 \
           file://NotoSansCJKsc-Regular.otf"

S = "${WORKDIR}"

do_install() {
        install -d ${D}/opt/ac880-qt
        tar -xzf ${WORKDIR}/ac880_qt_deploy.tar.gz -C ${D}/opt/ac880-qt
        install -d ${D}/opt/ac880-qt/fonts
        install -m 0644 ${WORKDIR}/NotoSansCJKsc-Regular.otf \
                ${D}/opt/ac880-qt/fonts/NotoSansCJKsc-Regular.otf
        chown -R root:root ${D}/opt/ac880-qt
}

FILES:${PN} += "/opt/ac880-qt"

# The deployment tree contains its own private Qt/libstdc++ runtime under
# /opt/ac880-qt/lib.  Keep these private copies out of Yocto's global shlib
# provider index and allow the already-stripped release binaries.
EXCLUDE_FROM_SHLIBS = "1"
INSANE_SKIP:${PN} += "already-stripped 32bit-time file-rdeps dev-so ldflags"
