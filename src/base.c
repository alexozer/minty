#include "base.h"
#include "platform.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

// Not sure why these defines are necessary, looks like the header ought to define them?
#define char16_t uint16_t
#define char32_t uint32_t
#include <simdutf_c.h>
#undef char32_t
#undef char16_t

// TODO sane arena sizing/lifetime scheme
static constexpr u64 ARENA_POOL_MAX = 16;
static constexpr u64 ARENA_SIZE = megabytes(32);
static Arena s_arena_pool[ARENA_POOL_MAX];
static Arena *s_arena_stack[ARENA_POOL_MAX];
static u64 s_arena_stack_top;

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

// TODO compile out file/line info in release builds
// ... and in non-release / non-profile builds, also print the failed assertion
[[noreturn]] void crash(const char *file, i32 line, const char *why) {
    if (line > 0) {
        Str f = str_from_c(file);

        // Try to find basename
        u64 pos = f.count;
        while (true) {
            if (pos == 0) break;
            if (A(f, pos - 1) == C('/')) break;
            if (A(f, pos - 1) == C('\\')) break;
            pos--;
        }

        Str basename = str_slice(f, pos, f.count);
        log_fatal("%.*s:%d: %s", SF(basename), line, why);
    } else {
        log_fatal("%s", why);
    }
}

[[noreturn]] void *oob() {
    crash("", 0, "Array index out of bounds");
}

void arena_pool_init() {
    for (u64 i = 0; i < ARENA_POOL_MAX; i++) {
        s_arena_pool[i].reserved = ARENA_SIZE;
        s_arena_pool[i].data = os_alloc(s_arena_pool[i].reserved);
        s_arena_stack[i] = &s_arena_pool[i];
    }
}

// TODO use comptime alignment
void *arena_push_bytes(Arena *arena, u64 size, u64 alignment) {
    arena->offset = align_to(arena->offset, alignment);
    void *pos = (void *)((u64)arena->data + arena->offset);
    arena->offset += align_to(size, alignment);
    if (arena->offset > arena->reserved) {
        log_fatal("Arena over! offset = %" PRIu64 ", reserved = %" PRIu64, arena->offset,
                  arena->reserved);
    }

    return pos;
}

void *arena_realloc_bytes(Arena *arena, void *ptr, u64 old_size, u64 new_size, u64 alignment) {
    if (new_size < old_size) return ptr;
    if (arena->data + arena->offset == ptr + old_size) {
        // Allocate new space at end of arena
        // Assume original alignment hasn't changed
        arena_push_bytes(arena, new_size - old_size, 1);  // Discard new space
        return ptr;
    } else {
        void *new_ptr = arena_push_bytes(arena, new_size, alignment);
        if (old_size > 0) {
            SDL_memcpy(new_ptr, ptr, old_size);
        }
        return new_ptr;
    }
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

Str str_clone(Arena *arena, Str str) {
    Str clone = {.ptr = (u8 *)arena_push_bytes(arena, str.count, 1), .count = str.count};
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

Str str_trim_prefix(Str s, Str prefix) {
    if (str_starts_with(s, prefix)) {
        return str_slice(s, prefix.count, s.count);
    }
    return s;
}

bool str_starts_with(Str s, Str prefix) {
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

StrPair str_split2(Str base, u8 delim) {
    StrPair pair = {};

    u64 delim_idx = 0;
    while (delim_idx < base.count && A(base, delim_idx) != delim) {
        delim_idx++;
    }
    if (delim_idx < base.count) {
        pair.left = str_slice(base, 0, delim_idx);
        pair.right = str_slice(base, delim_idx + 1, base.count);
    } else {
        pair.left = base;
    }
    return pair;
}

Str str_format_v(Arena *arena, const char *format, va_list args) {
    u64 init_arena_offset = arena->offset;
    Arr_u8 buf = arena_push_arr(arena, u8, kilobytes(8));
    int n = SDL_vsnprintf((char *)buf.ptr, buf.count, format, args);
    if (n < 0) {
        SDL_memset(buf.ptr, 0, buf.count);
        arena->offset = init_arena_offset;
        return S("<formatting error>");
    } else {
        arena->offset = init_arena_offset + (u64)n;
        return (Str){.ptr = buf.ptr, .count = (u64)n};
    }
}

__attribute__((format(printf, 2, 3))) Str str_format(Arena *arena, const char *format, ...) {
    va_list args;
    va_start(args, format);
    Str s = str_format_v(arena, format, args);
    va_end(args);
    return s;
}

Opt_u64 str_find(Str haystack, Str needle) {
    if (is_empty(needle)) {
        return none(u64);
    }

    u64 i = 0;
    while (i + needle.count <= haystack.count) {
        void *loc =
            (void *)simdutf_find((char *)(haystack.ptr + i),
                                 (char *)(haystack.ptr + haystack.count), (char)A(needle, 0));
        if (loc == haystack.ptr + haystack.count) return none(u64);

        u64 haystack_start = (u64)loc - (u64)haystack.ptr;
        u64 haystack_end = haystack_start + needle.count;
        if (haystack_end > haystack.count) return none(u64);

        Str haystack_slice = str_slice(haystack, haystack_start, haystack_end);
        if (str_eq(haystack_slice, needle)) {
            return some(haystack_start, u64);
        }

        i = haystack_start + 1;
    }
    return none(u64);
}

bool str_contains(Str haystack, Str needle) {
    return str_find(haystack, needle).present;
}

derive_type(char);

char *str_to_c(Arena *arena, Str s) {
    Arr_char cstr = arena_push_arr(arena, char, s.count + 1);
    if (s.count > 0) {
        SDL_memcpy(cstr.ptr, s.ptr, s.count);
    }
    return cstr.ptr;
}

Str str_from_c(const char *cstr) {
    return (Str){.ptr = (u8 *)cstr, .count = SDL_strlen(cstr)};
}

bool str_is_valid_utf8(Str s) {
    return simdutf_validate_utf8((const char *)s.ptr, s.count);
}

//
// Vec
//

// Alignment is a property of the elem type, not the vector. A 512 byte-aligned Vec_u8 won't work
// here... but I don't think e.g. GPU texture transfer buffers use Vec atm.
void vec__grow(Arena *arena, GenericVec *vec, u64 elem_size, u64 elem_align, u64 new_count) {
    // Callee checks this for performance (to hopefully avoid calling vec__grow() on each e.g.
    // vec_push())
    log_assert(new_count > vec->capacity);

    u64 old_size = vec->count * elem_size;
    u64 new_capacity = max(MIN_VEC_CAPACITY, next_pow2(new_count));
    u64 new_size = new_capacity * elem_size;
    vec->ptr = arena_realloc_bytes(arena, vec->ptr, old_size, new_size, elem_align);
    vec->capacity = new_capacity;
}

Packer packer_from_arr(Arr_u8 arr) {
    return (Packer){
        .ptr = arr.ptr,
        .offset = 0,
        .capacity = arr.count,
    };
}

// Returns offset iff packed
Opt_u64 packer_try_push(Packer *packer, Arr_u8 buf, u64 alignment) {
    u64 start = align_to(packer->offset, alignment);
    u64 end = start + buf.count;
    if (end > packer->capacity) {
        return none(u64);
    }
    if (buf.count > 0) {
        SDL_memcpy(packer->ptr + start, buf.ptr, end - start);
    }
    packer->offset = end;
    return some(start, u64);
}

//
// Idk
//

void thread_init() {
    arena_pool_init();
}

//
// Encoding/Decoding
//

u64 parse_u64(ErrorContext *err, Str s) {
    Scope scope = scope_open(err);

    if (is_empty(s)) {
        err_report(err, "Empty string");
    }

    u64 result = 0;
    for (u64 i = 0; i < s.count; i++) {
        if (A(s, i) < C('0') || A(s, i) > C('9')) {
            err_report(err, "Non-numeric character");
        }
        u64 new_result = result * 10 + (s.ptr[i] - C('0'));
        if (new_result < result) {
            err_report(err, "Overflow");
        }
        result = new_result;
    }

    scope_close(scope, "Parse '%.*s' as u64", SF(s));
    // In general, should fallible functions return "zero" values on error, or just anything goes?
    return err_occurred(err) ? 0 : result;
}

//
// Idk
//

SDL_IOStream *sdl_io_from_mem(ErrorContext *err, Arr_u8 buf) {
    SDL_IOStream *stream = SDL_IOFromMem(buf.ptr, buf.count);
    if (!stream) {
        err_report(err, "%s", SDL_GetError());
    }
    return stream;
}

void sdl_close_io(SDL_IOStream *stream) {
    if (stream) SDL_CloseIO(stream);
}

//
// Time
//

Instant get_current_monotonic_time() {
    return (Instant){.time_nanoseconds = (i64)SDL_GetTicksNS()};
}

Instant instant_from_sdl_nanos(u64 nanos) {
    return (Instant){.time_nanoseconds = (i64)nanos};
}

//
// Errors
//

Scope scope_open(ErrorContext *err) {
    return (Scope){
        .err = err,
        .last_err_stack_pos = err->ctx_stack.count,
    };
}

__attribute__((format(printf, 2, 3))) void err_report(ErrorContext *err, const char *format, ...) {
    log_assert(err != nullptr);
    if (err->ctx_stack.count == 0) {
        va_list args;
        va_start(args, format);
        Str msg = str_format_v(err->arena, format, args);
        vec_push(err->arena, &err->ctx_stack, msg);
        va_end(args);
    }
}

__attribute__((format(printf, 2, 3))) void scope_close(Scope scope, const char *format, ...) {
    log_assert(scope.err != nullptr);
    if (scope.err->ctx_stack.count > scope.last_err_stack_pos) {
        va_list args;
        va_start(args, format);
        Str msg = str_format_v(scope.err->arena, format, args);
        vec_push(scope.err->arena, &scope.err->ctx_stack, msg);
        va_end(args);
    }
}

bool err_occurred(ErrorContext *err) {
    return err->ctx_stack.count > 0;
}

void err_log(ErrorContext *err) {
    u64 count = err->ctx_stack.count;
    if (count == 0) return;

    Str root_cause = A(err->ctx_stack, count - 1);
    log_error("Failed: %.*s", SF(root_cause));
    if (count > 1) {
        log_error("");
        log_error("Caused By:");
        log_error("");
        for (u64 i = count - 1; i > 0; i--) {
            Str cause = A(err->ctx_stack, i - 1);
            log_error("  Failed: %.*s", SF(cause));
        }
    }
}

//
// Encoding/decoding
//

Arr_u8 decode_base64(ErrorContext *err, Arena *arena, Str s) {
    Scope scope = scope_open(err);

    u64 max_out_size = simdutf_maximal_binary_length_from_base64((const char *)s.ptr, s.count);
    Arr_u8 out = arena_push_arr(arena, u8, max_out_size);
    simdutf_result result =
        simdutf_base64_to_binary((const char *)s.ptr, s.count, (char *)out.ptr,
                                 SIMDUTF_BASE64_DEFAULT, SIMDUTF_LAST_CHUNK_STRICT);
    if (result.error != SIMDUTF_ERROR_SUCCESS) {
        err_report(err, "Invalid base64. Error Code = %d", result.error);
    } else {
        out = arr_slice(out, 0, result.count);
    }

    scope_close(scope, "Decode base64");
    return out;
}
