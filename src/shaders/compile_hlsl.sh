#!/usr/bin/env bash
set -euo pipefail
trap 's=$?; echo >&2 "$0: Error on line "$LINENO": $BASH_COMMAND"; exit $s' ERR

dxc -T vs_6_0 vert.hlsl -Fo vert.bin
dxc -T ps_6_0 frag_icon.hlsl -Fo frag_icon.bin
dxc -T ps_6_0 frag_glyph.hlsl -Fo frag_glyph.bin
