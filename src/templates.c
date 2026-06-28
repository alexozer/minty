#include "templates.h"
#include "bootstrap.h"

// BEGIN_TEMPLATE_FUNCTION vec__grow
void vec__grow_TYPE(Arena *arena, Vec_TYPE *vec, u64 new_count) {
    if (new_count <= vec->capacity) return;

    u64 new_cap = max(MIN_VEC_CAPACITY, next_pow2(new_count));
    void *new_ptr = arena__push_bytes(arena, new_cap * sizeof(TYPE), alignof(TYPE));
    SDL_memcpy(new_ptr, vec->ptr, vec->count * sizeof(TYPE));
    vec->ptr = new_ptr;
    vec->capacity = new_cap;
}
// END_TEMPLATE_FUNCTION

#define vec__grow(arena, vec, new_count) \
    _Generic((vec), u64: vec__grow_u64, Str: vec__grow_Str, )((arena), (vec), (new_count))
