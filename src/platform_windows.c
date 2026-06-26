#include "platform.h"

#include <process.h>

#include "base.h"

void *os_alloc(u64 size) {
    return calloc(size, 1);
}

void os_free(void *buf, u64 size) {
    free(buf);
}

const SDL_GPUShaderFormat OS_SHADER_FORMAT = SDL_GPU_SHADERFORMAT_DXIL;
const Str OS_SHADER_EXTENSION = S("dxil");
