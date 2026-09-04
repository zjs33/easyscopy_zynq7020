# AC880 部署生成物

本目录保存 AC880 Zynq-7020 示波器当前构建得到的可部署文件。构建工具链为 Vivado/PetaLinux 2026.1，Qt 应用使用 Qt5 `linuxfb`，并包含 GT911 触摸支持及中文字体资源。

## PetaLinux 启动文件

将 `petalinux/BOOT.BIN`、`petalinux/image.ub` 和 `petalinux/boot.scr` 复制到 SD 卡的 **BOOT** 分区。`BOOT.BIN` 是 PetaLinux 打包产物，已包含启动所需的 FSBL、bitstream 和 U-Boot；因此不要只更新 DTB 而继续使用旧的 BOOT.BIN。

`system.bit` 和 `system.dtb` 同时保留在目录中，便于检查和调试。正常启动时优先使用打包后的 `BOOT.BIN`/`image.ub`。

`rootfs.tar.gz` 可用于将根文件系统解压到 SD 卡的 **rootfs** 分区（先挂载该分区，再以 root 身份解压并保留权限）。本次构建的 `rootfs.ext4` 约 154 MiB，超过 GitHub 普通文件 100 MiB 限制，未上传仓库，仍保留在本地构建输出：

```text
D:\zynqstudy\1linux\petalinux_output\ac880_scope_acm108_2026_1\linux\rootfs.ext4
```

## Qt 应用和字体

将 `qt/ac880_qt_deploy.tar.gz` 复制到板端后解压。归档内容按部署脚本安装到 `/opt/ac880-qt/`，其中 Qt 可执行程序、启动脚本和运行库位于该目录；中文字体文件应安装为：

```text
/opt/ac880-qt/fonts/NotoSansCJKsc-Regular.otf
```

也可以单独复制 `qt/NotoSansCJKsc-Regular.otf` 到上述路径。启动脚本会设置 `QT_QPA_FONTDIR=/opt/ac880-qt/fonts`，程序本身还会通过 `QFontDatabase::addApplicationFont()` 做容错加载。

## 推荐启动和验证

```sh
/etc/init.d/S99ac880-qt start
# 或按项目脚本启动
/opt/ac880-qt/bin/ac880_scope_qt
```

启动后可检查显示、采集和字体日志：

```sh
dmesg | grep -Ei "drm|fb|goodix|ac880|capture"
ls -l /dev/dri /dev/input
cat /tmp/ac880_qt.log
```

## SHA-256

为便于传输，`packages/` 下提供两个压缩包，均小于 GitHub 普通文件 100 MiB 限制：

- `packages/AC880-PetaLinux-deploy.zip`：SD 卡启动文件和 `rootfs.tar.gz`。
- `packages/AC880-Qt-runtime.zip`：Qt 运行归档和中文字体。

压缩包校验值：

```text
4D5DA97D626D568E831705BDCB8D71CD88B246B13819283FC58AC08284C53827  packages/AC880-PetaLinux-deploy.zip
8C3C25CE5CDAC4DD5955DFA8B25D13290378524F10FB178A4D2BD512B0958D29  packages/AC880-Qt-runtime.zip
```

| 文件 | SHA-256 |
|---|---|
| `petalinux/BOOT.BIN` | `BCB15FF3FD946DB7B25A2578F7B074AA6D33646DD270140A759BBF98EEDC7F14` |
| `petalinux/boot.scr` | `4C7ED60CAD10872084BF727B2C9AB0ECA9EAB992635CF3ABF67DFE7ED9CC7807` |
| `petalinux/image.ub` | `DECBBA991EA1FF5A6C0006A3C37ABB036D2FA6FB2BE5A4651CD53C0FC80003B0` |
| `petalinux/rootfs.tar.gz` | `515056609E4889761BF2DD1877ACBFAB74FC409BBDF3D5F503AE5E3CBA052F5C` |
| `petalinux/system.bit` | `80A457F8D0E3A00D618C609437E10402B5BB6A67150EF40A94285E0EE7C03698` |
| `petalinux/system.dtb` | `B1A3D5133261CBE0BFE704C2E46BF8C5C806ECFACAF6E7E96944A525309C6AF0` |
| `qt/ac880_qt_deploy.tar.gz` | `52B776D034CF27429262BD2A4C4AE066349F92BA76A155519D91C9CFEC3765C6` |
| `qt/NotoSansCJKsc-Regular.otf` | `2C76254F6FC379FDDFCE0A7E84FB5385BB135D3E399294F6EEB6680D0365B74B` |
