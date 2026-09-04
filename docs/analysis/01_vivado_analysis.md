# 01 Vivado 分析

## 工具与器件

- 工具：Vivado 2026.1（工程入口和工作流文档均指定此版本）。
- 器件：`xc7z020clg484-2`。
- 顶层 Block Design：`system`，HDL wrapper 由 Vivado 生成。
- XSA：`scope_src/export/ac880_scope_acm108_2026_1.xsa`，旁边存在同名 bitstream，标记为【已实现并实际使用】。

## 实际 BD 拓扑

```text
ACM108 AD0[7:0]
  -> acm108_io
  -> ac880_capture_subsystem
       S_AXI_CTRL <- PS M_AXI_GP0
       M_AXIS -> axi_dma_capture S_AXIS_S2MM
  -> AXI DMA S2MM/SG (0x40400000)
  -> capture_hp1_smartconnect
  -> PS S_AXI_HP1
  -> PS DDR

PS M_AXI_GP0 -> axi_dma_capture / capture_subsystem / axi_vdma_0 / v_tc_0 / axi_dynclk_0

PS S_AXI_HP0 <- axi_vdma_0 MM2S
axi_vdma_0 -> rgb565to888_0 -> rgb2lcd_0 -> LCD
v_tc_0 -> 视频时序；axi_dynclk_0 -> LCD pixel clock
```

证据来自 `ac880_linux_bsp.srcs/sources_1/bd/system/system.bd` 中的 cell、connection 和 address-space 条目。

## 采集模块

| 模块 | 文件/IP | 功能 | 状态 |
|---|---|---|---|
| `acm108_io` | `ac880_acm108_io.sv` | IOB 寄存器采样 AD0，并用 ODDR 转发 AD0_CLK | 【已实现并实际使用】 |
| `capture_subsystem` | `ac880_capture_subsystem.sv` | AXI-Lite 配置、CDC、采样引擎、异步 FIFO、AXI-Stream 打包 | 【已实现并实际使用】 |
| `ac880_sample_engine` | 同名 RTL | 连续采样、触发状态机、预触发/后触发、内部测试源、溢出统计 | 【已实现并实际使用】（连续模式已验证；触发帧的端到端可靠性仍需单独验收） |
| `ac880_axis_packer` | 同名 RTL | 8-bit FIFO 样本组装为 64-bit AXI-Stream，使用 TKEEP/TLAST | 【已实现并实际使用】 |
| `axi_dma_capture` | AXI DMA 7.1 | S2MM、SG、64-bit 数据面、IRQ | 【已实现并实际使用】 |
| `capture_hp1_smartconnect` | SmartConnect | 将 DMA S2MM/SG 接到 PS HP1 | 【已实现并实际使用】 |

## 显示模块

`axi_vdma_0` 为 MM2S，经过自定义 `rgb565to888_0`、`rgb2lcd_0` 输出 800×480 RGB LCD；`v_tc_0` 负责视频时序，`axi_dynclk_0` 负责像素时钟。Linux 通过 `xlnx,pl-disp`/VTC DRM 绑定生成 `/dev/dri/card0`，同时保留 fbdev 兼容层。

## DAC

`ac880_dac_test_source.sv` 存在并在 BD 中有 `dac_test_source`、`rst_dac_125m` 相关层次；它是 125 MHz DDS 测试源，不是当前 ADC->Linux->Qt 数据链路的一部分。DAC 管脚约束存在于 PL 参考工程；当前 V1 不宣称已完成板级 DAC 模拟输出验收，状态为【已有设计但当前未启用】。

## 时钟/复位

- PS FCLK0 连接 AXI-Lite、DMA、SmartConnect、采集控制/AXIS 时钟域。
- 采集前端使用 50 MHz 域；ADC 输入在 `acm108_io` 中寄存。
- 视频像素时钟由动态时钟 IP 输出。
- 采集模块内部使用 XPM CDC/async FIFO，不用宽泛 clock-group 约束掩盖跨时钟路径。

## Vivado 门禁

工程报告记录：IP up-to-date、实现完成、无未布线网络、时序 WNS/WHS 为正、write_bitstream 成功。正式复现必须通过 GUI/Tcl/Vivado 生成 BD、IP output products、wrapper、synthesis、implementation、bitstream 和 XSA；不手改 `.xpr/.xci`。
