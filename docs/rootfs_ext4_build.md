# AC880 `rootfs.ext4` 构建与复现说明

## 结论

`rootfs.ext4` 不是手工把 `rootfs.tar.gz` 改名得到的文件，而是 PetaLinux/Yocto 在执行 `petalinux-build` 时，根据 rootfs 内容和镜像格式配置调用 `mkfs.ext4` 生成的块设备镜像。

当前仓库已经包含 AC880 的 `meta-user` 层、应用源码、内核配置片段、设备树覆盖和 Qt 部署包，能够支撑**在完整 PetaLinux 2026.1 工程中重建** `rootfs.ext4`。但仓库本身不是一个完整的 PetaLinux 工程，还需要：

- Ubuntu 中安装的 PetaLinux 2026.1 工具链；
- 一个由 `petalinux-create --template zynq` 创建的基础工程；
- 最新 Vivado 导出的 AC880 XSA（包含 bitstream）；
- 将本仓库的 `petalinux/meta-user` 合并到该工程；
- Qt recipe 所引用的两个部署文件（见下文）。

因此，只有 Git 仓库而没有 XSA、PetaLinux 基础工程和工具链时，不能单独复现完全相同的 ext4 镜像。

## 1. 创建或准备工程

在 Ubuntu 中：

```bash
source /home/jason/petalinux/2026.1/settings.sh
mkdir -p /home/jason/work/ac880_rebuild
cd /home/jason/work/ac880_rebuild

petalinux-create project --template zynq --name ac880_scope_acm108_2026_1
cd ac880_scope_acm108_2026_1
petalinux-config --get-hw-description /path/to/ac880_scope_acm108_touch_reset.xsa --silentconfig
```

`/path/to/ac880_scope_acm108_touch_reset.xsa` 必须替换为最新 Vivado 导出的 XSA。导入 XSA 会重新生成 PS、DDR、SD、UART、以太网和 PL 地址信息，不能用旧 XSA 代替。

## 2. 合并本仓库的持久化 layer

```bash
cp -a /path/to/easyscopy_zynq7020/petalinux/meta-user \
      project-spec/meta-user
```

如果目标工程已经存在 `project-spec/meta-user`，应合并内容而不是覆盖自己新增的 recipe。当前 layer 包含：

- `petalinux-image-minimal.bbappend`：加入采集、DMA、DRM 测试、Qt 运行时和开机服务；
- `recipes-bsp/device-tree/files/system-user.dtsi`：PL 显示、VDMA、GT911、SD 根参数；
- `recipes-kernel/linux`：DRM、UIO、动态时钟配置和补丁；
- `recipes-modules/ac880-capture-dma`：DMA 字符设备内核模块；
- `recipes-apps/ac880-capture`、`ac880-drm-test`：板端测试工具；
- `recipes-apps/ac880-qt-startup`：BusyBox 开机启动脚本。

## 3. 准备 Qt recipe 的输入文件

`ac880-qt-runtime.bb` 使用 `file://` 引用以下两个文件：

```text
project-spec/meta-user/recipes-apps/ac880-qt-runtime/files/ac880_qt_deploy.tar.gz
project-spec/meta-user/recipes-apps/ac880-qt-runtime/files/NotoSansCJKsc-Regular.otf
```

它们可从本仓库生成物复制：

```bash
mkdir -p project-spec/meta-user/recipes-apps/ac880-qt-runtime/files
cp /path/to/easyscopy_zynq7020/dist/qt/ac880_qt_deploy.tar.gz \
   project-spec/meta-user/recipes-apps/ac880-qt-runtime/files/
cp /path/to/easyscopy_zynq7020/dist/qt/NotoSansCJKsc-Regular.otf \
   project-spec/meta-user/recipes-apps/ac880-qt-runtime/files/
```

若暂时不需要 Qt，可从 `petalinux-image-minimal.bbappend` 移除 `ac880-qt-runtime` 和 `ac880-qt-startup`，这样不需要这两个文件；采集、DMA 和 LCD 相关内容仍可构建。

## 4. 配置 ext4 而不是 initramfs

在工程根目录执行：

```bash
petalinux-config
```

进入 `Image Packaging Configuration`，选择 SD 卡上的 ext4 根文件系统，并关闭 initramfs/initrd。最终 `project-spec/configs/config` 应满足：

```text
CONFIG_SUBSYSTEM_ROOTFS_EXT4=y
CONFIG_SUBSYSTEM_SDROOT_DEV="/dev/mmcblk0p2"
# CONFIG_SUBSYSTEM_ROOTFS_INITRD is not set
```

这一步很关键。若仍为 `CONFIG_SUBSYSTEM_ROOTFS_INITRD=y`，PetaLinux 可能把 CPIO ramdisk 封装进 `image.ub`，即使 bootargs 写着 `root=/dev/mmcblk0p2`，系统也可能实际从内存 rootfs 启动。

检查配置：

```bash
grep -E 'CONFIG_SUBSYSTEM_ROOTFS_(EXT4|INITRD)|CONFIG_SUBSYSTEM_SDROOT_DEV' \
    project-spec/configs/config
```

## 5. 构建 `rootfs.ext4`

推荐先构建设备树，再执行完整增量构建：

```bash
petalinux-build -c device-tree
petalinux-build
```

构建成功后，输出目录通常包含：

```text
images/linux/rootfs.ext4
images/linux/rootfs.tar.gz
images/linux/image.ub
images/linux/system.dtb
```

其中：

- `rootfs.ext4`：可直接写入 ext4 分区的文件系统镜像；
- `rootfs.tar.gz`：适合挂载第二分区后用 `tar --numeric-owner -xpf` 解压；
- `image.ub`：内核/FIT 镜像，不等于 rootfs.ext4。

验证输出：

```bash
ls -lh images/linux/rootfs.ext4 images/linux/rootfs.tar.gz
file images/linux/rootfs.ext4
dumpimage -l images/linux/image.ub
```

`dumpimage -l` 在使用 SD ext4 根方案时不应出现 `RAMDisk Image`。若出现 RAMDisk，回到第 4 步检查配置并重新执行 `petalinux-build`。

## 6. 生成启动包

rootfs 构建完成后重新打包 BOOT.BIN，确保使用最新 bitstream：

```bash
petalinux-package boot \
  --fsbl images/linux/zynq_fsbl.elf \
  --fpga images/linux/system.bit \
  --u-boot images/linux/u-boot.elf \
  --output images/linux/BOOT.BIN --force
```

BOOT 分区放置：

```text
BOOT.BIN
image.ub
boot.scr
```

第二分区可以直接写 `rootfs.ext4`，或者格式化为 ext4 后解压 `rootfs.tar.gz`。两种方式不要混用到同一分区。

## 7. 当前已有产物是否足够

当前已生成的 `dist/petalinux` 中有 `BOOT.BIN`、`image.ub`、`boot.scr`、`system.bit`、`system.dtb` 和 `rootfs.tar.gz`；这些足够部署和验证，也能证明当前 rootfs 内容已成功生成。 `dist/qt` 中有 Qt 归档和字体。

但是仓库没有提交 `rootfs.ext4`（约 154 MiB，超过 GitHub 普通文件限制），也没有提交完整 `build/`、XSA 和 `project-spec/configs`。要重新生成而不是直接使用现有镜像，必须补齐上一节列出的基础工程、XSA、配置和 Qt recipe 输入文件。

## 8. 本次已验证的本地输出

```text
D:\zynqstudy\1linux\petalinux_output\ac880_scope_acm108_2026_1\linux\rootfs.ext4
```

该文件大小为 `160532480` 字节。对应的可上传压缩包和校验值见 [`dist/README.md`](../dist/README.md) 与 [`dist/SHA256SUMS`](../dist/SHA256SUMS)。

