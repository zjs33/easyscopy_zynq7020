# 00 工程清单

## 扫描范围

扫描根目录：`D:\\zynqstudy\\1linux`。已排除 Vivado/Yocto 自动生成的缓存、运行目录和 `.Xil`，避免把生成文件误当作设计源文件。

## 当前工程组成

| 层 | 目录 | 状态 |
|---|---|---|
| Vivado 采集/显示 | `fpga/ac880_scope_acm108_2026_1` | 【已实现并实际使用】 |
| PetaLinux 功能层 | `petalinux_scope_acm108_2026_1` | 【已实现并实际使用】 |
| Qt 上位机/板端程序 | `_qt_scope_ref_20260903_v2` | 【已实现并实际使用】 |
| PL 侧独立 DDR3/UART 参考 | `fpga/ac880_acm108_ddr3_uart` | 【已有设计但当前未启用】（用于迁移、仿真和资源参考） |
| 基础 BSP 归档 | `archives/ac880_linux_bsp_2026_1_baseline_20260828` | 【已有设计但当前未启用】（只读归档） |

## 重要源文件

### Vivado

- `fpga/ac880_scope_acm108_2026_1/zynq_Linux.srcs/sources_1/bd/system/system.bd`
- `fpga/ac880_scope_acm108_2026_1/scope_src/rtl/ac880_capture_subsystem.sv`
- `scope_src/rtl/ac880_capture_ctrl_axi.sv`
- `scope_src/rtl/ac880_sample_engine.sv`
- `scope_src/rtl/ac880_axis_packer.sv`
- `scope_src/rtl/ac880_acm108_io.sv`
- `scope_src/rtl/ac880_dac_test_source.sv`
- `scope_src/constraints/acm108_scope.xdc`
- `scope_src/export/ac880_scope_acm108_2026_1.xsa`
- `scope_src/export/ac880_scope_acm108_2026_1.bit`

### PetaLinux

- `project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi`
- `recipes-kernel/linux/linux-xlnx_%.bbappend`
- `recipes-kernel/linux/linux-xlnx/ac880-drm.cfg`
- `recipes-kernel/linux/linux-xlnx/ac880-uio.cfg`
- `recipes-modules/ac880-capture-dma/files/ac880_capture_dma.c`
- `recipes-modules/ac880-capture-dma/files/ac880_capture_dma_uapi.h`
- `recipes-apps/ac880-capture/files/ac880_capture_ctl.c`
- `recipes-apps/ac880-capture/files/ac880_capture_lcd.c`

### Qt

- `src/main.cpp`
- `src/ui/mainwindow.cpp/.h`
- `src/acquisition/dmacapturedatasource.cpp/.h`
- `src/plotting/waveformwidget.cpp/.h`
- `src/plotting/sampleringbuffer.cpp/.h`
- `src/processing/fftworker.cpp/.h`
- `src/storage/waveformfilecodec.cpp/.h`

## 产物

当前已归档的 PetaLinux 产物在 `petalinux_output/ac880_scope_acm108_2026_1/linux`，包括 `BOOT.BIN`、`image.ub`、`system.dtb`、`system.bit`、`boot.scr` 和 rootfs 压缩包。Vivado 端已导出含 bitstream 的 XSA。

## 结论

当前 V1 是“Vivado PL 采样 + AXI DMA 写 PS DDR + Linux DMAengine 客户端 + Qt 显示”的工程，不是早期“PL MIG 直接作为 Linux 内存”的方案。
