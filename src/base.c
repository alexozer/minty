#include "base.h"
#include "platform.h"
#include "template_types.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

// TODO sane arena sizing/lifetime scheme
static constexpr u64 ARENA_POOL_MAX = 16;
static constexpr u64 ARENA_SIZE = megabytes(32);
static Arena s_arena_pool[ARENA_POOL_MAX];
static Arena *s_arena_stack[ARENA_POOL_MAX];
static u64 s_arena_stack_top;

// TODO make 100% sure LTO inlining is working for these small functions
u64 align_to(u64 n, u64 a) {
    return ((n) + (a - 1)) & ~(a - 1);
}

u64 min(u64 a, u64 b) {
    return a < b ? a : b;
}

u64 max(u64 a, u64 b) {
    return a > b ? a : b;
}

// https://jameshfisher.com/2018/03/30/round-up-power-2/
u64 next_pow2(u64 x) {
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    x++;
    return x;
}

[[noreturn]] void *oob() {
    log_fatal("Array index out of bounds");
}

[[noreturn]] void log__assert(const char *cond, const char *file, int line) {
    log_fatal("Assertion failed: %s:%d: %s", file, line, cond);
}

void arena_pool_init() {
    for (u64 i = 0; i < ARENA_POOL_MAX; i++) {
        s_arena_pool[i].reserved = ARENA_SIZE;
        s_arena_pool[i].data = os_alloc(s_arena_pool[i].reserved);
        s_arena_stack[i] = &s_arena_pool[i];
    }
}

// TODO use comptime alignment
void *arena__push_bytes(Arena *arena, u64 size, u64 alignment) {
    arena->offset = align_to(arena->offset, alignment);
    void *pos = (void *)((u64)arena->data + arena->offset);
    arena->offset += align_to(size, alignment);
    if (arena->offset > arena->reserved) {
        log_fatal("Arena over! offset = %" PRIu64 ", reserved = %" PRIu64, arena->offset,
                  arena->reserved);
    }

    return pos;
}

Arena *arena_acquire() {
    if (s_arena_stack_top >= ARENA_POOL_MAX) {
        log_fatal("Out of arenas!");
    }
    return s_arena_stack[s_arena_stack_top++];
}

void arena_release(Arena *arena) {
    if (s_arena_stack_top == 0) {
        log_fatal("Tried to release too many arenas!");
    }
    s_arena_stack[--s_arena_stack_top] = arena;
    SDL_memset(arena->data, 0, arena->offset);
    arena->offset = 0;
}

// char *str_to_c(Arena *arena, Str s) {
//     Arr<char> cstr = arena_push_arr<char>(arena, s.count + 1);
//     SDL_memcpy(cstr.ptr, s.ptr, s.count);
//     return cstr.ptr;
// }
//
// Str str_from_c(const char *cstr) {
//     u64 count = 0;
//     while (cstr[count] != '\0')
//         count++;
//     return (Str){.ptr = (u8 *)cstr, .count = count};
// }
//
// Str str_from_c_len(const char *cstr, u64 len) {
//     return {.ptr = (u8 *)cstr, .count = len};
// }

// Super conservative definition probably
bool char_is_whitespace(u8 c) {
    return c == C(' ') || c == C('\r') || c == C('\n') || c == C('\t');
}

bool str_eq(Str a, Str b) {
    if (a.count != b.count) {
        return false;
    }
    return SDL_memcmp(a.ptr, b.ptr, a.count) == 0;
}

// For non-overlapping arrays
void str_copy(Str dest, Str source) {
    log_assert(dest.count == source.count);
    if (dest.count > 0) {
        SDL_memcpy(dest.ptr, source.ptr, dest.count);
    }
}

bool str_is_empty(Str str) {
    return str.count == 0;
}

Str str_clone(Arena *arena, Str str) {
    Str clone = {.ptr = (u8 *)arena__push_bytes(arena, str.count, 1), .count = str.count};
    str_copy(clone, str);
    return clone;
}

Str str_slice(Str s, u64 start, u64 end) {
    log_assert(start <= s.count);
    log_assert(end <= s.count);
    log_assert(start <= end);
    return (Str){.ptr = (u8 *)s.ptr + start, .count = end - start};
}

Str str_trim(Str s) {
    u64 start = 0;
    while (start < s.count && char_is_whitespace(A(s, start))) {
        start++;
    }

    u64 end = s.count;
    while (end > start && char_is_whitespace(A(s, end - 1))) {
        end--;
    }

    return str_slice(s, start, end);
}

bool str_starts_with(Str s, Str prefix) {
    A(s, 0) = 'c';
    if (prefix.count > s.count) {
        return false;
    }
    Str s_prefix = str_slice(s, 0, prefix.count);
    return str_eq(prefix, s_prefix);
}

StrLineIter str_lines(Str s) {
    return (StrLineIter){.base = s, .pos = 0};
}

bool str_lines_next(StrLineIter *iter, Str *line) {
    if (iter->pos >= iter->base.count) {
        return false;
    }

    u64 line_start = iter->pos;
    Str data = iter->base;
    const u64 size = iter->base.count;

    // Advance until next line break
    u64 line_end = line_start;
    while (line_end < size && A(data, line_end) != C('\r') && A(data, line_end) != C('\n')) {
        line_end++;
    }

    // Advance past line breaks
    u64 next_line_start = line_end;
    while (next_line_start < size && A(data, next_line_start) == C('\r')) {
        next_line_start++;
    }
    if (next_line_start < size && A(data, next_line_start) == C('\n')) {
        next_line_start++;
    }

    iter->pos = next_line_start;

    if (line != nullptr) {
        line->ptr = iter->base.ptr + line_start;
        line->count = line_end - line_start;
    }

    return true;
}

u64 str_count_lines(Str s) {
    u64 line_count = 0;
    StrLineIter iter = str_lines(s);
    while (str_lines_next(&iter, nullptr)) {
        line_count++;
    }
    return line_count;
}

bool str_split2(Str base, u8 delim, Str *left, Str *right) {
    *left = STR_EMPTY;
    *right = STR_EMPTY;

    u64 delim_idx = 0;
    while (delim_idx < base.count && A(base, delim_idx) != delim) {
        delim_idx++;
    }
    if (delim_idx < base.count) {
        *left = str_slice(base, 0, delim_idx);
        *right = str_slice(base, delim_idx + 1, base.count);
        return true;
    }
    return false;
}

Str str_format_v(Arena *arena, const char *format, va_list args) {
    char buf[kilobytes(8)];
    int n = SDL_vsnprintf(buf, sizeof(buf), format, args);
    if (n < 0) {
        return S("<formatting error>");
    }

    Str s = {.ptr = (u8 *)buf, .count = (u64)n};
    // TODO try to allocate directly on tip of arena?
    return str_clone(arena, s);
}

__attribute__((format(printf, 2, 3))) Str str_format(Arena *arena, const char *format, ...) {
    va_list args;
    va_start(args, format);
    Str s = str_format_v(arena, format, args);
    va_end(args);
    return s;
}

bool str_find(Str haystack, Str needle, u64 *out) {
    *out = 0;

    if (str_is_empty(needle)) {
        // Found the non-existent needle at the start of the haystack
        return false;
    }

    u64 i = 0;
    while (i + needle.count < haystack.count) {
        void *loc = memchr(haystack.ptr + i, A(needle, 0), haystack.count - i);
        if (loc == nullptr) return false;

        u64 haystack_start = (u64)loc - (u64)haystack.ptr;
        u64 haystack_end = haystack_start + needle.count;
        if (haystack_end > haystack.count) return false;

        Str haystack_slice = str_slice(haystack, haystack_start, haystack_end);
        if (str_eq(haystack_slice, needle)) {
            *out = haystack_start;
            return true;
        }

        i = haystack_start + 1;
    }
    return false;
}

//
// Encoding/Decoding
//

bool parse_u64(Str s, u64 *out) {
    *out = 0;

    if (str_is_empty(s)) {
        return false;
    }

    u64 result = 0;
    for (u64 i = 0; i < s.count; i++) {
        if (A(s, i) < C('0') || A(s, i) > C('9')) {
            return false;
        }
        u64 new_result = result * 10 + (s.ptr[i] - C('0'));
        if (new_result < result) {
            return false;
        }
        result = new_result;
    }

    *out = result;
    return true;
}

//
// Idk
//

void thread_init() {
    arena_pool_init();
}

Arr_u8 fs_load_file_c(ErrorContext *err, Arena *arena, Str path) {
    Arr_u8 ret = {};
    Arena *scratch = arena_acquire();
    SDL_IOStream *stream = nullptr;

    char *path_cstr = str_to_c(scratch, path);
    stream = SDL_IOFromFile(path_cstr, "rb");

    i64 size = SDL_GetIOSize(stream);
    if (size < 0) {
        err_push(err, "%s", SDL_GetError());
        goto end;
    }

    ret = arena_push_arr(arena, u8, (u64)size);
    u64 offset = 0;
    while (offset < size && SDL_GetIOStatus(stream) == SDL_IO_STATUS_READY) {
        offset += SDL_ReadIO(stream, ret.ptr + offset, (u64)size - offset);
    }
    if (offset != size) {
        err_push(err, "%s", SDL_GetError());
        goto end;
    }

end:
    if (err->ctx_stack.count) err_push(err, "Load file '%.*s'", SF(path));
    if (stream) SDL_CloseIO(stream);
    if (arena) arena_release(scratch);
    return ret;
}

//
// Time
//

Instant get_current_monotonic_time() {
    return (Instant){.time_nanoseconds = (i64)SDL_GetTicksNS()};
}

//
// Errors
//

// typedef struct {
//     u64 stack_pos;
// } ErrToken;
//
// ErrToken err_push(ErrorContext *err);
// void err_pop(ErrorContext *err, const char *format, ...);

__attribute__((format(printf, 2, 3))) void err_push_ctx(ErrorContext *ctx,
                                                        const char *format,
                                                        ...) {
    va_list args;
    va_start(args, format);
    Str msg = str_format_v(ctx->arena, format, args);
    vec_push(ctx->arena, &ctx->ctx_stack, msg);
    va_end(args);
}
