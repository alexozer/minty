#!/usr/bin/env bash
set -euo pipefail
trap 's=$?; echo >&2 "$0: Error on line "$LINENO": $BASH_COMMAND"; exit $s' ERR

cd "$(dirname "$0")"

shadercross vert.hlsl --source hlsl --stage vertex --dest DXIL --output vert.dxil
shadercross vert.hlsl --source hlsl --stage vertex --dest MSL --output vert.msl
shadercross vert.hlsl --source hlsl --stage vertex --dest SPIRV --output vert.spv

shadercross frag_icon.hlsl --source hlsl --stage fragment --dest DXIL --output frag_icon.dxil
shadercross frag_icon.hlsl --source hlsl --stage fragment --dest MSL --output frag_icon.msl
shadercross frag_icon.hlsl --source hlsl --stage fragment --dest SPIRV --output frag_icon.spv

shadercross frag_glyph.hlsl --source hlsl --stage fragment --dest DXIL --output frag_glyph.dxil
shadercross frag_glyph.hlsl --source hlsl --stage fragment --dest MSL --output frag_glyph.msl
shadercross frag_glyph.hlsl --source hlsl --stage fragment --dest SPIRV --output frag_glyph.spv
