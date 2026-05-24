#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd "$SCRIPT_DIR"

mkdir -p out

zig c++ src/main.cpp src/base.cpp src/stb_sprintf.cpp src/platform_posix.cpp src/platform_macos.cpp -std=c++20 -nostdinc++ -fno-exceptions -fno-rtti -O2 -s -target aarch64-macos-none -o out/blitter_macos
zig c++ src/main.cpp src/base.cpp src/stb_sprintf.cpp src/platform_posix.cpp src/platform_linux.cpp -std=c++20 -nostdinc++ -fno-exceptions -fno-rtti -O2 -s -target x86_64-linux-gnu -o out/blitter_linux
zig c++ src/main.cpp src/base.cpp src/stb_sprintf.cpp src/platform_windows.cpp -std=c++20 -nostdinc++ -fno-exceptions -fno-rtti -O2 -s -target x86_64-windows -o out/blitter_windows.exe

# zig c++ src/main.cpp src/base.cpp src/stb_sprintf.cpp src/platform_posix.cpp src/platform_macos.cpp -std=c++20 -nostdinc++ -fno-exceptions -fno-rtti -O0 -target aarch64-macos-none -o out/blitter_macos
# zig c++ src/main.cpp src/base.cpp src/stb_sprintf.cpp src/platform_posix.cpp src/platform_linux.cpp -std=c++20 -nostdinc++ -fno-exceptions -fno-rtti -O0 -target x86_64-linux-gnu -o out/blitter_linux
# zig c++ src/main.cpp src/base.cpp src/stb_sprintf.cpp src/platform_windows.cpp -std=c++20 -nostdinc++ -fno-exceptions -fno-rtti -O0 -target x86_64-windows -o out/blitter_windows.exe
