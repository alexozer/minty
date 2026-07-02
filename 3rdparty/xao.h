// xao.h v0.2 - Alex Ozer
// Public domain - no warranty implied, use at your own risk

#ifndef XAO_H
#define XAO_H

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

enum { XAO_NONE, XAO_TAG, XAO_ATTR_NAME, XAO_ATTR_VALUE, XAO_CONTENT };

typedef struct {
    char *start, *end;
    int type;
    int depth;
} xao_Value;

typedef struct {
    char *data, *curr, *end;
    int depth;
    int state;
    char *error;
} xao_Reader;

xao_Reader xao_reader(char *data, size_t len);

bool xao_iter_attrs(xao_Reader *r, xao_Value tag, xao_Value *attr_name, xao_Value *attr_value);
bool xao_iter_content(xao_Reader *r, xao_Value parent, xao_Value *content);
bool xao_iter_tags(xao_Reader *r, xao_Value parent, xao_Value *child);

#endif // #ifdef XAO_IMPL

#ifdef XAO_IMPL

enum { XAO_S_CONTENT, XAO_S_TAG, XAO_S_END_ATTRS, XAO_S_END_ELEM, XAO_S_EOF };

static bool xao__is_whitespace(char c) {
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

static bool xao__is_string(char *cur, char *end, char *expect) {
    while (*expect) {
        if (cur == end || *cur != *expect) return false;
        expect++, cur++;
    }
    return true;
}

static bool xao__advance_until(xao_Reader *r, char *s) {
    while (true) {
        r->curr = memchr(r->curr, *s, r->end - r->curr);
        if (r->curr == NULL) return false;
        if (xao__is_string(r->curr, r->end, s)) return true;
        r->curr++;
    }
    return false;
}

const char *esc_codes[] = { "&lt;", "&gt;", "&amp;", "&apos;", "&quot;" };
const char esc_chars[] = { '<', '>', '&', '\'', '"' };

static char *xao__unescape(char *start, char *end) {
    char *base = start;
top: {
    char *esc = memchr(start, '&', end - start);
    if (esc == NULL) {
        memmove(base, start, end - start);
        return base + (end - start);
    }
    for (int i = 0; i < (sizeof esc_codes / sizeof esc_codes[0]); i++) {
        if (xao__is_string(esc, end, esc_codes[i])) {
            *esc = esc_chars[i];
            memmove(base, start, esc - start + 1);
            base += esc - start + 1;
            start = esc + strlen(esc_codes[i]);
            goto top;
        }
    }
    start++; goto top; // Unsupported code
}}

static xao_Value xao__read(xao_Reader *r) {
top: {
    if (r->curr == r->data) r->depth++;
    xao_Value res = { .depth = r->depth };
    if (r->error != NULL) { res.start = r->end; res.end = r->end; return res; }
    if (r->curr == r->end) {
        if (r->depth == 1) { r->state = XAO_S_EOF; return res; }
        r->error = "unexpected eof"; goto top;
    }

    if (r->state == XAO_S_TAG) {
        if (xao__is_whitespace(*r->curr)) { r->curr++; goto top; }
        if (*r->curr == '>') { r->curr++; r->depth++; r->state = XAO_S_END_ATTRS; return res; }

        // Closing tag
        if (*r->curr == '?' || *r->curr == '/') {
            if (!xao__advance_until(r, ">")) { r->error = "missing >"; goto top; }
            r->state = XAO_S_END_ELEM;
            r->curr++;
            return res;
        }

        // Attr value
        if (xao__is_string(r->curr, r->end, "=\"") || xao__is_string(r->curr, r->end, "='")) {
            bool dq = r->curr[1] == '"';
            r->curr += 2;
            res.type = XAO_ATTR_VALUE;
            res.start = r->curr;
            if (!xao__advance_until(r, dq ? "\"" : "'")) { r->error = "missing quote"; goto top; }
            res.end = r->curr++;
            return res;
        }
        if (*r->curr == '=') { r->error = "unexpected ="; goto top; }

        // Attr name
        res.type = XAO_ATTR_NAME;
        res.start = r->curr;
        if (!xao__advance_until(r, "=")) { r->error = "missing ="; goto top; }
        res.end = r->curr;
        return res;
    }

    if (r->state == XAO_S_END_ATTRS || r->state == XAO_S_END_ELEM) {
        r->state = XAO_S_CONTENT; goto top;
    }

    if (xao__is_string(r->curr, r->end, "</")) {
        if (!xao__advance_until(r, ">")) { r->error = "missing >"; goto top; }
        r->state = XAO_S_END_ELEM; r->curr++; r->depth--; return res;
    }

    // Comment
    if (xao__is_string(r->curr, r->end, "<!--")) {
        if (!xao__advance_until(r, "-->")) { r->error = "missing -->"; goto top; }
        r->curr += 3;
        goto top;
    }

    // CDATA content
    if (xao__is_string(r->curr, r->end, "<![CDATA[")) {
        res.type = XAO_CONTENT;
        res.start = r->curr += 9;
        if (!xao__advance_until(r, "]]>")) { r->error = "missing ]]>"; goto top; }
        res.end = r->curr; r->curr += 3;
        return res;
    }

    if (*r->curr == '<') {
        // Opening tag (just the name)
        r->state = XAO_S_TAG;
        res.type = XAO_TAG;
        res.start = ++r->curr;
        while (true) {
            if (r->curr == r->end) { r->error = "unfinished tag name"; goto top; }
            if (xao__is_whitespace(*r->curr) || *r->curr == '>') break;
            r->curr++;
        }
        res.end = r->curr;
        res.depth = r->depth;
        return res;
    }

    // Content
    res.type = XAO_CONTENT;
    res.start = r->curr;
    if (!xao__advance_until(r, "<")) r->curr = r->end;
    res.end = xao__unescape(res.start, r->curr);
    return res;
}}

xao_Reader xao_reader(char *data, size_t len) {
    return (xao_Reader){ .data = data, .curr = data, .end = data + len };
}

bool xao_iter_attrs(xao_Reader *r, xao_Value tag, xao_Value *attr_name, xao_Value *attr_value) {
    if (r->error != NULL || r->state >= XAO_S_END_ATTRS) return false;
    xao_Value name = xao__read(r);
    if (r->error != NULL || r->state >= XAO_S_END_ATTRS) return false;
    xao_Value value = xao__read(r);
    if (r->error != NULL) return false;
    *attr_name = name; *attr_value = value;
    return true;
}

bool xao_iter_content(xao_Reader *r, xao_Value parent, xao_Value *content) {
    while (true) {
        if (r->error != NULL || r->state == XAO_S_EOF) return false;
        if (r->depth == parent.depth && r->state == XAO_S_END_ELEM) return false;
        xao_Value v = xao__read(r);
        if (r->error != NULL) return false;
        if (r->depth == parent.depth + 1 && (v.type == XAO_TAG || v.type == XAO_CONTENT)) {
            *content = v;
            return true;
        }
    }
}

bool xao_iter_tags(xao_Reader *r, xao_Value parent, xao_Value *child) {
    while (true) {
        xao_Value v = {};
        if (!xao_iter_content(r, parent, &v)) return false;
        if (v.type == XAO_TAG) { *child = v; return true; }
    }
}

#endif // #ifdef XAO_IMPL
