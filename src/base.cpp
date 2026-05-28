#include "base.hpp"

#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "platform.hpp"

// TODO deal with e.g. string nonalignment
void *arena__push_bytes(Arena *arena, u64 size, u64 alignment) {
    arena->offset = align_to(arena->offset, alignment);
    void *pos = (void *)((u64)arena->data + arena->offset);
    arena->offset += align_to(size, alignment);
    if (arena->offset > arena->reserved) {
         log_fatal("Arena over! offset = %" PRIu64 ", reserved = %" PRIu64, arena->offset, arena->reserved);
    }
    return pos;
}

// TODO sane arena sizing/lifetime scheme
static constexpr u64 ARENA_POOL_MAX = 16;
static constexpr u64 ARENA_SIZE = megabytes(16);
static Arena s_arena_pool[ARENA_POOL_MAX];
static Arena *s_arena_stack[ARENA_POOL_MAX];
static u64 s_arena_stack_top;

void arena_pool_init() {
    for (u64 i = 0; i < ARENA_POOL_MAX; i++) {
        s_arena_pool[i].reserved = ARENA_SIZE;
        s_arena_pool[i].data = os_alloc(s_arena_pool[i].reserved);
        s_arena_stack[i] = &s_arena_pool[i];
    }
}

Arena *arena_acquire() {
    if (s_arena_stack_top >= ARENA_POOL_MAX) {
        log_fatal("FATAL: out of arenas");
    }
    return s_arena_stack[s_arena_stack_top++];
}

void arena_release(Arena *arena) {
    if (s_arena_stack_top == 0) {
        log_fatal("FATAL: tried to release too many arenas!");
    }
    s_arena_stack[--s_arena_stack_top] = arena;
    memset(arena->data, 0, arena->offset);
    arena->offset = 0;
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

    u64 end = s.count;
    while (end > 0 && char_is_whitespace(s[end - 1])) {
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

__attribute__((format(printf, 2, 3)))
Str str_format(Arena *arena, const char *format, ...) {
    char buf[kilobytes(8)];

    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (n < 0) {
        return S("<formatting error>");
    }

    Str s = { .value = (u8 *)buf, .count = (u64)n };
    // TODO try to allocate directly on tip of arena?
    return str_clone(arena, s);
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
    FILE *out;
    LogLevel level;
};

static struct {
    void *udata;
    LogLevel level;
} s_log;

static const char *level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char *level_colors[] = {
    "\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
};

static void log_stderr_callback(LogEvent *ev) {
    char buf[16];
    buf[strftime(buf, sizeof(buf), "%H:%M:%S", ev->time)] = '\0';
    fprintf(
            ev->out, "\x1b[90m%s %s%-5s \x1b[0m",
            buf, level_colors[(int)ev->level], level_strings[(int)ev->level]);
    vfprintf(ev->out, ev->fmt, ev->ap);
    fprintf(ev->out, "\n");
    fflush(ev->out);
}

void log_set_level(LogLevel level) {
    s_log.level = level;
}

static void init_event(LogEvent *ev, FILE *out) {
    if (!ev->time) {
        time_t t = time(NULL);
        ev->time = localtime(&t);
        ev->out = out;
    }
}

__attribute__((format(printf, 1, 2)))
[[noreturn]] void log_fatal(const char *fmt, ...) {
    LogEvent ev = {
        .fmt   = fmt,
        .level = LogLevel::Fatal,
    };

    init_event(&ev, stderr);
    va_start(ev.ap, fmt);
    log_stderr_callback(&ev);
    va_end(ev.ap);

    exit(EXIT_FAILURE);
}

__attribute__((format(printf, 2, 3)))
void log_log(LogLevel level, const char *fmt, ...) {
    LogEvent ev = {
        .fmt   = fmt,
        .level = level,
    };

    if ((int)level >= (int)s_log.level) {
        init_event(&ev, stderr);
        va_start(ev.ap, fmt);
        log_stderr_callback(&ev);
        va_end(ev.ap);
    }
}

//
// XML parser
//

// TODO:
// Iterator functions
// Escape codes (escape in-place?)
//
// Separate C99 library
// Code golf it a bit

bool xao__is_whitespace(u8 c) {
    return c == C(' ') || c == C('\r') || c == C('\n') || c == C('\t');
}

bool xao__is_string(const u8 *cur, const u8 *end, const char *expect) {
    while (*expect) {
        if (cur == end || *cur != *expect) return false;
        expect++, cur++;
    }
    return true;
}

bool xao__advance_till(XaoReader *r, const char *s) {
    while (true) {
        r->curr = (u8 *)memchr(r->curr, s[0], (size_t)(r->end - r->curr));
        if (r->curr == nullptr) return false;
        if (xao__is_string(r->curr, r->end, s)) return true;
        r->curr++;
    }
    return false;
}

XaoValue xao__read(XaoReader *r) {
top:
    XaoValue res = { .type = XaoThing::Error, .depth = r->depth };
    if (!str_is_empty(r->error)) { res.start = r->end; res.end = r->end; return res; }
    if (r->curr == r->end) { r->error = S("unexpected eof"); goto top; }

    if (r->in_tag) {
        if (xao__is_whitespace(*r->curr)) { r->curr++; goto top; }
        if (*r->curr == C('>')) { r->curr++; r->in_tag = false; goto top; }

        // Element end
        if (*r->curr == C('?') || *r->curr == C('/')) {
            if (!xao__advance_till(r, ">")) { r->error = S("unfinished tag"); goto top; }
            r->depth--; r->curr++; r->in_tag = false; goto top;
        }

        // Attr value
        if (xao__is_string(r->curr, r->end, "=\"")) {
            r->curr += 2;
            res.type = XaoThing::AttrValue;
            res.start = r->curr;
            if (!xao__advance_till(r, "\"")) { r->error = S("unfinished attr value"); goto top; }
            res.end = r->curr++;
            return res;
        }

        // Attr name
        res.type = XaoThing::AttrName;
        res.start = r->curr;
        if (!xao__advance_till(r, "=")) { r->error = S("unfinished attr name"); goto top; }
        res.end = r->curr;
        return res;
    }

    // In element body

    if (xao__is_string(r->curr, r->end, "</")) { r->curr += 1; r->in_tag = true; goto top; }

    // Element opening tag
    if (*r->curr == C('<')) {
        // Comment
        if (xao__is_string(r->curr, r->end, "<!--")) {
            if (!xao__advance_till(r, "-->")) { r->error = S("unfinished comment"); goto top; }
            r->curr += 4;
            goto top;
        }

        // CDATA content
        if (xao__is_string(r->curr, r->end, "<![CDATA[")) {
            res.type = XaoThing::Content;
            res.start = r->curr += 9;
            if (!xao__advance_till(r, "]]>")) { r->error = S("unfinished CDATA"); goto top; }
            res.end = r->curr;
            r->curr += 3;
            return res;
        }

        // Opening tag (just the name)
        res.type = XaoThing::OpeningTagName;
        r->in_tag = true;
        res.start = ++r->curr;
        while (true) {
            if (r->curr == r->end) { r->error = S("unfinished tag name"); goto top; }
            if (xao__is_whitespace(*r->curr) || *r->curr == C('>')) break;
            r->curr++;
        }
        res.end = r->curr;
        res.depth = ++r->depth;
        return res;
    }

    // Content
    res.type = XaoThing::Content;
    res.start = r->curr;
    if (!xao__advance_till(r, "<")) { r->error = S("unfinished content"); goto top; }
    res.end = r->curr;
    return res;
}

XaoReader xao_reader(Str document) {
    return {
        .data = document.value,
        .curr = document.value,
        .end = document.value + document.count,
    };
}

bool xao_iter_tags(XaoReader *reader, XaoValue tag, XaoValue *child_tag) {
    return false;
}

bool xao_iter_content(XaoReader *reader, XaoValue tag, XaoValue *content) {
    return false;
}

bool xao_iter_attrs(XaoReader *reader, XaoValue tag, XaoValue *attr_name, XaoValue *attr_value) {
    return false;
}
