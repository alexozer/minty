//
// GENERATED FILE - DO NOT MODIFY
// Generated from template 'src/templates.c', modify this instead
//

#pragma once

#include "bootstrap.h"
#include "generated.h"

void vec__grow_u64(Arena *arena, Vec_u64 *vec, u64 new_count) {
    if (new_count <= vec->capacity) return;

    u64 new_cap = max(MIN_VEC_CAPACITY, next_pow2(new_count));
    void *new_ptr = arena__push_bytes(arena, new_cap * sizeof(u64), alignof(u64));
    SDL_memcpy(new_ptr, vec->ptr, vec->count * sizeof(u64));
    vec->ptr = new_ptr;
    vec->capacity = new_cap;
}

void vec__grow_Str(Arena *arena, Vec_Str *vec, u64 new_count) {
    if (new_count <= vec->capacity) return;

    u64 new_cap = max(MIN_VEC_CAPACITY, next_pow2(new_count));
    void *new_ptr = arena__push_bytes(arena, new_cap * sizeof(Str), alignof(Str));
    SDL_memcpy(new_ptr, vec->ptr, vec->count * sizeof(Str));
    vec->ptr = new_ptr;
    vec->capacity = new_cap;
}

#define vec__grow(arg0, arg1, arg2) _Generic((arg1), \
    u64: vec__grow_u64, \
    Str: vec__grow_Str \
)(arg0, arg1, arg2)

