FILESEXTRAPATHS:prepend := "${THISDIR}/linux-xlnx:"

SRC_URI += "file://ac880-uio.cfg"
SRC_URI += "file://ac880-drm.cfg"
SRC_URI += "file://0001-xlnx-pl-disp-dpi.patch"
SRC_URI += "file://0002-dynclk-6.18.patch"

# openamp.cfg in the BSP changes this symbol back to a module.  Keep the
# generic-U​​IO platform driver built in so /dev/uio0 is available at boot,
# before the tty1 LCD test starts.
do_kernel_configme:append() {
    if [ -f "${B}/.config" ]; then
        sed -i 's/^CONFIG_UIO_PDRV_GENIRQ=.*/CONFIG_UIO_PDRV_GENIRQ=y/' "${B}/.config"
        if ! grep -q '^CONFIG_UIO_PDRV_GENIRQ=y$' "${B}/.config"; then
            echo 'CONFIG_UIO_PDRV_GENIRQ=y' >> "${B}/.config"
        fi
        oe_runmake -C "${B}" olddefconfig
    fi
}
