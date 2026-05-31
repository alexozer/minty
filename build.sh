#!/usr/bin/env bash
set -eou pipefail

cd "$( dirname -- "${BASH_SOURCE[0]}" )"

mkdir -p out
rm -f out/*.pdb

C_FLAGS="-std=c99 -Wall -Wshadow"
CPP_FLAGS="-std=c++20 -nostdinc++ -fno-exceptions -fno-rtti -Wall -Wshadow -Wconversion"

if [[ "${1:-}" == "debug" ]]; then
    PROFILE_FLAGS="-g -O0"
elif [[ "${1:-}" == "release" || "${1:-}" == "" ]]; then
    # TODO strip
    PROFILE_FLAGS="-O2 -Werror -UNDEBUG"
else
    echo "Invalid profile: $1"
    exit 1
fi

mkdir -p out/aarch64-macos-none
mkdir -p out/x86_64-linux-gnu
mkdir -p out/x86_64-windows

zig cc \
    $C_FLAGS -c $PROFILE_FLAGS \
    -target aarch64-macos-none \
    src/xao.c -o out/aarch64-macos-none/xao.o
# zig cc \
#     $C_FLAGS -c -O2 \
#     -target x86_64-linux-gnu \
#     src/xao.c -o out/x86_64-linux-gnu/xao.o
# zig cc \
#     $C_FLAGS -c -O2 \
#     -target x86_64-windows \
#     src/xao.c -o out/x86_64-windows/xao.o

zig c++ \
    $CPP_FLAGS $PROFILE_FLAGS \
    -target aarch64-macos-none \
    src/main.cpp \
    src/base.cpp \
    src/platform_posix.cpp \
    src/platform_macos.cpp \
    out/aarch64-macos-none/xao.o \
    -o out/aarch64-macos-none/blitter
# zig c++ \
#     $CPP_FLAGS -O2 \
#     -target x86_64-linux-gnu \
#     src/main.cpp \
#     src/base.cpp \
#     src/platform_posix.cpp \
#     src/platform_linux.cpp \
#     out/x86_64-linux-gnu/xao.o \
#     -o out/x86_64-linux-gnu/blitter
# zig c++ \
#     $CPP_FLAGS -O2 \
#     -target x86_64-windows \
#     src/main.cpp \
#     src/base.cpp \
#     src/platform_windows.cpp \
#     out/x86_64-windows/xao.o \
#     -o out/x86_64-windows/blitter
