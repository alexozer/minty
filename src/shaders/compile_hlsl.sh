#!/usr/bin/env bash
set -euo pipefail
trap 's=$?; echo >&2 "$0: Error on line "$LINENO": $BASH_COMMAND"; exit $s' ERR

cd "$(dirname "$0")"

dxc -T vs_6_0 vert.hlsl -Fo vert.dxil
dxc -T ps_6_0 frag_icon.hlsl -Fo frag_icon.dxil
dxc -T ps_6_0 frag_glyph.hlsl -Fo frag_glyph.dxil

dxc -T vs_6_0 -spirv -fspv-target-env=vulkan1.3 vert.hlsl -Fo vert.spv
dxc -T ps_6_0 -spirv -fspv-target-env=vulkan1.3 frag_icon.hlsl -Fo frag_icon.dxil
dxc -T ps_6_0 -spirv -fspv-target-env=vulkan1.3 frag_glyph.hlsl -Fo frag_glyph.dxil
