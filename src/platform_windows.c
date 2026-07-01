#include "platform.h"

#include <process.h>

#include "base.h"

void *os_alloc(u64 size) {
    return calloc(size, 1);
}

void os_free(void *buf, u64 size) {
    free(buf);
}

Arr_u8 os_read_file(ErrorContext *err, Arena *arena, Str path) {
    log_fatal("TODO");
}

static u8 VERT_SHADER[] = {
#embed "src/shaders/vert.dxil"
};

static u8 FRAG_ICON_SHADER[] = {
#embed "src/shaders/frag_icon.dxil"
};

static u8 FRAG_GLYPH_SHADER[] = {
#embed "src/shaders/frag_icon.dxil"
};

OS_Shaders OS_SHADERS = {
    .format = SDL_GPU_SHADERFORMAT_DXIL,
    .vert_shader = ARR(VERT_SHADER),
    .frag_icon_shader = ARR(FRAG_ICON_SHADER),
    .frag_glyph_shader = ARR(FRAG_GLYPH_SHADER),
};
