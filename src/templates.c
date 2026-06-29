#include "bootstrap.h"

// "Real Type"
typedef struct RTYPE {
} RTYPE;

// BEGIN_TEMPLATE_STRUCT Arr
typedef struct Arr_ITYPE {
    RTYPE *ptr;
    u64 count;
} Arr_ITYPE;
// END_TEMPLATE_STRUCT

// BEGIN_TEMPLATE_STRUCT Vec
typedef struct Vec_ITYPE {
    RTYPE *ptr;
    u64 count;
    u64 capacity;
} Vec_ITYPE;
// END_TEMPLATE_STRUCT

// BEGIN_TEMPLATE_STRUCT Opt
typedef struct Opt_ITYPE {
    RTYPE opt;
    bool present;
} Opt_ITYPE;
// END_TEMPLATE_STRUCT

// BEGIN_TEMPLATE_FUNCTION vec__grow
void vec__grow_ITYPE(Arena *arena, Vec_ITYPE *vec, u64 new_count) {
    if (new_count <= vec->capacity) return;

    u64 new_cap = max(MIN_VEC_CAPACITY, next_pow2(new_count));
    void *new_ptr = arena__push_bytes(arena, new_cap * sizeof(RTYPE), alignof(RTYPE));
    SDL_memcpy(new_ptr, vec->ptr, vec->count * sizeof(RTYPE));
    vec->ptr = new_ptr;
    vec->capacity = new_cap;
}
// END_TEMPLATE_FUNCTION

// BEGIN_TEMPLATE_FUNCTION some
Opt_ITYPE some_ITYPE(RTYPE value) {
    return (Opt_ITYPE){.present = true, .opt = value};
}
// END_TEMPLATE_FUNCTION
