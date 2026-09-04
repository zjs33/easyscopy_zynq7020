# AC880 Zynq-7020 简易示波器

本仓库整理 AC880 Zynq-7020 采集、显示和 Linux/Qt 工程，作为后续采集链路和上位机功能开发的基线。

## 工程组成

- `qt-scope/`：Qt 5.12 示波器应用。包含 Linux DMA mmap 采集、波形/FFT 绘制、GT911 触摸、文件回放和测试。
- `petalinux/meta-user/`：PetaLinux 2026.1 持久化配方，包含 DRM/KMS LCD、GT911、采集 DMA、UIO 和 Qt 启动脚本。
- `docs/`：Vivado、PetaLinux、Qt、网络调试和已知问题记录。
- `hardware/`：PL 显示和采集拓扑说明。

rootfs.ext4 的构建条件、配置开关和复现步骤见 [`docs/rootfs_ext4_build.md`](docs/rootfs_ext4_build.md)。

## 数据路径

```text
ADC(8-bit, 50 MSa/s)
  -> PL 采集逻辑 / AXI DMA S2MM
  -> ac880_capture_dma 环形缓冲
  -> /dev/ac880_capture_dma
  -> Qt mmap + WAIT_SEQ
  -> 50 倍抽取为约 1 MSa/s
  -> 像素级 min/max 包络
  -> RGB565 LCD /dev/fb0
```

LCD 为 800x480 RGB565；GT911 位于 PS I2C0，地址 0x14，INT 使用 MIO0，RST 使用 EMIO GPIO line 56。

## Qt 构建

Windows 仿真构建：

```text
cmake -S qt-scope -B build-release
cmake --build build-release --parallel 2
ctest --test-dir build-release --output-on-failure
```

Zynq ARM 构建和板端变量见 `qt-scope/scripts/build_zynq_linux.sh`、`qt-scope/scripts/run_zynq_linux.sh`。

## PetaLinux 构建

将 `petalinux/meta-user` 合并到 PetaLinux 工程的 `project-spec/meta-user`，然后执行：

```bash
source /path/to/petalinux/2026.1/settings.sh
petalinux-build
petalinux-package boot --fsbl images/linux/zynq_fsbl.elf \\
  --fpga images/linux/system.bit --u-boot images/linux/u-boot.elf \\
  --output images/linux/BOOT.BIN --force
```

Qt 运行时和字体部署路径固定为 `/opt/ac880-qt`。BusyBox 启动入口为 `/etc/rc5.d/S99ac880-qt`。

## 板端验证

```bash
ls -l /dev/dri/card0 /dev/fb0 /dev/ac880_capture_dma
cat /tmp/ac880_qt.log
ps | grep ac880_zynq_scope
ac880_capture_ctl /dev/uio0 status
```

默认启动 Qt 界面但不自动消费 DMA；点击“开始”按钮后进入连续显示。

## 注意事项

- 不提交 Vivado/PetaLinux 生成目录和大体积镜像；使用文档中的 Xilinx 工具重新生成。
- 不要在 rootfs 中同时运行旧的 `ac880_capture_lcd` 和 Qt 程序，避免 framebuffer 冲突。
- 如果界面出现空挡，优先检查 DMA `WAIT_SEQ` 跳号和 `droppedPeriods`，再检查抽取比例。
