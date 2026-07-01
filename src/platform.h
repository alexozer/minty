#pragma once

#include "base.h"

#include <SDL3/SDL_gpu.h>

void *os_alloc(u64 size);
void os_free(void *buf, u64 size);
Arr_u8 os_read_file(ErrorContext *err, Arena *arena, Str path);

extern const SDL_GPUShaderFormat OS_SHADER_FORMAT;
extern const Str OS_SHADER_EXTENSION;
