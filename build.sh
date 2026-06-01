#!/usr/bin/env bash
set -euo pipefail
trap 's=$?; echo >&2 "$0: Error on line "$LINENO": $BASH_COMMAND"; exit $s' ERR

# simdutf doesn't link standard library, but DOES include std headers, and Zig's
# -nostdinc++ strictly prevents this, so not using `-fnostdlib++` for now
C_FLAGS="-std=c99 -Wall -Wshadow -DYYJSON_DISABLE_INCR_READER -DYYJSON_DISABLE_UTILS -DYYJSON_DISABLE_FAST_FP_CONV -DYYJSON_DISABLE_NON_STANDARD"
CPP_FLAGS="-std=c++20 -fno-exceptions -fno-rtti -Wall -Wshadow -isystem 3rdparty"

SIMDUTF_FLAGS="-std=c++20 -fno-exceptions -fno-rtti -DSIMDUTF_NO_LIBCXX=1"

cd "$( dirname -- "${BASH_SOURCE[0]}" )"

if [[ "${1:-}" == "debug" ]]; then
    PROFILE_FLAGS="-g -O0"
elif [[ "${1:-}" == "release" || "${1:-}" == "" ]]; then
    # TODO strip
    PROFILE_FLAGS="-O2 -UNDEBUG"
else
    echo "Invalid profile: $1"
    exit 1
fi

rm -rf out
mkdir -p out/aarch64-macos-none
mkdir -p out/x86_64-linux-gnu
mkdir -p out/x86_64-windows

zig cc \
    -c $C_FLAGS $PROFILE_FLAGS \
    -target aarch64-macos-none \
    src/xao.c \
    -o out/aarch64-macos-none/xao.o &
zic cc \
    -c $C_FLAGS $PROFILE_FLAGS \
    -target aarch64-macos-none \
    3rdparty/yyjson.c \
    -o out/aarch64-macos-none/yyjson.o &
zig c++ \
    -c $SIMDUTF_FLAGS $PROFILE_FLAGS \
    -target aarch64-macos-none \
    3rdparty/simdutf.cpp \
    -o out/aarch64-macos-none/simdutf.o &
zig c++ \
    -c $CPP_FLAGS $PROFILE_FLAGS \
    src/main.cpp \
    src/base.cpp \
    src/platform_posix.cpp \
    src/platform_macos.cpp \
    -o out/aarch64-macos-none/blitter.o &
wait
zig c++ \
    -nostdinc++ $CPP_FLAGS $PROFILE_FLAGS \
    out/aarch64-macos-none/*.o \
    -o out/aarch64-macos-none/blitter
