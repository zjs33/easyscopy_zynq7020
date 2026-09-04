# 04 Qt 分析

## 构建

工程：`_qt_scope_ref_20260903_v2`，CMake 支持 Qt 5/6，Zynq 目标强制 Qt 5.12.8。Linux 脚本要求交叉编译器和 `AC880_QT5_PREFIX`。

## 类关系

```text
main.cpp
  -> QApplication / MainWindow
  -> DmaCaptureDataSource 或 SimulationDataSource
  -> AcquisitionThread
  -> MainWindow::handleBlock
       -> SampleRingBuffer
       -> WaveformWidget
       -> MeasurementEngine / TriggerEngine
       -> FftWorker -> SpectrumWidget
       -> FileWorker -> WaveformFileCodec
```

采集源、FFT 和文件 worker 分别运行在 QThread；GUI 线程通过 signal/slot 接收已整理的 `DataBlock`，避免 GUI 直接阻塞设备读取。

## Linux DMA 读取

`DmaCaptureDataSource` 打开 `/dev/ac880_capture_dma`，当前源码实现使用 `poll()` + `read()` 读取完整 1 MiB 段；工程文档同时约定了 `GET_INFO → mmap(4 MiB) → WAIT_SEQ` 的零拷贝路径，需以板端实际部署二进制/UAPI 版本为准。两者不能在报告中混写为同一运行实现。

## 显示与处理

- `WaveformWidget` 维护多通道环形缓存、时间/幅度缩放、余辉、光标和 min/max 包络。
- `SampleRingBuffer` 单通道使用连续复制，256 点 envelope 用于降低绘图 CPU 占用。
- `FftWorker` 后台计算 FFT，`SpectrumWidget` 显示频谱。
- `MainWindow` 提供开始/暂停、清屏、时基、Y 灵敏度、时域/FFT 切换、触发、测量和文件操作。

## 文件存储

程序不会自动把每个实时块写成 CSV/TXT。点击导出后由文件对话框决定路径，`WaveformFileCodec` 使用 `QSaveFile` 写入 CSV 或制表符 TXT；路径可以是上位机或板端当前文件系统，取决于程序在哪里运行。数据默认没有固定保存目录。

## 字体与显示

`main.cpp` 目前仍有非 Windows 分支硬编码 `Noto Sans CJK SC` 的历史逻辑；字体部署任务应改为应用字体文件 + `QFontDatabase::addApplicationFont()`，但这属于后续修正，不应在当前 V1 报告中宣称已经完成。
