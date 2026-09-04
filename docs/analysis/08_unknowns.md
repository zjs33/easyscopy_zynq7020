# 08 待确认信息

- 当前板卡 PL DDR3 是否在 V1 运行镜像中完全断开：工程 BD 使用 PS DDR 作为 DMA 目标，但 PL DDR 参考工程仍存在。
- ACM108 ADC 的最终模拟量程、零点和 volts-per-code 标定值；Qt 当前默认换算只是软件默认值。
- Ubuntu 当前实际 PetaLinux 工具安装路径、最终构建 commit 和生成 DTB 中的完整 IRQ 数字。
- 当前板端运行的 Qt 可执行文件是否来自 `_qt_scope_ref_20260903_v2` 最新源码。
- LCD 实际 QPA 后端是 linuxfb 还是 eglfs/DRM-KMS，需以启动环境和 `QT_QPA_PLATFORM` 实测为准。
- DAC 输出的外部模拟幅度、频率误差和连接器电气时序。
- GT911 的最终 reset/interrupt 电平及其与当前 XSA GPIO EMIO 配置的一致性，应以最终 DTB 和 `/proc/interrupts` 实测闭环确认。
- PL 侧实际综合使用的 BRAM 数量应以 Vivado utilization 报告为准，不能由“存在 FIFO”推断容量。
