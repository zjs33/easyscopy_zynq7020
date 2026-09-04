# 05 PC-Ubuntu-Zynq 网络与 Codex

## 实际开发路径

```text
Windows/Codex 工作区
   -> SSH 到 Ubuntu VM（端口转发 127.0.0.1:<SSH_PORT>）
   -> Ubuntu 内 PetaLinux/Vivado 相关 Linux 工作目录
   -> SSH/SCP 到 Zynq 开发板（<BOARD_IP>）
   -> 板端运行 Linux、驱动、Qt
```

工作流文档记录了 Ubuntu 入口形式：`ssh -i <SSH_KEY> -p <SSH_PORT> <USERNAME>@127.0.0.1`。凭据、密码、私钥、Token 不写入报告，统一使用 `<SSH_KEY>`、`<USERNAME>`、`<BOARD_IP>` 占位符。

## 文件传输

- Windows → Ubuntu：`scp -i <SSH_KEY> -P <SSH_PORT> ...`。
- Ubuntu → Zynq：使用 `scp`/`rsync` 或板端在线部署脚本。
- 大型 PetaLinux 构建只在 Ubuntu 原生 Linux 文件系统进行，不在 Windows/VMware 共享目录直接 BitBake。

## 在线编辑原则

1. Codex 修改 Windows 工作区中的 RTL、XDC、Tcl、meta-user、Qt 源码和文档。
2. Ubuntu 通过 SSH 执行 PetaLinux 构建。
3. 生成的 `BOOT.BIN`/`image.ub`/应用程序通过 SCP 部署。
4. 板端重启后用串口或 SSH 验证。
5. 每次部署记录时间、XSA/bitstream 哈希和应用版本。

## 不应做的事

- 不直接修改 `.xpr/.xci` 或 PetaLinux 自动生成 DTS。
- 不在文档中写真实密码。
- 不同时启动多个 Qt 实例；程序已有单实例保护，但旧进程仍需通过 `ps`/`kill` 检查。
