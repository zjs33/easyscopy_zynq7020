SUMMARY = "AC880 ACM108 AXI DMA S2MM capture client"
DESCRIPTION = "Linux DMAengine client and character device for the AC880 capture stream"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = "file://Makefile \
           file://ac880_capture_dma_uapi.h \
           file://ac880_capture_dma.c \
          "

S = "${WORKDIR}"

KERNEL_MODULE_AUTOLOAD += "ac880_capture_dma"
