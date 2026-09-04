# AC880 硬件拓扑摘要

## PL 显示链路

`AXI VDMA(MM2S, 0x43000000) -> VTC(0x43c10000) -> AXI4S Video Out -> rgb2lcd -> RGB565 LCD`

LCD 时序：800x480，H total 1056，V total 525，像素时钟约 33.264 MHz。

## PL 采集链路

ADC 数据进入 PL 采集逻辑，经 AXI DMA S2MM 写入 DDR3 环形缓冲。Linux 驱动导出 `/dev/ac880_capture_dma`，Qt 通过 `GET_INFO`、`mmap`、`START` 和 `WAIT_SEQ` 读取。

## 触摸连接

- GT911：PS I2C0，7-bit 地址 0x14
- TP_INT：PS MIO0 / Linux GPIO line 0
- TP_RST：PS GPIO EMIO[2] / Linux GPIO line 56 / PL Bank34 R15
