# 07 已知问题

## Qt DMA 读取实现与文档存在版本差异

### 现象

Qt 源码 `dmacapturedatasource.cpp` 当前可见实现是 `poll()` + `read()` 完整 1 MiB 段；交接文档约定的是 mmap + WAIT_SEQ 零拷贝。

### 证据

源码包含 `poll()`、`read()`；内核驱动包含 `mmap()`、`WAIT_SEQ`。

### 原因

源码和部署二进制可能处于不同迭代版本。

### 当前解决方法

报告将两者分开记录，不把“设计约定”冒充“当前源码实现”。

### 是否彻底解决

未彻底解决。需要在 Ubuntu 重新交叉编译并在板端确认 `strings`/日志/UAPI 行为。

## Qt 字体路径

### 现象

`main.cpp` 非 Windows 分支硬编码 family；启动脚本默认 `QT_QPA_FONTDIR=/usr/share/fonts`，而精简 rootfs 可能没有该目录。

### 当前解决方法

后续按应用字体文件方案部署到 `/opt/ac880-qt/fonts/`。本报告不修改功能代码。

### 状态

【未实现/后续规划】。

## 触发帧端到端验收

### 现象

PL 采样引擎实现触发/预触发/后触发，但 Linux DMA 客户端以连续 period 为主，驱动没有把 AXI-Stream TLAST 作为用户态帧协议暴露。

### 状态

连续采集【已实现并实际使用】；可靠触发帧协议【未实现/后续规划】。

## DAC 板级验收

DAC HDL 和约束存在，但当前 Qt 示波器主要验证 ADC 采集显示，DAC 模拟输出没有当前工程证据证明已完成板级验收。
