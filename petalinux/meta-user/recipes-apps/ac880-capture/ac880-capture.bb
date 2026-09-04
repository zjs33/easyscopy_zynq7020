SUMMARY = "AC880 ACM108 capture register bring-up utility"
DESCRIPTION = "Read and control the AC880 ACM108 AXI-Lite capture block through UIO."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ac880_capture_ctl.c file://ac880_capture_lcd.c file://ac880_dma_mmap_test.c file://ac880_link_test.sh file://ac880-link-test.init"

S = "${WORKDIR}"

do_compile() {
        ${CC} ${CFLAGS} ${LDFLAGS} ac880_capture_ctl.c -o ac880_capture_ctl
        ${CC} ${CFLAGS} ${LDFLAGS} ac880_capture_lcd.c -o ac880_capture_lcd
        ${CC} ${CFLAGS} ${LDFLAGS} ac880_dma_mmap_test.c -o ac880_dma_mmap_test
}

do_install() {
        install -d ${D}${bindir}
        install -m 0755 ac880_capture_ctl ${D}${bindir}/ac880_capture_ctl
        install -m 0755 ac880_capture_lcd ${D}${bindir}/ac880_capture_lcd
        install -m 0755 ac880_dma_mmap_test ${D}${bindir}/ac880_dma_mmap_test
        install -m 0755 ac880_link_test.sh ${D}${bindir}/ac880_link_test.sh
        install -d ${D}${sysconfdir}/init.d
        install -m 0755 ac880-link-test.init ${D}${sysconfdir}/init.d/S99ac880-link-test
}
