# 06 依赖清单

## FPGA

- Vivado 2026.1
- Xilinx Zynq-7000 PS7、AXI DMA 7.1、AXI VDMA 6.3、VTC 6.2、SmartConnect、xlconcat
- 自定义 IP：`rgb565to888`、`rgb2lcd`、`axi_dynclk`
- XPM CDC/FIFO 原语

## PetaLinux/Linux

- PetaLinux 2026.1
- Xilinx Linux 6.18 系列（以当前构建实际版本为准）
- Yocto/BitBake、device-tree-compiler、Bootgen
- 内核：DRM Xilinx、VTC bridge、xilinx-vdma、DMAengine、UIO、Goodix 输入驱动、framebuffer 兼容层
- 用户态：BusyBox 基础命令，工程自带 `ac880_capture_ctl`、`ac880_capture_lcd`、`ac880_dma_mmap_test`、`ac880_drm_test`

## Qt

- Qt 5.12.8（Zynq Linux 目标）
- CMake、C++14 编译器、ARM Linux 交叉工具链
- Qt Core/Gui/Widgets/Network
- linuxfb 或 eglfs/DRM-KMS QPA（由启动环境决定）
- `NotoSansCJKsc-Regular.otf`：目标板中文字体，当前部署需单独确认

## 依赖边界

不强制依赖 fontconfig；不使用 Vitis 作为当前 V1 的构建工具。Vitis 只在后续需要裸机/驱动 BSP 时再评估。
