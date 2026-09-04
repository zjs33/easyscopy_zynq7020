# Zynq 简易示波器项目 V1

> 文档类型：工程技术文档、项目交接文档、复现手册
>
> 平台：AC880、Zynq-7020、ACM108、Vivado/PetaLinux 2026.1、Qt 5.12.8
>
> 事实以当前工程文件为准；无法确认的内容标记为待确认。

## 1 项目概述

V1 将 ACM108 单通道 8-bit ADC 数据采入 PL，经 AXI DMA 写入 Zynq PS DDR，由 Linux 驱动提供字符设备，Qt 读取并显示时域波形或 FFT 频谱。显示链路为 AXI VDMA + VTC + 动态像素时钟 + RGB565 LCD；触摸链路为 PS I2C0 + GT911。DAC 测试源保留在硬件工程中，但不是当前采集闭环的一部分。

状态标签：

- 【已实现并实际使用】
- 【已有设计但当前未启用】
- 【未实现/后续规划】
- 【无法从工程确认】

## 2 系统总体架构

~~~text
ACM108 AD0[7:0]
  -> acm108_io
  -> ac880_capture_subsystem
  -> AXI-Stream
  -> AXI DMA S2MM/SG
  -> capture_hp1_smartconnect
  -> PS S_AXI_HP1
  -> PS DDR3
  -> ac880_capture_dma
  -> /dev/ac880_capture_dma
  -> Qt DmaCaptureDataSource
  -> DataBlock / SampleRingBuffer
  -> WaveformWidget 或 SpectrumWidget
  -> DRM/KMS 或 fbdev
  -> LCD
~~~

## 3 硬件和资源

### 3.1 器件与 DDR

- 器件：xc7z020clg484-2。
- PS DDR3：工作流资料记录为 2 片 MT41K256M16，32-bit、1 GiB，供 PS/Linux 使用。
- PL DDR3：早期纯 PL 参考工程记录为 1 片 MT41K128M16，16-bit、256 MiB，由 PL MIG 使用。
- 当前 Linux DMA 目标是 PS DDR，不是 PL MIG DDR。
- PS OCM 为 256 KiB；A9 每核 L1 指令/数据缓存各 32 KiB，共享 L2 为 512 KiB。当前 4 MiB DMA ring 和 Qt 缓存均在 PS DDR，未使用 OCM 作为大缓存。

### 3.2 ACM108 P7 映射

| P7 | 信号 | FPGA 管脚 | 方向 |
|---:|---|---|---|
| 1 | DA0_Data[7] | U12 | 输出 |
| 2 | DA0_Clk | U11 | 输出 |
| 3 | DA0_Data[5] | U10 | 输出 |
| 4 | DA0_Data[6] | U9 | 输出 |
| 5 | DA0_Data[3] | Y11 | 输出 |
| 6 | DA0_Data[4] | Y10 | 输出 |
| 7 | DA0_Data[1] | V7 | 输出 |
| 8 | DA0_Data[2] | W7 | 输出 |
| 9 | AD0[0] | AB2 | 输入 |
| 10 | DA0_Data[0] | AB1 | 输出 |
| 11 | +5V | — | 电源 |
| 12 | GND | — | 地 |
| 13–19 | AD0[1]…AD0[7] | U6/U5/V5/V4/T4/U4/R6 | 输入 |
| 20 | AD0_CLK | T6 | 输出 |

P7 物理脚顺序不能当作逻辑位序。当前闭环只使用 AD0；第二 ADC/DAC 通道未接入。

## 4 Vivado FPGA 设计

### 4.1 工程和源文件

Vivado 工程目录：D:\zynqstudy\1linux\fpga\ac880_scope_acm108_2026_1

关键文件：

- ac880_linux_bsp.srcs/sources_1/bd/system/system.bd
- scope_src/rtl/ac880_acm108_io.sv
- scope_src/rtl/ac880_capture_subsystem.sv
- scope_src/rtl/ac880_capture_ctrl_axi.sv
- scope_src/rtl/ac880_sample_engine.sv
- scope_src/rtl/ac880_axis_packer.sv
- scope_src/rtl/ac880_dac_test_source.sv
- scope_src/constraints/acm108_scope.xdc
- scope_src/export/ac880_scope_acm108_2026_1.xsa
- scope_src/export/ac880_scope_acm108_2026_1.bit

### 4.2 采集模块

ac880_acm108_io【已实现并实际使用】：在 50 MHz 采样域寄存 AD0[7:0]，用 ODDR 转发 AD0_CLK。

ac880_sample_engine【已实现并实际使用】：实现采样分频、连续采样、触发状态机、预触发/后触发、内部递增/LFSR 测试源、样本数/溢出/触发统计。

ac880_capture_subsystem【已实现并实际使用】：包含 AXI-Lite 控制、配置/统计 CDC、XPM 异步 FIFO、采样引擎和 AXIS 输出。

ac880_axis_packer【已实现并实际使用】：把 9-bit FIFO 项中的 8-bit 样本组装为 64-bit AXIS beat，利用 TKEEP/TLAST 表示末 beat。

### 4.3 显示模块

- axi_vdma_0：MM2S 显示帧读取，接 PS HP0。
- rgb565to888_0：RGB565 转 RGB888。
- rgb2lcd_0：并行 RGB LCD 输出。
- v_tc_0：视频时序。
- axi_dynclk_0：像素时钟。
- xlconcat_0：合并 VTC、VDMA、DMA 中断并接到 IRQ_F2P。

Linux 已经出现 xlnx DRM、DPI connector、/dev/dri/card0，并保留 /dev/fb0 兼容接口。【已实现并实际使用】

### 4.4 8-bit 与 16-bit

当前 PS-DMA/Qt 闭环是单通道原始 8-bit 样本。早期 PL-MIG/UART 工程中的 {8'h00, adc_data + 8'h80} 是 8→16 扩展，不代表当前 DMA 协议。当前没有把两个连续 8-bit 样本拼成一个 16-bit 样本。若改变格式，必须同时改变 TLAST、计数、DMA period 和 Qt 解码。

### 4.5 DAC

ac880_dac_test_source.sv 使用 125 MHz 相位累加器和 8-bit 正弦 LUT，输出 DA0_Data 和 DA0_Clk。旧参考工程还有 DDS_Module、正弦/方波/三角波 ROM。当前 DAC 源文件/BD 设计【已有设计但当前未启用】；DAC 模拟输出幅值、失真和板级时序【未实现/后续规划】。

### 4.6 时钟、复位和 CDC

PS FCLK0 驱动 AXI-Lite、DMA、SmartConnect、控制和 AXIS 域；采样输入使用 50 MHz 域；视频像素时钟来自 axi_dynclk_0。采样与 AXIS 之间通过 XPM CDC/async FIFO，不能用宽泛 clock-group 约束掩盖错误 CDC。

## 5 AXI、地址和内存

| 模块 | 基地址 | 范围 | Linux 访问 |
|---|---:|---:|---|
| AXI DMA | 0x40400000 | 64 KiB | xilinx-dma/DMAengine |
| AXI dynamic clock | 0x43C00000 | 64 KiB | DRM/VTC clock |
| AXI VDMA | 0x43000000 | 64 KiB | xilinx-vdma/DRM |
| VTC | 0x43C10000 | 64 KiB | VTC bridge |
| Capture AXI-Lite | 0x43C20000 | 64 KiB | generic-uio、/dev/uio0 |

地址由 system.bd 的 address-space 条目确认；DMA 缓冲地址是运行时分配的物理地址，不是寄存器基地址。

## 6 PetaLinux

### 6.1 持久化修改

工程：petalinux_scope_acm108_2026_1。持久化修改只放 project-spec/meta-user：设备树、kernel cfg/bbappend、DMA module recipe、采集工具和 DRM 测试程序。不要长期修改自动生成 DTS 或 build/tmp。

### 6.2 导入、构建、打包

~~~bash
source /home/jason/petalinux/2026.1/settings.sh
petalinux-create project --template zynq --name ac880_scope_acm108_2026_1
cd ac880_scope_acm108_2026_1
petalinux-config --get-hw-description <XSA_PATH> --silentconfig
petalinux-config --silentconfig
petalinux-build -c device-tree
petalinux-build
petalinux-package boot \
  --fsbl images/linux/zynq_fsbl.elf \
  --fpga images/linux/system.bit \
  --u-boot images/linux/u-boot.elf \
  --output images/linux/BOOT.BIN
~~~

Vivado PL 改动后必须用含最新 bitstream 的 XSA 重新导入和打包，不能只更新 DTB。

### 6.3 SD 启动

FAT BOOT 分区放 BOOT.BIN、image.ub、boot.scr；ext4 第二分区作为 /dev/mmcblk0p2 根文件系统。SD 是存储介质，Linux 内核运行时在 PS DDR。

### 6.4 Device Tree 节点

system-user.dtsi 当前描述 axi_dma_capture、ac880_capture_dma_client、capture_subsystem generic-uio、pl-disp、axi_vdma_0、VTC、panel-dpi、axi_dynclk_0 和 v_tc_0。构建后必须反编译 DTB 检查 memory、reg、interrupt、DMA channel 和 Goodix 节点。

## 7 Linux 驱动和工具

### 7.1 UIO

capture_subsystem 通过 generic-uio 映射 0x43C20000 为 /dev/uio0。ac880_capture_ctl 负责 ID、status、start、stop、clear、snapshot；不负责 DMA 数据搬运。

### 7.2 DMA 字符驱动

ac880_capture_dma.c 通过 dma_request_chan 请求 rx，使用 dma_alloc_coherent 分配 4 MiB，调用 dmaengine_prep_dma_cyclic 建立 4 个 1 MiB period，period callback 更新 period_seq，再通过 waitqueue、poll、read、mmap、ioctl 提供用户态访问。

UAPI 文件为 ac880_capture_dma_uapi.h，包含 START、STOP、GET_SEQ、GET_INFO、WAIT_SEQ。ac880_dma_mmap_test 用于验证映射和序号。

### 7.3 版本差异

Qt 源码 dmacapturedatasource.cpp 可见实现是 poll()+read() 完整 1 MiB 段；配套接入说明定义了 GET_INFO→mmap(4 MiB)→WAIT_SEQ 零拷贝流程。当前板端二进制是否已切换到 mmap 版本【无法从工程确认】。

## 8 Qt 软件架构

工程：_qt_scope_ref_20260903_v2。

~~~text
main.cpp
  -> QApplication
  -> DmaCaptureDataSource/SimulationDataSource
  -> AcquisitionThread
  -> MainWindow::handleBlock
  -> SampleRingBuffer
  -> WaveformWidget
  -> MeasurementEngine/TriggerEngine
  -> FftWorker -> SpectrumWidget
  -> FileWorker -> WaveformFileCodec
~~~

GUI 线程只处理 DataBlock；采集、FFT、文件任务通过 QThread 和 queued signal/slot 解耦。SampleRingBuffer 使用单通道连续复制和 256 点 min/max envelope。MainWindow 提供时基、Y 灵敏度、时域/FFT 切换、触发、测量和导入导出。

实时数据在驱动 4 MiB ring 和 Qt 用户态 ring 中，均是 PS DDR；进程退出后 Qt 缓存丢失。CSV/TXT 只有点击导出并选择路径后才写盘，当前没有固定默认目录。

## 9 LCD、DRM、framebuffer 和 GT911

当前板端已验证 /dev/dri/card0、DPI connector connected/enabled 和 /dev/fb0。DRM/KMS 是主接口，fbdev 仅作兼容测试。

GT911 位于 PS I2C0；TP_INT、TP_RST 的最终 GPIO 编号、极性和中断计数必须以最新 XSA、最终 DTB 和 /proc/interrupts 实测闭环确认。

~~~bash
dmesg | grep -i goodix
cat /proc/interrupts
cat /proc/bus/input/devices
gpioinfo
hexdump -C /dev/input/event0
~~~

## 10 PC-Ubuntu-Zynq 在线开发

~~~text
Windows/Codex
  -> SSH 到 Ubuntu VM
  -> Ubuntu 执行 PetaLinux 构建
  -> SCP/rsync 到 Zynq
  -> 板端 SSH/串口运行和验证
~~~

凭据使用 <SSH_KEY>、<USERNAME>、<SSH_PORT>、<BOARD_IP> 占位符，不在报告保存密码或私钥。

## 11 Vivado GUI/Tcl 流程

Create Project → 选择器件 → Add Sources/Constraints → Create Block Design → Zynq PS7 → Block Automation → 配置 PS DDR、FCLK、GP、HP、IRQ → 加入 AXI DMA、SmartConnect、VDMA、VTC、dynamic clock、自定义采集模块 → 连接 AXI/时钟/复位/中断 → Address Editor → Validate Design → Generate HDL Wrapper → Synthesis → Implementation → Bitstream → Export XSA（包含 bitstream）。

Tcl 只执行工程中已有脚本；新增脚本必须以实际 cell/IP/端口为依据。禁止直接编辑 .xpr/.xci。

## 12 验收命令

~~~bash
ls -lh images/linux
file images/linux/BOOT.BIN images/linux/image.ub images/linux/system.dtb images/linux/system.bit
bootgen -arch zynq -read images/linux/BOOT.BIN
sha256sum images/linux/BOOT.BIN images/linux/image.ub images/linux/system.dtb images/linux/system.bit
~~~

板端：

~~~bash
uname -a
cat /proc/cmdline
cat /proc/meminfo
mount
ip addr
dmesg | grep -Ei 'mmc|eth|drm|vdma|dma|goodix|i2c|framebuffer'
~~~

采集：

~~~bash
ls -l /dev/uio0 /dev/ac880_capture_dma
/usr/bin/ac880_capture_ctl /dev/uio0 status
/usr/bin/ac880_dma_mmap_test /dev/ac880_capture_dma
dd if=/dev/ac880_capture_dma of=/tmp/dma.bin bs=1048576 count=1
~~~

先用内部测试源，再用 1 kHz/1 Vpp 外部正弦波；检查 period_seq、overflow、实际采样率、幅值标定和长时间运行。

## 13 已知问题和后续规划

- PetaLinux 2020.x 原地迁移风险高，当前采用新建 2026.1 工程和选择性迁移。
- image.ub 带 initramfs 时，root=/dev/mmcblk0p2 不代表根目录来自 SD，需检查 /proc/mounts 和 dumpimage。
- 4 段环形缓冲在用户态处理不及时会覆盖并产生丢段。
- Qt 源码和 mmap 接入说明存在版本差异。
- 精简 rootfs 可能没有系统字体目录。
- 触发 TLAST 已在 PL 产生，但可靠触发帧协议尚未冻结。
- DAC 有 HDL 设计，但没有当前板级模拟验收证据。
- 最终 Qt 二进制、DTB IRQ、ADC 标定、QPA 后端、PL BRAM 使用量和 DAC 指标待确认。

## 14 V1 状态与 V2 计划

| 功能 | 状态 |
|---|---|
| Vivado 2026.1、PS/PL、XSA | 【已实现并实际使用】 |
| ADC 单通道采集前端 | 【已实现并实际使用】 |
| AXI DMA S2MM 到 PS DDR | 【已实现并实际使用】 |
| UIO 控制、DRM card0、fb0 | 【已实现并实际使用】 |
| GT911 event0 | 【已实现并实际使用】；IRQ 计数需每版复核 |
| Qt 时域/FFT/测量/文件框架 | 【已实现并实际使用】 |
| Qt 当前二进制和源码一致 | 【无法从工程确认】 |
| 可靠触发帧、标定、DAC 模拟验收 | 【未实现/后续规划】 |

V2 优先级：

1. 统一 Qt、UAPI 和板端程序为 mmap + WAIT_SEQ 零拷贝版本。
2. 将采样率、格式、通道数、标定参数显式化。
3. 增大或可配置 DMA ring，增加丢段和覆盖统计。
4. 冻结 TLAST、帧序号、时间戳和单次触发协议。
5. 完成 GT911 实测和应用字体部署。
6. 增加 ADC 标定、RMS、峰峰值、频率和长期运行日志。
7. 单独完成 DAC 模拟输出验收。

## 15 从零复现清单

~~~text
[ ] 阅读 docs/analysis 和 AC880 管脚资料
[ ] Vivado 2026.1 Validate BD、综合、实现、生成 bitstream
[ ] 导出含最新 bitstream 的 XSA
[ ] Ubuntu source PetaLinux 2026.1
[ ] 新建 zynq 工程并导入 XSA
[ ] 合并 meta-user，构建设备树
[ ] 完整 petalinux-build
[ ] petalinux-package boot 生成 BOOT.BIN
[ ] 反向读取 BOOT.BIN、反编译 system.dtb、计算哈希
[ ] SD BOOT 分区放 BOOT.BIN/image.ub/boot.scr
[ ] SD ext4 分区解压 rootfs.tar.gz
[ ] 上板验证 Linux、DDR、网络、DRM、触摸、UIO
[ ] 先内部测试源，再接 ACM108
[ ] 验证 DMA period、overflow、序号和 Qt 连续显示
[ ] 归档源码、project-spec、产物、日志和哈希
~~~

