#pragma once

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

#define derive_containers(name)         \
    typedef struct CONCAT(Arr_, name) { \
        name *ptr;                      \
        u64 count;                      \
    } CONCAT(Arr_, name);               \
                                        \
    typedef struct CONCAT(Vec_, name) { \
        name *ptr;                      \
        u64 count;                      \
        u64 capacity;                   \
    } CONCAT(Vec_, name);               \
                                        \
    typedef struct CONCAT(Opt_, name) { \
        bool present;                   \
        name opt;                       \
    } CONCAT(Opt_, name)

#define derive_primitive(name) derive_containers(name)

// In the rare case of recursive types
#define derive_struct_pre(name)     \
    typedef struct name name;       \
    typedef name *CONCAT(P_, name); \
    derive_containers(CONCAT(P_, name))

#define derive_struct_post(name) derive_containers(name)

#define derive_struct(name)  \
    derive_struct_pre(name); \
    derive_struct_post(name)

#define derive_enum(name)   \
    typedef enum name name; \
    derive_containers(name)

derive_primitive(u8);
derive_primitive(u16);
derive_primitive(u32);
derive_primitive(u64);
derive_primitive(i8);
derive_primitive(i16);
derive_primitive(i32);
derive_primitive(i64);
derive_primitive(f32);
derive_primitive(f64);

//
// Arrays
//

#define c_arr_count(a) (sizeof((a)) / sizeof((a)[0]))

#define arr_eq(a, b) \
    ((a).count == (b).count && SDL_memcmp((a).ptr, (b).ptr, (a).count * sizeof(*(a).ptr)) == 0)

#define arr_slice(arr, start, end)      \
    ({                                  \
        log_assert(start <= arr.count); \
        log_assert(end <= arr.count);   \
        log_assert(start <= end);       \
        ((typeof(arr)){                 \
            .ptr = arr.ptr + start,     \
            .count = end - start,       \
        })                              \
    })

#define arr_last(v) A(v, ((v).count - 1))

// For non-overlapping arrays
#define arr_copy(dest, source)                                                          \
    ({                                                                                  \
        log_assert((dest).count == (source).count);                                     \
        if ((dest).count > 0) {                                                         \
            SDL_memcpy((dest).ptr, (source).ptr, (dest).count * sizeof(*((dest).ptr))); \
        }                                                                               \
    })

// For potentially overlapping arrays
#define arr_move(dest, source)                                                           \
    ({                                                                                   \
        log_assert((dest).count == (source).count);                                      \
        if ((dest).count > 0) {                                                          \
            SDL_memmove((dest).ptr, (source).ptr, (dest).count * sizeof(*((dest).ptr))); \
        }                                                                                \
    })

#define arr_is_empty(arr) ((arr).count == 0)

#define arr_clone(arena, arr)                                                          \
    ({                                                                                 \
        typeof(arr) clone = arena_push_arr(arena, typeof(*((arr).ptr)), (arr).count)); \
        arr_copy(clone, (arr));                                                        \
        clone;                                                                         \
    })

//
// Option
//

// TODO codegen to avoid passing type
#define some(v, t) ((CONCAT(Opt_, t)){.opt = (v), .present = true})

//
// Duration
//

// Monotonic nanoseconds
typedef i64 Duration;
derive_primitive(Duration);

struct Instant {
    // Monotonic nanoseconds starting at program start
    i64 time_nanoseconds;
};
derive_struct(Instant);

inline static Duration instant_sub(Instant a, Instant b) {
    return a.time_nanoseconds - b.time_nanoseconds;
}

Instant get_current_monotonic_time();
Instant instant_from_sdl_nanos(u64 nanos);

// TODO memset?
#define vec_reset(v) (v)->count = 0

//
// Errors
//

struct ErrorContext {
    Arena *arena;
    Vec_Str ctx_stack;
};
derive_struct(ErrorContext);

struct Scope {
    ErrorContext *err;
    u64 last_err_stack_pos;
};
derive_struct(Scope);

Scope scope_open(ErrorContext *err);
__attribute__((format(printf, 2, 3))) void err_report(ErrorContext *err, const char *format, ...);
__attribute__((format(printf, 2, 3))) void scope_close(Scope scope, const char *format, ...);
bool err_occurred(ErrorContext *err);

//
// Encoding/Decoding
//

Arr_u8 decode_base64(ErrorContext *err, Arena *arena, Str s);
u64 parse_u64(ErrorContext *err, Str s);

//
// FS
//

Arr_u8 fs_load_file(ErrorContext *err, Arena *arena, Str path);

//
// IO
//

SDL_IOStream *sdl_io_from_mem(ErrorContext *err, Arr_u8 buf);
void sdl_close_io(SDL_IOStream *stream);
