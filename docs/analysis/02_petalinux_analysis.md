# 02 PetaLinux 分析

## 工程

`petalinux_scope_acm108_2026_1` 是从 Vivado 2026.1 XSA 派生的新工程。工程中的 `project-spec/meta-user` 是持久化修改入口；`build/`、自动生成 DTS 和 Yocto 临时目录不作为源码。

## 配置层

- `linux-xlnx_%.bbappend` 引入 DRM、UIO 配置和动态时钟/PL display patch。
- `ac880-drm.cfg` 打开 Xilinx DRM PL display、VTC bridge、DRM fbdev emulation、panel-dpi 相关配置。
- `ac880-uio.cfg` 与 bbappend 确保 `uio_pdrv_genirq` 可用。
- image bbappend 安装 `ac880-capture`、`ac880-capture-dma`、`ac880-drm-test`，并配置 tty1 登录测试脚本。

## Device Tree

持久化文件：`project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi`。其中确认：

- `axi_dma_capture` 为 DMA 节点；
- `capture_subsystem` 使用 `generic-uio` 作为控制寄存器访问入口；
- `ac880_capture_dma_client` 使用 `dmas = <&axi_dma_capture 1>`；
- `pl-disp` 使用 VDMA 通道 0 和 VTC bridge；
- `panel-dpi` 固定 800×480 RGB565 timing；
- `v_tc_0` 使用 PS 时钟与动态 pixel clock。

最终生成的 `pl.dtsi/system.dts` 由工具产生，不能长期手工修改。当前 Windows 工作区没有完整生成缓存，因此最终标签/IRQ 编号需在 Ubuntu 构建后以反编译 DTB 为准。

## 构建流程

```bash
source /home/jason/petalinux/2026.1/settings.sh
cd <PETA_PROJECT>
petalinux-config --get-hw-description <XSA_PATH> --silentconfig
petalinux-config --silentconfig
petalinux-build -c device-tree
petalinux-build
petalinux-package boot \
  --fsbl images/linux/zynq_fsbl.elf \
  --fpga images/linux/system.bit \
  --u-boot images/linux/u-boot.elf \
  --output images/linux/BOOT.BIN
```

`BOOT.BIN` 必须包含 FSBL、最新 `system.bit`、U-Boot，必要时包含 DTB。启动文件和 XSA/bitstream 的时间与哈希需一并记录。

## 启动存储

当前实际方案是 SD FAT BOOT 分区放 `BOOT.BIN`、`image.ub`、`boot.scr`，ext4 rootfs 分区挂载为 `/dev/mmcblk0p2`。不要把“SD 上有文件”误认为“文件运行在 SD”；内核、驱动、Qt 和 DMA 缓冲都进入 PS DDR。
