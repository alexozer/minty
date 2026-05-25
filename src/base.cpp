#include "base.hpp"

#include <fcntl.h>
#include <time.h>
#include <string.h>

extern "C" {
#include "stb_sprintf.h"
}

#include "platform.hpp"

void arena__ensure_init(Arena *arena) {
    if (arena->data == nullptr) {
        arena->reserved = megabytes(16);
        arena->data = os_alloc(arena->reserved);
    }
}

// TODO deal with e.g. string nonalignment
void *arena__push_bytes(Arena *arena, u64 size, u64 alignment) {
    arena__ensure_init(arena);

    void *pos = (void *)((u64)arena->data + arena->offset);
    size = align_to(size, alignment);
    arena->offset += size;
    if (arena->offset > arena->reserved) {
         log_fatal("Arena over! offset = %ull, reserved = %ull", arena->offset, arena->reserved);
    }
    return pos;
}

void arena_release(Arena *arena) {
    if (arena->data != nullptr) {
        os_free(arena->data, arena->reserved);
        *arena = (Arena){};
    }
}

void arena_reset(Arena *arena) {
    if (arena->data != nullptr) {
        memset(arena->data, 0, arena->offset);
        arena->offset = 0;
    }
}

char *str_to_c(Arena *arena, Str s) {
    Arr<char> cstr = arena_push_arr<char>(arena, s.count + 1);
    memcpy(cstr.value, s.value, s.count);
    return cstr.value;
}

Str str_from_c(const char *cstr) {
    u64 count = 0;
    while (cstr[count] != '\0') count++;
    return (Str){ .value = (u8 *)cstr, .count = count };
}

Str str_from_c_len(const char *cstr, u64 len) {
    return { . value = (u8 *)cstr, .count = len };
}

// Returns a string from a utf8 byte buffer. Doesn't validate if it's actually utf8.
Str str_from_bytes(Arr<u8> bytes) {
    // Skip utf8 BOM
    u64 start = 0;
    if (bytes.count >= 3 && bytes[0] == C('\xef') && bytes[1] == C('\xbb') && bytes[2] == C('\xbf')) {
        start = 3;
    }

    return arr_slice(bytes, start, bytes.count);
}

// Super conservative definition probably
bool char_is_whitespace(u8 c) {
    return c == C(' ') || c == C('\r') || c == C('\n');
}

Str str_trim(Str s) {
    u64 start = 0;
    while (start < s.count && char_is_whitespace(s[start])) {
        start++;
    }

    i64 end = ((i64)s.count) - 1;
    while (end >= 0 && char_is_whitespace(s[end])) {
        end--;
    }

    return arr_slice(s, start, end);
}

Str str_clone(Arena *arena, Str s) {
    Str clone = arena_push_arr<u8>(arena, s.count);
    memcpy(clone.value, s.value, s.count);
    return clone;
}

bool str_starts_with(Str s, Str prefix) {
    if (prefix.count > s.count) {
        return false;
    }
    Str s_prefix = arr_slice(s, 0, prefix.count);
    return arr_eq(prefix, s_prefix);
}

StrLineIter str_lines(Str s) {
     return (StrLineIter){ .base = s, .pos = 0 };
}

bool str_lines_next(StrLineIter* iter, Str *line) {
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
        line->value = iter->base.value + line_start;
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

Pair<Str, Str> str_split2(Str base, u8 delim) {
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

struct StbspContext {
    Arena *arena;
    Vec<u8> *out_str;
    char buf[STB_SPRINTF_MIN];
};

char *stbsp_callback(char const *buf, void *user, int len) {
    StbspContext *ctx = (StbspContext *)user;
    Str substr = { .value = (u8 *)buf, .count = (u64)len };
    vec_extend(ctx->arena, ctx->out_str, substr);
    return ctx->buf;
}

void str_format_append_v(Arena *arena, Vec<u8> *out_str, const char *format, va_list args) {
    StbspContext ctx = { .arena = arena, .out_str = out_str };
    stbsp_vsprintfcb(&stbsp_callback, &ctx, ctx.buf, format, args);
}

Str str_format(Arena *arena, const char *format, ...) {
    va_list args;
    va_start(args, format);
    Vec<u8> out_str = {};
    str_format_append_v(arena, &out_str, format, args);
    va_end(args);
    return vec_arr(&out_str);
}

void str_format_append(Arena *arena, Vec<u8>* out_str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    str_format_append_v(arena, out_str, format, args);
    va_end(args);
}

//
// Paths
//

Str path_join(Arena *arena, Str left_path, Str right_path) {
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

Arr<char *> g_envp;

Str env_get(Str key) {
    Arena scratch = {};
    defer(arena_release(&scratch));

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

struct LogEvent {
    va_list ap;
    const char *fmt;
    struct tm *time;
    LogLevel level;
};

static struct {
    void *udata;
    LogLevel level;
} s_log;

static const Str level_strings[] = {
    S("TRACE"), S("DEBUG"), S("INFO"), S("WARN"), S("ERROR"), S("FATAL")
};

static const Str level_colors[] = {
    S("\x1b[94m"), S("\x1b[36m"), S("\x1b[32m"), S("\x1b[33m"), S("\x1b[31m"), S("\x1b[35m")
};

static void log_stderr_callback(LogEvent *ev) {
    Arena scratch = {}; // TODO preallocate arenas
    defer(arena_release(&scratch));

    Vec<u8> out_str = {};

    // Write date
    char date_buf[16];
    u64 date_size = strftime(date_buf, sizeof(date_buf), "%H:%M:%S", ev->time);
    Str date_str = str_from_c_len(date_buf, date_size);
    vec_extend(&scratch, &out_str, date_str);

    // Write level
    str_format_append(
            &scratch,
            &out_str,
            "\x1b[90m %s%-5s \x1b[0m",
            level_colors[(int)ev->level],
            level_strings[(int)ev->level]);

    // Write message
    str_format_append_v(&scratch, &out_str, ev->fmt, ev->ap);

    vec_push(&scratch, &out_str, C('\n'));
    os_write_stderr(vec_arr(&out_str));
}

void log_set_level(LogLevel level) {
    s_log.level = level;
}

static void init_event(LogEvent *ev) {
    if (!ev->time) {
        time_t t = time(NULL);
        ev->time = localtime(&t);
    }
}

[[noreturn]] void log_fatal(const char *fmt, ...) {
    LogEvent ev = {
        .fmt   = fmt,
        .level = LogLevel::Fatal,
    };

    init_event(&ev);
    va_start(ev.ap, fmt);
    log_stderr_callback(&ev);
    va_end(ev.ap);

    os_exit();
}

void log_log(LogLevel level, const char *fmt, ...) {
    LogEvent ev = {
        .fmt   = fmt,
        .level = level,
    };

    if ((int)level >= (int)s_log.level) {
        init_event(&ev);
        va_start(ev.ap, fmt);
        log_stderr_callback(&ev);
        va_end(ev.ap);
    }
}
