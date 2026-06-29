#pragma once

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

//
// Primitives
//

typedef uint64_t u64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef float f32;
typedef double f64;

//
// Container macros
//

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

//
// Basic
//

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

#define kilobytes(n) ((n) * 1024)
#define megabytes(n) ((n) * kilobytes(n))

#define align_to(n, a) ((n) + (a - 1)) & ~(a - 1);
u64 next_pow2(u64 x);

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define c_arr_count(a) (sizeof((a)) / sizeof((a)[0]))

//
// Arenas
//

typedef struct Arena {
    void *data;
    u64 reserved;
    u64 offset;
} Arena;

// TODO use comptime alignment
void *arena__push_bytes(Arena *arena, u64 size, u64 alignment);

void arena_pool_init();
Arena *arena_acquire();
void arena_release(Arena *arena);

#define arena_push(arena, t) (t *)(arena__push_bytes(arena, sizeof(t), alignof(t)))

#define arena_push_arr(arena, t, c)                                        \
    ((CONCAT(Arr_, t)){                                                    \
        .ptr = (t *)(arena__push_bytes(arena, sizeof(t) * c, alignof(t))), \
        .count = c,                                                        \
    })

constexpr u64 MIN_VEC_CAPACITY = 8;

//
// Strings
//

[[noreturn]] void *oob();

// Sneaky array bounds checks in C
#define A(arr, idx)                                                                       \
    (*({                                                                                  \
        typeof(arr) *__arr__ = &(arr);                                                    \
        u64 __i__ = (u64)(idx);                                                           \
        __i__ < __arr__->count ? &__arr__->ptr[__i__] : (typeof(__arr__->ptr[0]) *)oob(); \
    }))

struct Str {
    u8 *ptr;
    u64 count;
};
derive_struct(Str);

#define STR_EMPTY ((Str){})

#define S(s) ((Str){.ptr = (u8 *)(s), .count = (sizeof(s)) - 1})
#define SF(s) (int)(s).count, (char *)(s).ptr
#define C(c) ((u8)(c))
#define ARR(a) ((Arr<u8>){.ptr = (a), .count = sizeof((a)) / sizeof((a)[0])})

Str str_from_c(const char *cstr);
Str str_from_c_len(const char *cstr);
bool char_is_whitespace(u8 c);
Str str_trim(Str s);
Str str_trim_prefix(Str s, Str prefix);
Str str_clone(Arena *arena, Str s);
bool str_eq(Str s1, Str s2);
bool str_starts_with(Str s, Str prefix);
__attribute__((format(printf, 2, 3))) Str str_format(Arena *arena, const char *format, ...);
Str str_format_v(Arena *arena, const char *format, va_list args);
bool str_is_valid_utf8(Arr_u8 s);
bool str_is_empty(Str s);
bool str_find(Str haystack, Str needle, u64 *pos);
bool str_contains(Str haystack, Str needle);
Str str_slice(Str s, u64 start, u64 end);

// Certainly possible to do this simply and w/o an iterator object, but just messin around
typedef struct StrLineIter {
    Str base;
    u64 pos;
} StrLineIter;

typedef struct Pair_Str {
    Str left;
    Str right;
} Pair_Str;

StrLineIter str_lines(Str s);
bool str_lines_next(StrLineIter *iter, Str *line);
u64 str_count_lines(Str s);
Pair_Str str_split2(Str base, u8 delim);

//
// Vec
//

// void vec_push(Arena *arena, Vec<T> *vec, T val) {
#define vec_push(arena, vec, val)         \
    ({                                    \
        (vec)->ptr[(vec)->count] = (val); \
        (vec)->count++;                   \
    })

// vec__grow(arena, vec, vec->count + 1);
#define vec_push_zero(arena, vec) (&(vec)->ptr[(vec)->count++])

#define vec_pop(vec)                  \
    ({                                \
        log_assert((vec)->count > 0); \
        (vec)->count--;               \
    })

// vec__grow(arena, vec, vec->count + arr.count);
#define vec_extend(arena, vec, arr)                            \
    ({                                                         \
        u64 start = (vec)->count;                              \
        (vec)->count += (arr).count;                           \
        auto a = arr_slice(vec_arr(vec), start, (vec)->count); \
        arr_copy(a, (arr));                                    \
    })

// template <typename T>
// Arr<T> vec_extend_zero(Arena *arena, Vec<T> *vec, u64 count) {
//     vec__grow(arena, vec, vec->count + count);
//
//     u64 start = vec->count;
//     vec->count += count;
//     Arr<T> a = arr_slice(vec_arr(vec), start, vec->count);
//
//     return a;
// }

#define vec_arr(vec, t) ((CONCAT(Arr_, t)){.ptr = (vec)->ptr, .count = (vec)->count})

// vec__grow(arena, vec, vec->count + count);
#define vec_extend_zero(arena, vec, t, count)              \
    ({                                                     \
        u64 start = (vec)->count;                          \
        (vec)->count += (count);                           \
        arr_slice(vec_arr(vec), start, (t), (vec)->count); \
    })

//
// template <typename T>
// void vec_prealloc(Arena *arena, Vec<T> *vec, u64 max_elems) {
//     vec__grow(arena, vec, max_elems);
// }
//
// template <typename T>
// Arr<T> vec_arr(Vec<T> *vec) {
//     return {.ptr = vec->ptr, .count = vec->count};
// }

//
// template <typename T>
// void vec_reset(Vec<T> *vec) {
//     vec->count = 0;
// }
//
// template <typename T>
// bool vec_is_empty(Vec<T> *vec) {
//     return vec->count == 0;
// }

//
// Logging
//

#define log_trace(...) SDL_LogTrace(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_debug(...) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_info(...) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_warn(...) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_error(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_fatal(...)                                          \
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__); \
    abort()

#define unreachable() log__assert("unreachable", __FILE_NAME__, __LINE__)

[[noreturn]] void log__assert(const char *cond, const char *file, int line);
#define log_assert(cond) \
    if ((cond) == false) log__assert(#cond, __FILE_NAME__, __LINE__)

//
// Idk
//

void thread_init();

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
