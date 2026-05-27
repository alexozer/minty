#!/usr/bin/env bash
set -eou pipefail

cd "$( dirname -- "${BASH_SOURCE[0]}" )"

mkdir -p out
rm -f out/*.pdb

COMMON_ARGS="-std=c++20 -nostdinc++ -fno-exceptions -fno-rtti -Wall -Wshadow -Wconversion"

if [[ "${1:-}" == "debug" ]]; then
    PROFILE_ARGS="-O0"
elif [[ "${1:-}" == "release" || "${1:-}" == "" ]]; then
    PROFILE_ARGS="-O2 -s -Werror"
else
    echo "Invalid profile: $1"
    exit 1
fi

zig c++ src/main.cpp src/base.cpp src/platform_posix.cpp src/platform_macos.cpp $COMMON_ARGS $PROFILE_ARGS -target aarch64-macos-none -o out/blitter_macos
zig c++ src/main.cpp src/base.cpp src/platform_posix.cpp src/platform_linux.cpp $COMMON_ARGS $PROFILE_ARGS -target x86_64-linux-gnu -o out/blitter_linux
zig c++ src/main.cpp src/base.cpp src/platform_windows.cpp $COMMON_ARGS $PROFILE_ARGS -target x86_64-windows -o out/blitter_windows.exe
