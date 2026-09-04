# AC880 Zynq-7020 Linux 示波器（Qt5）

这是面向 Zynq-7020 的新版白色风格 Qt 示波器工程。工程独立于已有 Windows/STM32 工程，当前版本优先打通“连续采样、读取 1 MiB、降采样显示”的链路。

## 数据链路

```text
ACM108 AD0[7:0] → PL 采样引擎 → AXI DMA S2MM → PS DDR3
→ Linux dma_alloc_coherent 环形缓冲 → /dev/ac880_capture_dma → Qt 采集线程
```

Qt 不访问固定 DDR3 物理地址，也不把 `/dev/uio0` 当作采样数据设备。`/dev/uio0` 仍由采集控制逻辑使用；采样数据只从 `/dev/ac880_capture_dma` 读取。

## 当前实现

- 单通道 AD0，8-bit 无符号采样值。
- 默认采样率 50 MSa/s，默认每段 1 MiB（1,048,576 个样本）。
- Linux 设备读取采用 `poll()` + 完整 1 MiB 段读取。
- Windows 可用同样的 1 MiB 二进制文件循环回放，便于先优化界面和绘图性能。
- 默认电压换算为 `(code - 128) / 128 V`；实际量程/偏置应按 ACM108 前端标定值调整。
- 当前按连续采样显示；驱动没有向用户空间暴露 AXI-Stream `TLAST`，4096 点触发帧暂不宣称可靠支持。
- RTL 使用 ACM108 实际采样前，请确认 `source_select = 0`，否则仍可能显示内部递增测试数据。

## Windows 编译与回放

在本机 Qt/MinGW 环境运行：

```powershell
./scripts/build_windows.ps1
build-release\src\ac880_zynq_scope.exe --simulation
build-release\src\ac880_zynq_scope.exe --capture .\capture-1m.bin
```

`build_windows.ps1` 会自动运行 `windeployqt`，把 Qt DLL、`platforms\qwindows.dll`
和 MinGW 运行库复制到 `build-release\src`。在资源管理器中直接双击该目录下的
`ac880_zynq_scope.exe` 即可启动。

二进制回放文件必须至少包含 1 MiB 原始 8-bit 样本；程序会循环读取。

## Zynq Linux 编译与运行

交叉编译时提供 Qt5 安装路径和交叉编译器：

```bash
export AC880_QT5_PREFIX=/opt/qt5-zynq
export CXX=arm-linux-gnueabihf-g++
export CC=arm-linux-gnueabihf-gcc
./scripts/build_zynq_linux.sh
```

板端运行：

```bash
./scripts/run_zynq_linux.sh
```

脚本默认使用 Qt5 `eglfs` 的 DRM/KMS 后端输出到 LCD，并打开
`/dev/ac880_capture_dma`；也可以设置 `AC880_SCOPE_APP` 或
`AC880_CAPTURE_DEVICE` 覆盖程序和设备路径。

## 尚需板级确认

目前没有在工程中臆造 `START/STOP/GET_SEQ` 的 ioctl 数值，因为交接说明没有附带对应 UAPI 头文件。第一版设备源按驱动的默认运行状态读取字符设备；若驱动要求 Qt 主动启动/停止 DMA，请提供驱动 UAPI 头文件或 ioctl 定义，再接入控制器并保持 ABI 一致。
