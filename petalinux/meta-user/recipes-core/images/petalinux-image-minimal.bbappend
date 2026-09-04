# Include the AC880 capture utilities, DMA client and Qt startup service in
# the deployed Linux root filesystem.  The Qt service owns /dev/fb0 at boot;
# do not start the legacy ac880_capture_lcd tty1 viewer in parallel.
IMAGE_INSTALL:append = " ac880-capture ac880-capture-dma ac880-drm-test ac880-qt-runtime ac880-qt-startup"

# Ensure the DMA client is available before the Qt service starts, even on
# images where module autoload generation is disabled by the BSP.
ROOTFS_POSTPROCESS_COMMAND:append = " ac880_capture_module_autoload;"

ac880_capture_module_autoload() {
    if [ -e "${IMAGE_ROOTFS}${sysconfdir}/modules-load.d" ]; then
        printf '%s\n' ac880_capture_dma > "${IMAGE_ROOTFS}${sysconfdir}/modules-load.d/ac880_capture_dma.conf"
    else
        install -d "${IMAGE_ROOTFS}${sysconfdir}/modules-load.d"
        printf '%s\n' ac880_capture_dma > "${IMAGE_ROOTFS}${sysconfdir}/modules-load.d/ac880_capture_dma.conf"
    fi
}
