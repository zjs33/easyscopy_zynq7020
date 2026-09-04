SUMMARY = "AC880 DRM/KMS RGB565 display test"
DESCRIPTION = "Userspace DRM dumb-buffer test for the AC880 800x480 LCD"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "libdrm"
SRC_URI = "file://ac880_drm_test.c"
S = "${WORKDIR}"

do_compile() {
        ${CC} ${CFLAGS} ${LDFLAGS} -o ac880_drm_test ac880_drm_test.c
}

do_install() {
        install -d ${D}${bindir}
        install -m 0755 ac880_drm_test ${D}${bindir}/ac880_drm_test
}

FILES:${PN} += "${bindir}/ac880_drm_test"
