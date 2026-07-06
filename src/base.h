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

#define derive_containers(name)                       \
    typedef struct CONCAT(Arr_, name) {               \
        u64 count;                                    \
        name *ptr __attribute__((counted_by(count))); \
    } CONCAT(Arr_, name);                             \
                                                      \
    typedef struct CONCAT(Vec_, name) {               \
        u64 count;                                    \
        name *ptr __attribute__((counted_by(count))); \
        u64 capacity;                                 \
        void *__typeid_vec[0];                        \
        CONCAT(Arr_, name) __to_arr[0];               \
    } CONCAT(Vec_, name);                             \
                                                      \
    typedef struct CONCAT(FixedVec_, name) {          \
        u64 count;                                    \
        name *ptr __attribute__((counted_by(count))); \
        u64 capacity;                                 \
        void *__typeid_fixed_vec[0];                  \
        CONCAT(Arr_, name) __to_arr[0];               \
    } CONCAT(FixedVec_, name);                        \
                                                      \
    typedef struct CONCAT(Opt_, name) {               \
        bool present;                                 \
        name opt;                                     \
    } CONCAT(Opt_, name)

#define derive_type(name) derive_containers(name)

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

#define derive_union(name)   \
    typedef union name name; \
    derive_containers(name)

//
// Basic
//

derive_type(u8);
derive_type(u16);
derive_type(u32);
derive_type(u64);
derive_type(i8);
derive_type(i16);
derive_type(i32);
derive_type(i64);
derive_type(f32);
derive_type(f64);

#define kilobytes(n) (1024 * (n))
#define megabytes(n) (1024 * kilobytes(n))

#define align_to(n, a) ((n) + (a - 1)) & ~(a - 1);
u64 next_pow2(u64 x);

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define c_arr_count(a) (sizeof((a)) / sizeof((a)[0]))

#define bit(x) (1 << (x))

//
// Arenas
//

struct Arena {
    u64 offset;
    u8 *data __attribute__((counted_by(offset)));
    u64 reserved;
};
derive_struct(Arena);

// TODO use comptime alignment
void *arena_push_bytes(Arena *arena, u64 size, u64 alignment);
void *arena_realloc_bytes(Arena *arena, void *ptr, u64 old_size, u64 new_size, u64 alignment);

void arena_pool_init();
Arena *arena_acquire();
void arena_release(Arena *arena);

#define arena_push(arena, t) (t *)(arena_push_bytes((arena), sizeof(t), alignof(t)))

#define arena_push_arr(arena, t, c)                                               \
    ({                                                                            \
        typeof(arena) _arena_ = (arena);                                          \
        u64 _c_ = (c);                                                            \
        ((CONCAT(Arr_, t)){                                                       \
            .ptr = (t *)(arena_push_bytes(_arena_, sizeof(t) * _c_, alignof(t))), \
            .count = _c_,                                                         \
        });                                                                       \
    })

constexpr u64 MIN_VEC_CAPACITY = 8;

//
// Logging
//

[[noreturn]] void crash(const char *why);

#define log_trace(...) SDL_LogTrace(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_debug(...) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_info(...) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_warn(...) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_error(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_fatal(...)                                          \
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__); \
    abort()

#define log_assert(cond) \
    if ((cond) == false) crash("assertion failed")

#define log_unreachable() crash("assertion failed: unreachable")

//
// Strings
//

[[noreturn]] void *oob();

// Sneaky array bounds checks in C
#define A(arr, idx)                                                                       \
    (*({                                                                                  \
        typeof(arr) *_A_arr_ = &(arr);                                                    \
        u64 _A_i_ = (u64)(idx);                                                           \
        _A_i_ < _A_arr_->count ? &_A_arr_->ptr[_A_i_] : (typeof(_A_arr_->ptr[0]) *)oob(); \
    }))

typedef Arr_u8 Str;
derive_type(Str);

#define S(s) ((Str){.ptr = (u8 *)(s), .count = (sizeof(s)) - 1})
#define SF(s) (int)(s).count, (char *)(s).ptr
#define C(c) ((u8)(c))
#define ARR(a) ((Arr_u8){.ptr = (a), .count = sizeof((a)) / sizeof((a)[0])})

Str str_from_c(const char *cstr);
char *str_to_c(Arena *arena, Str str);
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
Opt_u64 str_find(Str haystack, Str needle);
bool str_contains(Str haystack, Str needle);
Str str_slice(Str s, u64 start, u64 end);

// Certainly possible to do this simply and w/o an iterator object, but just messin around
struct StrLineIter {
    Str base;
    u64 pos;
};
derive_struct(StrLineIter);

struct StrPair {
    Str left;
    Str right;
};
derive_struct(StrPair);

StrLineIter str_lines(Str s);
bool str_lines_next(StrLineIter *iter, Str *line);
u64 str_count_lines(Str s);
StrPair str_split2(Str base, u8 delim);

//
// Arrays
//

#define is_empty(arr) ((arr).count == 0)

#define c_arr_count(a) (sizeof((a)) / sizeof((a)[0]))

#define arr_eq(a, b)                                                      \
    ({                                                                    \
        typeof(a) _a_ = (a);                                              \
        typeof(b) _b_ = (b);                                              \
        (_a_.count == _b_.count &&                                        \
         SDL_memcmp(_a_.ptr, _b_.ptr, _a_.count * sizeof(*_a_.ptr)) == 0) \
    })

#define arr_slice(arr, start, end)          \
    ({                                      \
        typeof(arr) _arr_ = (arr);          \
        u64 _start_ = (start);              \
        u64 _end_ = (end);                  \
        log_assert(_start_ <= _arr_.count); \
        log_assert(_end_ <= _arr_.count);   \
        log_assert(_start_ <= _end_);       \
        ((typeof(_arr_)){                   \
            .ptr = _arr_.ptr + _start_,     \
            .count = _end_ - _start_,       \
        });                                 \
    })

// #define arr_last(v) A(v, ((v).count - 1))

// For non-overlapping arrays
#define arr_copy(dest, source)                                                          \
    ({                                                                                  \
        typeof(dest) _dest_ = (dest);                                                   \
        typeof(dest) _source_ = (source);                                               \
        log_assert(_dest_.count == _source_.count);                                     \
        if (_dest_.count > 0) {                                                         \
            SDL_memcpy(_dest_.ptr, _source_.ptr, _dest_.count * sizeof(*(_dest_.ptr))); \
        }                                                                               \
    })

// For potentially overlapping arrays
#define arr_move(dest, source)                                                           \
    ({                                                                                   \
        typeof(dest) _dest_ = (dest);                                                    \
        typeof(dest) _source_ = (source);                                                \
        log_assert(_dest_.count == _source_.count);                                      \
        if (_dest_.count > 0) {                                                          \
            SDL_memmove(_dest_.ptr, _source_.ptr, _dest_.count * sizeof(*(_dest_.ptr))); \
        }                                                                                \
    })

#define arr_clone(arena, arr)                                                       \
    ({                                                                              \
        typeof(arena) _arena_ = (arena);                                            \
        typeof(arr) _arr_ = (arr);                                                  \
        void *new_ptr = arena_push_bytes(_arena_, _arr_.count * sizeof(*_arr_.ptr), \
                                         alignof(typeof(*_arr_.ptr)));              \
        SDL_memcpy(new_ptr, _arr_.ptr, _arr_.count * sizeof(*_arr_.ptr));           \
        (typeof(_arr_)){.ptr = new_ptr, .count = _arr_.count};                      \
    })

#define arr_sort(arr, compare)                                                                 \
    ({                                                                                         \
        typeof(arr) _arr_ = (arr);                                                             \
        int (*_compare_)(const typeof(*_arr_.ptr) *, const typeof(*_arr_.ptr) *) = (compare);  \
        SDL_qsort(_arr_.ptr, _arr_.count, sizeof(*_arr_.ptr), (SDL_CompareCallback)_compare_); \
    })

//
// Vec
//

struct GenericVec {
    u64 count;
    void *ptr;
    u64 capacity;
};
derive_struct(GenericVec);

void vec__grow(Arena *arena, GenericVec *vec, u64 elem_size, u64 elem_align, u64 new_count);

#define vec_push(arena, vec, val)                                        \
    ({                                                                   \
        typeof(arena) _arena_ = (arena);                                 \
        typeof(vec) _vec_ = (vec);                                       \
        typeof(val) _val_ = (val);                                       \
        _vec_->__typeid_vec;                                             \
        if (_vec_->count == _vec_->capacity) {                           \
            vec__grow(_arena_, (GenericVec *)_vec_, sizeof(*_vec_->ptr), \
                      alignof(typeof(*_vec_->ptr)), _vec_->count + 1);   \
        }                                                                \
        _vec_->count++;                                                  \
        _vec_->ptr[_vec_->count - 1] = _val_;                            \
    })

#define vec_push_zero(arena, vec)                                        \
    ({                                                                   \
        typeof(arena) _arena_ = (arena);                                 \
        typeof(vec) _vec_ = (vec);                                       \
        _vec_->__typeid_vec;                                             \
        if (_vec_->count == _vec_->capacity) {                           \
            vec__grow(_arena_, (GenericVec *)_vec_, sizeof(*_vec_->ptr), \
                      alignof(typeof(*_vec_->ptr)), _vec_->count + 1);   \
        }                                                                \
        _vec_->count++;                                                  \
        (&_vec_->ptr[_vec_->count - 1]);                                 \
    })

#define vec_pop(vec)                                            \
    ({                                                          \
        typeof(vec) _vec_ = (vec);                              \
        _vec_->__typeid_vec;                                    \
        log_assert(_vec_->count > 0);                           \
        _vec_->ptr[_vec_->count - 1] = (typeof(*_vec_->ptr)){}; \
        _vec_->count--;                                         \
    })

#define vec_extend(arena, vec, arr)                                              \
    ({                                                                           \
        typeof(arena) _arena_ = (arena);                                         \
        typeof(vec) _vec_ = (vec);                                               \
        typeof(arr) _arr_ = (arr);                                               \
        _vec_->__typeid_vec;                                                     \
        if (_vec_->count + _arr_.count > _vec_->capacity) {                      \
            vec__grow(_arena_, (GenericVec *)_vec_, sizeof(*_vec_->ptr),         \
                      alignof(typeof(*_vec_->ptr)), _vec_->count + _arr_.count); \
        }                                                                        \
        u64 start = _vec_->count;                                                \
        _vec_->count += _arr_.count;                                             \
        typeof(_arr_) a = {                                                      \
            .ptr = _vec_->ptr + start,                                           \
            .count = _vec_->count - start,                                       \
        };                                                                       \
        SDL_memcpy(a.ptr, arr.ptr, a.count * sizeof(*a.ptr));                    \
    })

#define vec_extend_zero(arena, vec, new_count)                                   \
    ({                                                                           \
        typeof(arena) _arena_ = (arena);                                         \
        typeof(vec) _vec_ = (vec);                                               \
        _vec_->__typeid_vec;                                                     \
        u64 _new_count_ = (new_count);                                           \
        if (_vec_->count + _new_count_ > _vec_->capacity) {                      \
            vec__grow(_arena_, (GenericVec *)_vec_, sizeof(*_vec_->ptr),         \
                      alignof(typeof(*_vec_->ptr)), _vec_->count + _new_count_); \
        }                                                                        \
        u64 start = _vec_->count;                                                \
        _vec_->count += _new_count_;                                             \
        (typeof(_vec_->__to_arr[0])){                                            \
            .ptr = _vec_->ptr + start,                                           \
            .count = _new_count_,                                                \
        };                                                                       \
    })

#define vec_arr(vec)                    \
    ({                                  \
        typeof(vec) _vec_ = (vec);      \
        _vec_->__typeid_vec;            \
        (typeof((_vec_)->__to_arr[0])){ \
            .ptr = (_vec_)->ptr,        \
            .count = (_vec_)->count,    \
        };                              \
    })

#define vec_reset(vec)                                                 \
    ({                                                                 \
        typeof(vec) _vec_ = (vec);                                     \
        _vec_->__typeid_vec;                                           \
        SDL_memset(_vec_->ptr, 0, sizeof(*_vec_->ptr) * _vec_->count); \
        _vec_->count = 0;                                              \
    })

#define vec_prealloc(arena, vec, reserve)                                                     \
    ({                                                                                        \
        typeof(arena) _arena_ = (arena);                                                      \
        typeof(vec) _vec_ = (vec);                                                            \
        typeof(reserve) _reserve_ = (reserve);                                                \
        _vec_->__typeid_vec;                                                                  \
        if (_vec_->capacity < _reserve_) {                                                    \
            u64 old_size = _vec_->capacity * sizeof(*_vec_->ptr);                             \
            u64 new_size = _reserve_ * sizeof(*_vec_->ptr);                                   \
            u64 align = alignof(typeof(*_vec_->ptr));                                         \
            _vec_->ptr = arena_realloc_bytes(_arena_, _vec_->ptr, old_size, new_size, align); \
            _vec_->capacity = _reserve_;                                                      \
        }                                                                                     \
    })

//
// FixedVec
//

#define fvec_alloc(arena, t, cap)                                            \
    ({                                                                       \
        typeof(arena) _arena_ = (arena);                                     \
        typeof(cap) _cap_ = (cap);                                           \
        (CONCAT(FixedVec_, t)){                                              \
            .ptr = arena_push_bytes(_arena_, sizeof(t) * _cap_, alignof(t)), \
            .count = 0,                                                      \
            .capacity = _cap_,                                               \
        };                                                                   \
    })

#define fvec_push(vec, val)                         \
    ({                                              \
        typeof(vec) _vec_ = (vec);                  \
        typeof(val) _val_ = (val);                  \
        _vec_->__typeid_fixed_vec;                  \
        log_assert(_vec_->count < _vec_->capacity); \
        _vec_->count++;                             \
        _vec_->ptr[_vec_->count - 1] = _val_;       \
    })

#define fvec_push_zero(vec)                       \
    ({                                            \
        typeof(vec) _vec_ = (vec);                \
        _vec_->__typeid_fixed_vec;                \
        log_assert(_vec_.count < _vec_.capacity); \
        _vec_->count++;                           \
        (&_vec_->ptr[_vec_->count - 1]);          \
    })

#define fvec_pop(vec)                                           \
    ({                                                          \
        typeof(vec) _vec_ = (vec);                              \
        _vec_->__typeid_fixed_vec;                              \
        log_assert(_vec_->count > 0);                           \
        _vec_->ptr[_vec_->count - 1] = (typeof(*_vec_->ptr)){}; \
        _vec_->count--;                                         \
    })

#define fvec_extend(vec, arr)                                      \
    ({                                                             \
        typeof(vec) _vec_ = (vec);                                 \
        typeof(arr) _arr_ = (arr);                                 \
        _vec_->__typeid_fixed_vec;                                 \
        log_assert(_vec_->count + _arr_.count <= _vec_->capacity); \
        u64 start = _vec_->count;                                  \
        _vec_->count += _arr_.count;                               \
        typeof(_arr_) a = {                                        \
            .ptr = _vec_->ptr + start,                             \
            .count = _vec_->count - start,                         \
        };                                                         \
        SDL_memcpy(a.ptr, arr.ptr, a.count * sizeof(*a.ptr));      \
    })

#define fvec_extend_zero(vec, new_count)                           \
    ({                                                             \
        typeof(vec) _vec_ = (vec);                                 \
        _vec_->__typeid_fixed_vec;                                 \
        u64 _new_count_ = (new_count);                             \
        log_assert(_vec_->count + _new_count_ <= _vec_->capacity); \
        u64 start = _vec_->count;                                  \
        _vec_->count += _new_count_;                               \
        (typeof(_vec_->__to_arr[0])){                              \
            .ptr = _vec_->ptr + start,                             \
            .count = _new_count_,                                  \
        };                                                         \
    })

#define fvec_arr(vec)                   \
    ({                                  \
        typeof(vec) _vec_ = (vec);      \
        _vec_->__typeid_fixed_vec;      \
        (typeof((_vec_)->__to_arr[0])){ \
            .ptr = (_vec_)->ptr,        \
            .count = (_vec_)->count,    \
        };                              \
    })

#define fvec_reset(vec)                                                \
    ({                                                                 \
        typeof(vec) _vec_ = (vec);                                     \
        _vec_->__typeid_fixed_vec;                                     \
        SDL_memset(_vec_->ptr, 0, sizeof(*_vec_->ptr) * _vec_->count); \
        _vec_->count = 0;                                              \
    })

//
// Idk
//

void thread_init();

//
// Option
//

#define some(v, t) ((CONCAT(Opt_, t)){.opt = (v), .present = true})
#define none(t) ((CONCAT(Opt_, t)){.present = false})

//
// Duration
//

// Monotonic nanoseconds
typedef i64 Duration;
derive_type(Duration);

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
void err_log(ErrorContext *ctx);

//
// Encoding/Decoding
//

Arr_u8 decode_base64(ErrorContext *err, Arena *arena, Str s);
u64 parse_u64(ErrorContext *err, Str s);

//
// SDL helpers
//

SDL_IOStream *sdl_io_from_mem(ErrorContext *err, Arr_u8 buf);
void sdl_close_io(SDL_IOStream *stream);

#define sdl_assert(func)                     \
    ({                                       \
        typeof(func) ret = (func);           \
        if (!ret) {                          \
            log_fatal("%s", SDL_GetError()); \
        }                                    \
        ret;                                 \
    })

//
// Packer
//

struct Packer {
    u64 offset;
    u8 *ptr __attribute__((sized_by(offset)));
    u64 capacity;
};
derive_struct(Packer);

Packer packer_from_arr(Arr_u8 arr);
// Returns offset iff packed
Opt_u64 packer_try_push(Packer *packer, Arr_u8 buf, u64 alignment);
