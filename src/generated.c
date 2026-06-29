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

#define vec__grow(arena, vec, new_count) _Generic((vec), \
    u64: vec__grow_u64, \
    Str: vec__grow_Str \
)(arena, vec, new_count)

Opt_u64 some_u64(u64 value) {
    return (Opt_u64){.present = true, .opt = value};
}

Opt_Str some_Str(Str value) {
    return (Opt_Str){.present = true, .opt = value};
}

#define some(value) _Generic((value), \
    u64: some_u64, \
    Str: some_Str \
)(value)

