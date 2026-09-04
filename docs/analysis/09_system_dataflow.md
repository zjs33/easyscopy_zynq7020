# 09 端到端系统数据流

## ADC 到 Qt

```text
物理模拟信号
  -> ACM108 ADC AD0[7:0]
  -> acm108_io（50 MHz 采样寄存器）
  -> ac880_sample_engine（分频/连续或触发）
  -> XPM async FIFO
  -> ac880_axis_packer（8-bit -> 64-bit AXIS）
  -> AXI DMA S2MM（0x40400000）
  -> SmartConnect -> PS HP1
  -> PS DDR coherent 4 MiB ring
  -> ac880_capture_dma 字符驱动
  -> Qt DmaCaptureDataSource
  -> DataBlock(float)
  -> SampleRingBuffer / WaveformWidget
  -> LCD DRM/KMS 或 framebuffer
```

## 每一步的数据与同步

| 位置 | 数据 | 写入者 | 读取者 | 同步 |
|---|---|---|---|---|
| AD0 | 8-bit ADC code | ACM108 | `acm108_io` | `adc_capture_clk` |
| 采集 FIFO | 9-bit（含 TLAST 标记） | sample engine | AXIS packer | async FIFO |
| AXI-Stream | 64-bit beat + TKEEP/TLAST | packer | AXI DMA | AXIS ready/valid |
| PS DDR ring | 4 × 1 MiB 原始样本 | DMA | Linux driver | DMA callback/period_seq |
| DataBlock | float 电压 | Qt acquisition thread | GUI/处理线程 | Qt queued signals |
| 显示 ring | float 通道缓存 + envelope | `SampleRingBuffer` | waveform/FFT | GUI 线程 |

## 控制与状态

```text
Qt/命令行
  -> /dev/uio0
  -> capture_subsystem AXI-Lite 0x43C20000
  -> CDC handshake
  -> sample_engine
```

DMA 的寄存器由内核 xilinx-dma 驱动管理，Qt 不直接操作 AXI DMA 物理寄存器。`/dev/uio0` 仅用于采集控制/状态寄存器。

## 显示反向链路

```text
Qt/DRM 或 fbdev 写显示缓冲
  -> PS DDR
  -> AXI VDMA MM2S 0x43000000 / PS HP0
  -> rgb565to888
  -> rgb2lcd
  -> panel-dpi LCD 800×480
```

## 关键一致性检查

- 8-bit ADC 样本与 Qt 单通道 8-bit 输入一致；当前 PL 并非把两个连续 8-bit 样本拼成 16-bit ADC 样本。
- DMA period、Qt 读取长度和 4 MiB ring 必须保持一致。
- PS DDR 是运行内存；SD 只负责启动和持久化文件。
