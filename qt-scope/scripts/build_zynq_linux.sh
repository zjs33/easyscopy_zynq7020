#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_root}/build-zynq-linux"

: "${AC880_QT5_PREFIX:?请先设置 AC880_QT5_PREFIX，例如 /opt/qt5-zynq}"
: "${CXX:?请先设置交叉 C++ 编译器，例如 arm-linux-gnueabihf-g++}"
: "${CC:?请先设置交叉 C 编译器，例如 arm-linux-gnueabihf-gcc}"

cmake_args=(
    -S "${project_root}"
    -B "${build_dir}"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTING=OFF
    -DZYNQ_SCOPE_FORCE_QT5=ON
    -DZYNQ_SCOPE_QT5_VERSION=5.12.8
    -DCMAKE_PREFIX_PATH="${AC880_QT5_PREFIX}"
)

if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    cmake_args+=("-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
fi

cmake "${cmake_args[@]}"
cmake --build "${build_dir}" --parallel "${JOBS:-2}"

