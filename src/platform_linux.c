#include "platform.h"

static u8 VERT_SHADER[] = {
#embed "src/shaders/vert.spv"
};

static u8 FRAG_ICON_SHADER[] = {
#embed "src/shaders/frag_icon.spv"
};

static u8 FRAG_GLYPH_SHADER[] = {
#embed "src/shaders/frag_icon.spv"
};

OS_Shaders OS_SHADERS = {
    .format = SDL_GPU_SHADERFORMAT_SPIRV,
    .vert_shader = ARR(VERT_SHADER),
    .frag_icon_shader = ARR(FRAG_ICON_SHADER),
    .frag_glyph_shader = ARR(FRAG_GLYPH_SHADER),
};
