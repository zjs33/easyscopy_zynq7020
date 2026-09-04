#!/usr/bin/env sh
set -eu

app="${AC880_SCOPE_APP:-/usr/local/bin/ac880_zynq_scope}"
dma_device="${AC880_CAPTURE_DEVICE:-/dev/ac880_capture_dma}"

if [ ! -x "${app}" ]; then
    echo "找不到可执行程序：${app}" >&2
    exit 1
fi
if [ ! -e "${dma_device}" ]; then
    echo "找不到 DMA 设备：${dma_device}" >&2
    exit 1
fi

# Qt5 eglfs 使用 DRM/KMS 负责 /dev/dri/card0 的 LCD 输出；若系统镜像
# 使用了定制 QPA，可在启动前覆盖 QT_QPA_PLATFORM。
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-eglfs}"
export QT_QPA_EGLFS_INTEGRATION="${QT_QPA_EGLFS_INTEGRATION:-eglfs_kms}"
export QT_QPA_FONTDIR="${QT_QPA_FONTDIR:-/usr/share/fonts}"
export ZYNQ_SCOPE_FULLSCREEN="${ZYNQ_SCOPE_FULLSCREEN:-1}"
export ZYNQ_SCOPE_TOUCH="${ZYNQ_SCOPE_TOUCH:-1}"
export ZYNQ_SCOPE_LOW_POWER="${ZYNQ_SCOPE_LOW_POWER:-1}"
export ZYNQ_SCOPE_RENDER_FPS="${ZYNQ_SCOPE_RENDER_FPS:-30}"

exec "${app}" --dma-device "${dma_device}" --fullscreen "$@"

