#pragma once

#include "base.h"

#include <SDL3/SDL_gpu.h>

void *os_alloc(u64 size);
void os_free(void *buf, u64 size);
Arr_u8 os_read_file(ErrorContext *err, Arena *arena, Str path);

extern const SDL_GPUShaderFormat OS_SHADER_FORMAT;

// Compiled shaders linked in as C arrays by build system
// Names are: `os_shader_{shader_source_filename_without_ext}`

extern u8 os_shader_vert[];
extern unsigned int os_shader_vert_len;

extern u8 os_shader_frag_icon[];
extern unsigned int os_shader_frag_icon_len;

extern u8 os_shader_frag_glyph[];
extern unsigned int os_shader_frag_glyph_len;
