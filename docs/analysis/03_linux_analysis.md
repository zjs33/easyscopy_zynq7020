# 03 Linux 底层分析

## 驱动与设备

| 接口 | 来源 | 用途 | 状态 |
|---|---|---|---|
| `/dev/uio0` | `generic-uio` + `capture_subsystem` | AXI-Lite 控制/状态寄存器 | 【已实现并实际使用】 |
| `/dev/ac880_capture_dma` | `ac880_capture_dma.c` 字符驱动 | DMAengine S2MM 环形缓冲、poll/read/mmap/ioctl | 【已实现并实际使用】 |
| `/dev/dri/card0` | Xilinx DRM/KMS PL display | LCD 显示设备 | 【已实现并实际使用】 |
| `/dev/fb0` | DRM fbdev emulation | 兼容 framebuffer 验证 | 【已实现并实际使用】 |
| `/dev/input/event0` | Goodix GT911 内核驱动 | 触摸输入 | 【已实现并实际使用】 |

## DMA 客户端实现

`ac880_capture_dma.c` 通过 `dma_request_chan("rx")` 请求 S2MM 通道，分配 `4 * 1 MiB = 4 MiB` coherent 缓冲，调用 `dmaengine_prep_dma_cyclic()` 启动循环传输。每完成一个 period，回调递增 `period_seq` 并唤醒 waitqueue。

UAPI 位于 `ac880_capture_dma_uapi.h`，包括 `START`、`STOP`、`GET_SEQ`、`GET_INFO`、`WAIT_SEQ`。`mmap()` 使用 `dma_mmap_coherent()`，不把固定物理 DDR 地址暴露给 Qt。

## 数据格式

- PL 采样输入：单通道 AD0，8-bit 无符号 code。
- AXI-Stream：8-bit FIFO 样本由 `ac880_axis_packer` 组装为 64-bit beat，TLAST/TKEEP 表示块边界。
- DMA 用户接口：按 1 MiB period 返回原始 8-bit 样本。
- Qt：转换为 `float` 电压值，默认 `(code - 128) * voltsPerCode`。

## 地址表

| 模块 | Vivado 地址 | Device Tree/标签 | Linux 使用 |
|---|---:|---|---|
| AXI DMA | `0x40400000`, 64 KiB | `axi_dma_capture` | DMAengine，不直接 mmap 寄存器 |
| AXI dynamic clock | `0x43C00000` | `axi_dynclk_0` | DRM/VTC clock provider |
| VDMA display | `0x43000000` | `axi_vdma_0` | xilinx-vdma / DRM display |
| VTC | `0x43C10000` | `v_tc_0` | VTC DRM bridge |
| Capture AXI-Lite | `0x43C20000` | `capture_subsystem` | UIO `/dev/uio0` |

这些地址由 `system.bd` 的 address-space 条目确认。DMA 缓冲地址是运行时由 DMA API 分配的，不是上表中的寄存器地址。

## 内存/缓存边界

PS DDR 是 Linux 主内存；DMA coherent ring 在 DDR 中。CPU L1/L2 cache 不应与普通 DMA 地址直接混用，驱动通过 DMA API 管理一致性。当前采集不使用 PS OCM 作为大缓存，也不使用 PL 侧 MIG DDR 作为 Linux 采集内存。
