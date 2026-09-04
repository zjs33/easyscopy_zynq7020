# AC880 Zynq-7020 示波器优化与 Linux 接入说明

## 1. 本次已经完成的优化

### 1.1 高速缓存容量改为按通道数和内存预算计算

旧逻辑固定最多保存 2,000,000 个浮点样本。50 MSa/s 下，默认 5 ms/div × 10 div 需要 2,500,000 点，因此显示窗口会缺少左侧约 10 ms 数据。

现在显示缓存按以下规则计算：

```text
目标容量 = 采样率 × 最大历史时间
内存上限 = 16 MiB / (通道数 × sizeof(float))
实际容量 = min(目标容量, 内存上限)
```

单通道 50 MSa/s 时最多约 4,194,304 点，可以覆盖默认 50 ms 窗口，并限制嵌入式端内存使用。实现位置：

`src/plotting/waveformwidget.cpp::ensureBufferConfiguration()`

### 1.2 单通道环形缓存使用连续复制

ACM108 当前只有 1 路 AD0。旧逻辑每个样本都执行一次通道循环和环形下标取模；现在单通道场景按写指针是否跨尾部拆成最多两次 `std::copy_n()`，减少 1 MiB 数据块写入时的循环开销。

多通道路径仍保留原有逻辑，便于以后扩展。

实现位置：

`src/plotting/sampleringbuffer.cpp::SampleRingBuffer::append()`

### 1.3 增加 256 点 min/max 包络

绘图窗口大于屏幕像素数时，不能把每个原始样本都画出来。现在环形缓存维护 256 点一组的最小值/最大值包络，绘图时按像素查询范围：

```text
原始数据：每个像素扫描几百至几千点
优化后：完整 256 点块直接读取 min/max，边界最多扫描少量样本
```

这样可以保留尖峰，同时将绘图工作量从百万级样本扫描压缩到接近像素级。实现位置：

`src/plotting/sampleringbuffer.h/.cpp::rangeForSamples()`

绘图位置：

`src/plotting/waveformwidget.cpp::drawWaveforms()`

### 1.4 降低 DMA 设备轮询频率

1 MiB 数据在 50 MSa/s 下约 20.97 ms 完成。设备模式的 Qt 定时器由 1 ms 调整为 5 ms，仍由 `poll()` 等待设备可读，降低无效唤醒。

后续拿到 Linux 工程后，推荐进一步改成采集线程中的阻塞循环：

```text
poll() → read(1 MiB) → 发布数据块 → poll()
```

不要在 GUI 线程直接读取字符设备。

## 2. 当前数据所有权和复制路径

当前版本的实际路径是：

```text
DMA 驱动缓冲
    ↓ read()：内核复制到用户 QByteArray，1 MiB
QByteArray 原始 8-bit 段
    ↓ emitSegment()：转换为 float，约 4 MiB
DataBlock::interleaved
    ↓ SampleRingBuffer::append()：写入显示环形缓存
显示缓存 / FFT / 测量
```

当前版本已经优化了最后一步的单通道复制和绘图扫描，但还没有消除内核到用户空间的复制。要做到真正零拷贝，需要 Linux 驱动提供安全的 `mmap()` 接口，并明确缓冲区生命周期、段序号和缓存同步规则。

## 3. 接入 Linux 工程时需要提供的内容

请把以下文件或信息一并提供：

1. DMA 驱动 UAPI 头文件，尤其是 `START`、`STOP`、`GET_SEQ`、`GET_INFO` 的 ioctl 定义。
2. `read()` 的精确语义：是否保证一次返回完整 1 MiB，是否可能短读，短读时是否阻塞。
3. 段序号和丢段规则：序号从 0 还是 1 开始，环形缓冲覆盖时如何报告。
4. 采样率/分频寄存器的实际配置方式。
5. `source_select` 控制寄存器的 Linux 访问方式，并确认实际采样使用 `source_select = 0`。
6. LCD 使用的 Qt QPA 后端：`eglfs`、`linuxfb` 或定制 DRM/KMS 后端。

目前 Qt 代码不会臆造 ioctl 数值。没有 UAPI 时，只按驱动默认运行状态打开 `/dev/ac880_capture_dma` 并读取完整 1 MiB 段。

## 4. 建议的 Linux 接入结构

建议保持三层职责：

```text
DmaCaptureController
    └─ 负责 ioctl：启动、停止、查询状态、丢段统计

DmaCaptureDataSource
    └─ 负责 poll/read 或 mmap，并生成 DataBlock

MainWindow / WaveformWidget
    └─ 只处理线程安全的数据块和显示
```

不要让 `MainWindow` 直接操作 `/dev/uio0`，也不要让绘图控件知道 DDR3 物理地址。

如果 Linux 驱动的 `read()` 已经负责启动 DMA，第一版可以只接入 `DmaCaptureDataSource`。如果必须由用户空间启动，则在 `DmaCaptureController` 中调用真实 UAPI，然后再启动数据源。

## 5. 运行参数

Windows 仿真：

```powershell
ac880_zynq_scope.exe --simulation
```

Windows 二进制回放：

```powershell
ac880_zynq_scope.exe --capture capture-1m.bin
```

Zynq Linux：

```bash
AC880_CAPTURE_DEVICE=/dev/ac880_capture_dma \
./scripts/run_zynq_linux.sh
```

程序默认采样率为 50 MSa/s、段大小为 1 MiB、电压换算为 `(code - 128) / 128 V`。实际 ACM108 前端接入后，应根据板级标定修改增益和偏置。

## 6. 验证清单

- `ls -l /dev/ac880_capture_dma /dev/uio0`
- `dmesg | grep -Ei "ac880|dma"`
- 确认 RTL `source_select = 0`
- 确认波形不再是内部递增测试数据
- 观察 Qt 状态栏累计帧数是否连续增长
- 人为停止/重启 DMA，确认 Qt 能显示错误并恢复
- 检查段序号是否连续，确认无丢段或覆盖未报告
- 在 LCD 上检查默认 50 ms 窗口是否完整显示

## 7. 当前回归测试

Windows Qt 构建已通过 12/12 测试，其中包括：

`tests/test_dmacapturedatasource.cpp`

该测试验证 1 MiB 原始 8-bit 采样文件可以转换为有效的单通道 `DataBlock`，并检查 0、128、255 三个 ADC 码值的默认电压换算。

