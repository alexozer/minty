#include "base.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_timer.h>
#include <simdutf_c.h>

#include "platform.hpp"

// TODO sane arena sizing/lifetime scheme
static constexpr u64 ARENA_POOL_MAX = 16;
static constexpr u64 ARENA_SIZE = megabytes(32);
static Arena s_arena_pool[ARENA_POOL_MAX];
static Arena *s_arena_stack[ARENA_POOL_MAX];
static u64 s_arena_stack_top;

void
arena_pool_init()
{
    for (u64 i = 0; i < ARENA_POOL_MAX; i++) {
        s_arena_pool[i].reserved = ARENA_SIZE;
        s_arena_pool[i].data = os_alloc(s_arena_pool[i].reserved);
        s_arena_stack[i] = &s_arena_pool[i];
    }
}

Arena *
arena_acquire()
{
    if (s_arena_stack_top >= ARENA_POOL_MAX) {
        log_fatal("FATAL: out of arenas");
    }
    return s_arena_stack[s_arena_stack_top++];
}

void
arena_release(Arena *arena)
{
    if (s_arena_stack_top == 0) {
        log_fatal("FATAL: tried to release too many arenas!");
    }
    s_arena_stack[--s_arena_stack_top] = arena;
    SDL_memset(arena->data, 0, arena->offset);
    arena->offset = 0;
}

char *
str_to_c(Arena *arena, Str s)
{
    Arr<char> cstr = arena_push_arr<char>(arena, s.count + 1);
    SDL_memcpy(cstr.ptr, s.ptr, s.count);
    return cstr.ptr;
}

Str
str_from_c(const char *cstr)
{
    u64 count = 0;
    while (cstr[count] != '\0')
        count++;
    return (Str){ .ptr = (u8 *)cstr, .count = count };
}

Str
str_from_c_len(const char *cstr, u64 len)
{
    return { .ptr = (u8 *)cstr, .count = len };
}

// Returns a string from a utf8 byte buffer. Doesn't validate if it's actually
// utf8.
Str
str_from_bytes(Arr<u8> bytes)
{
    // Skip utf8 BOM
    u64 start = 0;
    if (bytes.count >= 3 && bytes[0] == C('\xef') && bytes[1] == C('\xbb') &&
        bytes[2] == C('\xbf')) {
        start = 3;
    }

    return arr_slice(bytes, start, bytes.count);
}

// Super conservative definition probably
bool
char_is_whitespace(u8 c)
{
    return c == C(' ') || c == C('\r') || c == C('\n') || c == C('\t');
}

Str
str_trim(Str s)
{
    u64 start = 0;
    while (start < s.count && char_is_whitespace(s[start])) {
        start++;
    }

    u64 end = s.count;
    while (end > start && char_is_whitespace(s[end - 1])) {
        end--;
    }

    return arr_slice(s, start, end);
}

Str
str_clone(Arena *arena, Str s)
{
    Str clone = arena_push_arr<u8>(arena, s.count);
    SDL_memcpy(clone.ptr, s.ptr, s.count);
    return clone;
}

bool
str_eq(Str s1, Str s2)
{
    return arr_eq(s1, s2);
}

bool
str_starts_with(Str s, Str prefix)
{
    if (prefix.count > s.count) {
        return false;
    }
    Str s_prefix = arr_slice(s, 0, prefix.count);
    return arr_eq(prefix, s_prefix);
}

StrLineIter
str_lines(Str s)
{
    return (StrLineIter){ .base = s, .pos = 0 };
}

bool
str_lines_next(StrLineIter *iter, Str *line)
{
    if (iter->pos >= iter->base.count) {
        return false;
    }

    u64 line_start = iter->pos;
    Arr<u8> data = iter->base;
    const u64 size = iter->base.count;

    // Advance until next line break
    u64 line_end = line_start;
    while (line_end < size && data[line_end] != C('\r') && data[line_end] != C('\n')) {
        line_end++;
    }

    // Advance past line breaks
    u64 next_line_start = line_end;
    while (next_line_start < size && (data[next_line_start] == C('\r'))) {
        next_line_start++;
    }
    if (next_line_start < size && (data[next_line_start] == C('\n'))) {
        next_line_start++;
    }

    iter->pos = next_line_start;

    if (line != nullptr) {
        line->ptr = iter->base.ptr + line_start;
        line->count = line_end - line_start;
    }

    return true;
}

u64
str_count_lines(Str s)
{
    u64 line_count = 0;
    StrLineIter iter = str_lines(s);
    while (str_lines_next(&iter, nullptr)) {
        line_count++;
    }
    return line_count;
}

Pair<Str, Str>
str_split2(Str base, u8 delim)
{
    u64 delim_idx = 0;
    while (delim_idx < base.count && base[delim_idx] != delim) {
        delim_idx++;
    }
    Pair<Str, Str> result = {};
    if (delim_idx < base.count) {
        result.left = arr_slice(base, 0, delim_idx);
        result.right = arr_slice(base, delim_idx + 1, base.count);
    }
    return result;
}

Str
str_format_v(Arena *arena, const char *format, va_list args)
{
    char buf[kilobytes(8)];
    int n = SDL_vsnprintf(buf, sizeof(buf), format, args);
    if (n < 0) {
        return S("<formatting error>");
    }

    Str s = { .ptr = (u8 *)buf, .count = (u64)n };
    // TODO try to allocate directly on tip of arena?
    return str_clone(arena, s);
}

__attribute__((format(printf, 2, 3))) Str
str_format(Arena *arena, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Str s = str_format_v(arena, format, args);
    va_end(args);
    return s;
}

bool
str_is_valid_utf8(Str s)
{
    return simdutf_validate_utf8((const char *)s.ptr, s.count);
}

Opt<u64>
str_find(Str haystack, Str needle)
{
    if (str_is_empty(needle)) {
        // Found the non-existent needle at the start of the haystack
        return some((u64)0);
    }

    u64 i = 0;
    while (i + needle.count < haystack.count) {
        void *loc = memchr(haystack.ptr + i, needle[0], haystack.count - i);
        if (loc == nullptr)
            return {};

        u64 haystack_start = (u64)loc - (u64)haystack.ptr;
        u64 haystack_end = haystack_start + needle.count;
        if (haystack_end > haystack.count)
            return {};

        Str haystack_slice = arr_slice(haystack, haystack_start, haystack_end);
        if (str_eq(haystack_slice, needle)) {
            return some(haystack_start);
        }

        i = haystack_start + 1;
    }
    return {};
}

//
// Paths
//

Str
path_join(Arena *arena, Str left_path, Str right_path)
{
    if (arr_is_empty(left_path)) {
        return right_path;
    }
    if (arr_is_empty(right_path)) {
        return left_path;
    }
    Vec<u8> joined = {};
    vec_extend(arena, &joined, left_path);
    vec_push(arena, &joined, C('/'));
    vec_extend(arena, &joined, right_path);
    return vec_arr(&joined);
}

//
// Subprocesses
//

Arr<char *> g_argv;
Arr<char *> g_envp;

Str
env_get(Str key)
{
    for (u64 i = 0; i < g_envp.count; i++) {
        Str env_pair = str_from_c(g_envp[i]);
        if (str_starts_with(env_pair, key)) {
            if (env_pair.count > key.count && env_pair[key.count] == C('=')) {
                return arr_slice(env_pair, key.count + 1, env_pair.count);
            }
        }
    }

    return {};
}

//
// Logging
//

[[noreturn]] void
log__assert(const char *cond, const char *file, int line)
{
    log_fatal("Assertion failed: %s:%d: %s", file, line, cond);
}

//
// Encoding/Decoding
//

u64
parse_u64(ErrorContext *err, Str s)
{
    err_scope(err, "Parse '%.*s' as u64", SF(s));

    if (str_is_empty(s)) {
        bail_v(err, 0, "Empty string");
    }

    u64 result = 0;
    for (u64 i = 0; i < s.count; i++) {
        if (s[i] < C('0') || s[i] > C('9')) {
            bail_v(err, 0, "Non-numeric char: '%c'", s[i]);
        }
        u64 new_result = result * 10 + (s[i] - C('0'));
        if (new_result < result) {
            bail_v(err, 0, "u64 overflow");
        }
        result = new_result;
    }

    return result;
}

Arr<u8>
base64_decode(Arena *arena, ErrorContext *err, Str s)
{
    err_scope(err, "Decode base64");

    u64 max_out_size = simdutf_maximal_binary_length_from_base64((const char *)s.ptr, s.count);
    Arr<u8> out = arena_push_arr<u8>(arena, max_out_size);
    simdutf_result result = simdutf_base64_to_binary((const char *)s.ptr,
                                                     s.count,
                                                     (char *)out.ptr,
                                                     SIMDUTF_BASE64_DEFAULT,
                                                     SIMDUTF_LAST_CHUNK_STRICT);
    if (result.error != SIMDUTF_ERROR_SUCCESS) {
        bail_v(err, {}, "Invalid base64. Error Code = %d", result.error);
    }
    return arr_slice(out, 0, result.count);
}

Str
base64_encode(Arena *arena, Arr<u8> a)
{
    u64 out_size = simdutf_base64_length_from_binary(a.count, SIMDUTF_BASE64_DEFAULT);
    Arr<u8> out = arena_push_arr<u8>(arena, out_size);
    u64 written = simdutf_binary_to_base64(
      (const char *)a.ptr, a.count, (char *)out.ptr, SIMDUTF_BASE64_DEFAULT);
    log_assert(written == out_size);
    return out;
}

//
// Time
//

constexpr u64 SECOND_IN_NS = 1'000'000'000;

Instant
instant_from_sdl_nanos(u64 nanos)
{
    return {
        .seconds = (i64)(nanos / SECOND_IN_NS),
        .nanoseconds = (u32)(nanos % SECOND_IN_NS),
    };
}

Instant
get_current_monotonic_time()
{
    return instant_from_sdl_nanos(SDL_GetTicksNS());
}

//
// Idk
//

void
thread_init(int argc, char **argv)
{
    g_argv = { .ptr = argv, .count = (u64)argc };
    arena_pool_init();
}

//
// Errors
//

__attribute__((format(printf, 2, 3))) void
err_push_ctx(ErrorContext *ctx, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Str msg = str_format_v(ctx->arena, format, args);
    vec_push(ctx->arena, &ctx->ctx_stack, msg);
    va_end(args);
}

void
err_log(ErrorContext *ctx)
{
    u64 count = ctx->ctx_stack.count;
    if (count == 0)
        return;

    log_error("Failed: %.*s", SF(ctx->ctx_stack[count - 1]));
    if (count > 1) {
        log_error("");
        log_error("Caused By:");
        log_error("");
        for (u64 i = count - 1; i > 0; i--) {
            log_error("  Failed: %.*s", SF(ctx->ctx_stack[i - 1]));
        }
    }
}

bool
err_occurred(ErrorContext *ctx)
{
    return !vec_is_empty(&ctx->ctx_stack);
}

//
// FS
//

Arr<u8>
fs_load_file(ErrorContext *err, Arena *arena, Str path)
{
    err_scope(err, "Load file '%.*s'", SF(path));

    Arena *scratch = arena_acquire();
    defer(arena_release(scratch));

    char *path_cstr = str_to_c(scratch, path);
    SDL_IOStream *stream = try_sdl(err, {}, SDL_IOFromFile(path_cstr, "rb"));
    defer(SDL_CloseIO(stream));

    i64 size = SDL_GetIOSize(stream);
    if (size < 0) {
        bail_v(err, {}, "%s", SDL_GetError());
    }

    Arr<u8> contents = arena_push_arr<u8>(arena, (u64)size);
    u64 offset = 0;
    while (offset < size && SDL_GetIOStatus(stream) == SDL_IO_STATUS_READY) {
        offset += SDL_ReadIO(stream, (u8 *)contents.ptr + offset, (u64)size - offset);
    }
    if (offset != size) {
        bail_v(err, {}, "%s", SDL_GetError());
    }

    return contents;
}
