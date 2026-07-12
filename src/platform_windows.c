#include "platform.h"

// #include <process.h>

#include "base.h"

void *os_alloc(u64 size) {
    // TODO
    return calloc(size, 1);
}

void os_free(void *buf, u64 size) {
    // TODO
    free(buf);
}

Arr_u8 os_read_file(ErrorContext *err, Arena *arena, Str path) {
    // TODO
    // PLEASE WE'RE LEAKING
    Arena *scratch = arena_acquire();
    Scope scope = scope_open(err);

    char *path_cstr = str_to_c(scratch, path);

    size_t size = 0;
    void *ptr = SDL_LoadFile(path_cstr, &size);
    if (ptr == nullptr) {
        err_report(err, "%s", SDL_GetError());
    }
    Arr_u8 contents = {.ptr = ptr, .count = size};

    scope_close(scope, "Read file %.*s", SF(path));
    arena_release(scratch);

    return contents;
}

const SDL_GPUShaderFormat OS_SHADER_FORMAT = SDL_GPU_SHADERFORMAT_DXIL;
