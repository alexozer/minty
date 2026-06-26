#pragma once

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

//
// Math
//

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef float f32;
typedef double f64;

#define kilobytes(n) ((n) * 1024)
#define megabytes(n) ((n) * kilobytes(n))

constexpr u64 DEFAULT_ALIGNMENT = 8;

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

[[noreturn]] void log__assert(const char *cond, const char *file, int line);
#define log_assert(cond) \
    if ((cond) == false) log__assert(#cond, __FILE_NAME__, __LINE__)
#define unreachable() log__assert("unreachable", __FILE_NAME__, __LINE__)

//
// Arenas
//

typedef struct {
    void *data;
    u64 reserved;
    u64 offset;
} Arena;

// TODO use comptime alignment
void *arena__push_bytes(Arena *arena, u64 size, u64 alignment);

void arena_pool_init();
Arena *arena_acquire();
void arena_release(Arena *arena);

#define arena_push_arr(arena, t, count) sizeof(*(t){}.ptr)

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

typedef struct {
    u8 *ptr;
    u64 count;
} Str;

#define STR_EMPTY ((Str){})

#define c_arr_count(a) (sizeof((a)) / sizeof((a)[0]))
// #define arr_from_c(a) (COJ){.ptr = (a), .count = c_arr_count(a)})

#define S(s) ((Str){.ptr = (u8 *)(s), .count = (sizeof(s)) - 1})
#define SF(s) (int)(s).count, (char *)(s).ptr
#define C(c) ((u8)(c))
#define ARR(a) ((Arr<u8>){.ptr = (a), .count = sizeof((a)) / sizeof((a)[0])})

char *str_to_c(Arena *arena, Str s);
Str str_from_c(const char *cstr);
Str str_from_c_len(const char *cstr);
bool char_is_whitespace(u8 c);
Str str_trim(Str s);
Str str_clone(Arena *arena, Str s);
bool str_eq(Str s1, Str s2);
bool str_starts_with(Str s, Str prefix);
__attribute__((format(printf, 2, 3))) Str str_format(Arena *arena, const char *format, ...);
Str str_format_v(Arena *arena, const char *format, va_list args);
bool str_is_valid_utf8(Str s);
bool str_find(Str haystack, Str needle, u64 *pos);
Str str_slice(Str s, u64 start, u64 end);

// Certainly possible to do this simply and w/o an iterator object, but just messin around
typedef struct {
    Str base;
    u64 pos;
} StrLineIter;

StrLineIter str_lines(Str s);
bool str_lines_next(StrLineIter *iter, Str *line);
u64 str_count_lines(Str s);
bool str_split2(Str base, u8 delim, Str *left, Str *right);

//
// Arrays
//

#define arr_eq(a, b) \
    ((a).count == (b).count && SDL_memcmp((a).ptr, (b).ptr, (a).count * sizeof(*(a).ptr)) == 0)

#define vec_last(v) A(v, (v).count - 1)

//
// Encoding/Decoding
//

bool parse_u64(Str s, u64 *out);

//
// FS
//

// Arr<u8> fs_load_file(ErrorContext* err, Arena* arena, Str path);

//
// Duration
//

// Monotonic nanoseconds
typedef i64 Duration;
typedef struct {
    // Monotonic nanoseconds starting at program start
    i64 time_nanoseconds;
} Instant;
inline static Duration instant_sub(Instant a, Instant b) {
    return a.time_nanoseconds - b.time_nanoseconds;
}

// TODO memset?
#define vec_reset(v) (v)->count = 0
