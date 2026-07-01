#pragma once

#include "base.h"

#include <SDL3/SDL_gpu.h>

void *os_alloc(u64 size);
void os_free(void *buf, u64 size);
Arr_u8 os_read_file(ErrorContext *err, Arena *arena, Str path);

struct OS_Shaders {
    SDL_GPUShaderFormat format;
    Arr_u8 vert_shader;
    Arr_u8 frag_icon_shader;
    Arr_u8 frag_glyph_shader;
};
derive_struct(OS_Shaders);

extern OS_Shaders OS_SHADERS;
