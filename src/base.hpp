#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>

#include <SDL3/SDL_log.h>

//
// World's crappiest optional type
//

template <typename T>
struct Opt {
    bool present;
    T opt; // Makes it more obvious we're accessing an `Opt` at a glance
};

template <typename T>
constexpr Opt<T> some(T v) { return { .present = true, .opt = v }; }

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

constexpr u64 kilobytes(u64 n) { return n * 1024LL; }
constexpr u64 megabytes(u64 n) { return kilobytes(n) * 1024LL; }
constexpr u64 align_to(u64 n, u64 a) { return ((n) + (a - 1)) & ~(a - 1); }

template <typename T>
constexpr T min(T a, T b) { return a < b ? a : b; }

template <typename T>
constexpr T max(T a, T b) { return a > b ? a : b; }

constexpr u64 DEFAULT_ALIGNMENT = 8;

// https://jameshfisher.com/2018/03/30/round-up-power-2/
constexpr u64 next_pow2(u64 x) {
    x--;
    x |= x>>1;
    x |= x>>2;
    x |= x>>4;
    x |= x>>8;
    x |= x>>16;
    x |= x>>32;
    x++;
    return x;
}

//
// Logging
//

#define log_trace(...) SDL_LogTrace(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_debug(...) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_info(...) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_warn(...) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_error(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define log_fatal(...) SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__); exit(1)

[[noreturn]] void log__assert(const char *cond, const char *file, int line);
#define log_assert(cond) if ((cond) == false) log__assert(#cond, __FILE_NAME__, __LINE__)
#define unreachable() log__assert("unreachable", __FILE_NAME__, __LINE__)

//
// Arenas
//

template <typename T>
struct Arr {
    T *ptr;
    u64 count;

    T& operator[](u64 i) {
        log_assert(i < count);
        return ptr[i];
    }
};

struct Arena {
    void *data;
    u64 reserved;
    u64 offset;
};

template <u64 ALIGNMENT>
void *arena__push_bytes(Arena *arena, u64 size) {
    arena->offset = align_to(arena->offset, ALIGNMENT);
    void *pos = (void *)((u64)arena->data + arena->offset);
    arena->offset += align_to(size, ALIGNMENT);
    if (arena->offset > arena->reserved) {
         log_fatal("Arena over! offset = %" PRIu64 ", reserved = %" PRIu64, arena->offset, arena->reserved);
    }
    return pos;
}

void arena_pool_init();
Arena *arena_acquire();
void arena_release(Arena *arena);

template <typename T>
T *arena_push(Arena *arena) {
    return (T *)arena__push_bytes<8>(arena, sizeof(T));
}

template <typename T>
Arr<T> arena_push_arr(Arena *arena, u64 count) {
    constexpr u64 ALIGN = min(next_pow2(sizeof(T)), (u64)8);
    return {
        .ptr = (T *)arena__push_bytes<ALIGN>(arena, sizeof(T) * count),
        .count = count,
    };
}

//
// Arrays
//

template <typename T>
Arr<T> arr_from_null_terminated(T *v) {
    u64 count = 0;
    while (v[count] != nullptr) count++;
    return { .ptr = v, .count = count };
}

template <typename T>
Arr<T> arr_slice(Arr<T> arr, u64 start, u64 end) {
    log_assert(start <= arr.count);
    log_assert(end <= arr.count);
    log_assert(start <= end);

    return {
        .ptr = arr.ptr + start,
        .count = end - start,
    };
}

template <typename T>
bool arr_eq(Arr<T> a, Arr<T> b) {
    if (a.count != b.count) {
        return false;
    }
    for (u64 i = 0; i < a.count; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

template <typename T>
void arr_copy(Arr<T> dest, Arr<T> source) {
    if (dest.count != source.count) {
        log_fatal("Unequal array lengths: dest = %" PRIu64 ", source = %" PRIu64, dest.count, source.count);
    }
    for (u64 i = 0; i < dest.count; i++) {
        dest[i] = source[i];
    }
}

template <typename T>
bool arr_is_empty(Arr<T> arr) {
    return arr.count == 0;
}

// May come to regret this...
template <typename L, typename R>
struct Pair {
    L left;
    R right;
};

//
// Strings
//

using Str = Arr<u8>;

#define S(s) ((Str){ .ptr = (u8 *)(s), .count = (sizeof(s)) - 1 })
#define SF(s) (int)(s).count, (char *)(s).ptr
#define C(c) ((u8)(c))
#define A(a) ((Arr<u8>){ .ptr = (a), .count = sizeof((a)) / sizeof((a)[0]) })

char *str_to_c(Arena *arena, Str s);
Str str_from_c(const char *cstr);
Str str_from_c_len(const char *cstr);
Str str_from_bytes(Arr<u8> bytes);
bool char_is_whitespace(u8 c);
Str str_trim(Str s);
Str str_clone(Arena *arena, Str s);
bool str_eq(Str s1, Str s2);
bool str_starts_with(Str s, Str prefix);
__attribute__((format(printf, 2, 3)))
Str str_format(Arena *arena, const char *format, ...);
Str str_format_v(Arena *arena, const char *format, va_list args);
bool str_is_valid_utf8(Str s);
Opt<u64> str_find(Str haystack, Str needle);

// Certainly possible to do this simply and w/o an iterator object, but just messin around
struct StrLineIter {
    Str base;
    u64 pos;
};

StrLineIter str_lines(Str s);
bool str_lines_next(StrLineIter* iter, Str *line);
u64 str_count_lines(Str s);
Pair<Str, Str> str_split2(Str base, u8 delim);
constexpr bool str_is_empty(Str s) { return s.count == 0; }

//
// Vec
//

template <typename T>
struct Vec {
    T *ptr;
    u64 count; // Element count (not size in bytes)
    u64 cap; // Element capacity (not size capacity in bytes)

    T& operator[](u64 i) {
        log_assert(i < count);
        return ptr[i];
    }
};

constexpr u64 MIN_VEC_CAPACITY = 8;

template <typename T>
void vec__grow(Arena *arena, Vec<T> *vec, u64 new_cap) {
    // Fast path?
    if (new_cap <= vec->cap) return;

    new_cap = next_pow2(max(new_cap, MIN_VEC_CAPACITY));

    if (new_cap > vec->cap) {
        Arr<T> new_arr = arena_push_arr<T>(arena, new_cap);
        Arr<T> new_arr_slice = arr_slice(new_arr, 0, vec->count);
        arr_copy(new_arr_slice, vec_arr(vec));

        vec->ptr = new_arr.ptr;
        vec->cap = new_cap;
    }
}

template <typename T>
T *vec_push(Arena *arena, Vec<T> *vec, T val) {
    vec__grow(arena, vec, vec->count + 1);
    vec->ptr[vec->count] = val;
    return &vec->ptr[vec->count++];
}

template <typename T>
void vec_pop(Vec<T> *vec) {
    log_assert(vec->count > 0);
    vec->count--;
}

template <typename T>
Arr<T> vec_extend(Arena *arena, Vec<T> *vec, Arr<T> arr) {
    vec__grow(arena, vec, vec->count + arr.count);

    u64 start = vec->count;
    vec->count += arr.count;
    Arr<T> a = arr_slice(vec_arr(vec), start, vec->count);
    arr_copy(a, arr);

    return a;
}

template <typename T>
Arr<T> vec_arr(Vec<T> *vec) {
    return { .ptr = vec->ptr, .count = vec->count };
}

template <typename T>
void vec_reset(Vec<T> *vec) {
    vec->count = 0;
}

template <typename T>
bool vec_is_empty(Vec<T> *vec) {
    return vec->count == 0;
}

//
// Defer
//

template<typename F>
struct Defer {
    F fn;
    explicit Defer(F f) : fn(f) {}
    ~Defer() { fn(); }

    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;
};

// Deduction guide (C++17) — lets you write Defer d([&]{...}) without specifying F
template<typename F>
Defer(F) -> Defer<F>;

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define defer(code) Defer CONCAT(_defer_, __LINE__)([&]{ code; })

template<typename F>
struct DeferCtx {
    F fn;
    u64 ctx;

    DeferCtx(F f, uint64_t c) : fn(f), ctx(c) {}
    ~DeferCtx() { fn(ctx); }

    DeferCtx(const DeferCtx&) = delete;
    DeferCtx& operator=(const DeferCtx&) = delete;
};

template<typename F>
DeferCtx(F, uint64_t) -> DeferCtx<F>;

#define defer_ctx(code, value) Defer CONCAT(_defer_ctx_, __LINE__)([&]{ code; }, value)

//
// Errors
//

struct ErrorContext {
    Arena *arena;
    Vec<Str> ctx_stack;
};

__attribute__((format(printf, 2, 3)))
void err_push_ctx(ErrorContext *ctx, const char *format, ...);
void err_log(ErrorContext *ctx);
bool err_failed(ErrorContext *ctx);

#define err_scope(err, format, ...) \
    DeferCtx CONCAT(_err_scope_, __LINE__)([&] (u64 _err_scope_count_) { \
        if ((err)->ctx_stack.count > 0 && _err_scope_count_ == 0) { \
            err_push_ctx((err), (format) __VA_OPT__(,) __VA_ARGS__); \
        } \
    }, (err)->ctx_stack.count)

#define err_report(ctx, format, ...) if (vec_is_empty(&((ctx)->ctx_stack))) err_push_ctx((ctx), (format) __VA_OPT__(,) __VA_ARGS__)

//
// Paths
//

Str path_join(Arena *arena, Str left_path, Str right_path);

//
// Subprocesses
//

extern Arr<char *> g_argv;
extern Arr<char *> g_envp;

Str env_get(Str key);

struct Cmd {
    Str name;
    Arr<Str> args;
    Arr<Pair<Str, Str>> env;
    Arr<u8> input;
    Str cwd;
};

enum class [[nodiscard]] OSResult {
    Ok,
    PermissionDenied,
    AllocationFailed,
    InvalidPath,
    InvalidFileDescriptor,
    SubprocessExitError,
    SubprocessNonZeroExitCode,
    InvalidUtf8, // Not an OS error! TODO fix
    OtherError,
};

//
// Time
//

struct Duration {
    i64 seconds;
    u32 nanoseconds;
};

struct Instant {
    i64 seconds;
    u32 nanoseconds;
};

// Nanosconds. Signed so that we can use the same type for diffs.
constexpr Duration DURATION_NANOSECOND = { .nanoseconds = 1 };
constexpr Duration DURATION_MICROSECOND = { .nanoseconds = 1'000 };
constexpr Duration DURATION_MILLISECOND = { .nanoseconds = 1'000'000 };
constexpr Duration DURATION_SECOND = { .seconds = 1 };
constexpr Duration DURATION_ZERO = {};

Instant get_current_monotonic_time();
Instant instant_from_sdl_nanos(u64 nanos);

constexpr Duration operator+(const Duration &t1, const Duration &t2) {
    i64 ns_sum = t1.nanoseconds + t2.nanoseconds;
    i64 sec_sum = t1.seconds + t2.seconds + (ns_sum / 1'000'000'000);
    return { .seconds = sec_sum, .nanoseconds = (u32)(ns_sum % 1'000'000'000) };
}

constexpr Duration operator-(const Duration &t1, const Duration &t2) {
    i64 ns_diff = (i64)t1.nanoseconds - (i64)t2.nanoseconds;
    i64 sec_diff = t1.seconds - t2.seconds;
    if (ns_diff < 0) {
        // Carry the 1...
        sec_diff--;
        ns_diff += 1'000'000'000;
    }
    return { .seconds = sec_diff, .nanoseconds = (u32)(ns_diff) };
}

constexpr Duration operator-(const Duration &t) {
    return DURATION_ZERO - t;
}

constexpr void operator+=(Duration& t1, const Duration &t2) {
    t1 = t1 + t2;
}

constexpr void operator-=(Duration& t1, const Duration &t2) {
    t1 = t1 - t2;
}

constexpr bool operator==(const Duration &t1, const Duration &t2) {
    return t1.seconds == t2.seconds && t1.nanoseconds == t2.nanoseconds;
}

constexpr bool operator<(const Duration &t1, const Duration &t2) {
    if (t1.seconds < t2.seconds) {
        return true;
    }
    if (t1.seconds > t2.seconds) {
        return false;
    }
    return (t1.seconds < 0) ^ (t1.nanoseconds < t2.nanoseconds);
}

constexpr bool operator<=(const Duration &t1, const Duration &t2) {
    return (t1 < t2) || t1 == t2;
}

constexpr bool operator>(const Duration &t1, const Duration &t2) {
    return !(t1 <= t2);
}

constexpr bool operator>=(const Duration &t1, const Duration &t2) {
    return !(t1 < t2);
}

constexpr u64 duration_seconds(Duration duration) {
    if (duration.seconds < 0) {
        return (u64)(-duration).seconds;
    } else {
        return (u64)duration.seconds;
    }
}

constexpr u32 duration_subsec_nanos(Duration duration) {
    if (duration.seconds < 0) {
        return (-duration).nanoseconds;
    } else {
        return duration.nanoseconds;
    }
}

constexpr Duration operator-(const Instant &t1, const Instant &t2) {
    Duration d1 = { .seconds = t1.seconds, .nanoseconds = t1.nanoseconds };
    Duration d2 = { .seconds = t2.seconds, .nanoseconds = t2.nanoseconds };
    return d1 - d2;
}

//
// Encoding/Decoding
//

u64 parse_u64(ErrorContext *err, Str s);
Arr<u8> base64_decode(Arena *arena, ErrorContext *err, Str s);
Str base64_encode(Arena *arena, Arr<u8> a);

//
// Idk
//

void thread_init(int argc, char **argv);
