// TODO:
// Iterator functions
// Escape codes (escape in-place?)
// Code golf it a bit

#ifndef XAO_H
#define XAO_H

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

enum { XAO_ERROR, XAO_TAG, XAO_ATTR_NAME, XAO_ATTR_VALUE, XAO_CONTENT };

typedef struct {
    char *start; char *end;
    int type;
    int depth;
} xao_Value;

typedef struct {
    char *data; char *curr; char *end;
    int depth;
    bool in_tag;
    char *error;
} xao_Reader;

xao_Reader xao_reader(char *data, size_t len);
bool xao_iter_children(xao_Reader *reader, xao_Value tag, xao_Value *content);
bool xao_iter_attrs(xao_Reader *reader, xao_Value tag, xao_Value *attr_name, xao_Value *attr_value);

// TODO remove
xao_Value xao__read(xao_Reader *r);

#endif // #ifndef SJ_H

// #ifdef XAO_IMPL

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
        r->curr = memchr(r->curr, s[0], (size_t)(r->end - r->curr));
        if (r->curr == NULL) return false;
        if (xao__is_string(r->curr, r->end, s)) return true;
        r->curr++;
    }
    return false;
}

// TODO make static
xao_Value xao__read(xao_Reader *r) {
top: {
    xao_Value res = { .depth = r->depth };
    if (r->error != NULL) { res.start = r->end; res.end = r->end; return res; }
    if (r->curr == r->end) { r->error = "unexpected eof"; goto top; }

    if (r->in_tag) {
        if (xao__is_whitespace(*r->curr)) { r->curr++; goto top; }
        if (*r->curr == '>') { r->curr++; r->in_tag = false; goto top; }

        // Closing tag
        if (*r->curr == '?' || *r->curr == '/') {
            if (!xao__advance_until(r, ">")) { r->error = "unfinished tag"; goto top; }
            r->depth--; r->curr++; r->in_tag = false; goto top;
        }

        // Attr value
        if (xao__is_string(r->curr, r->end, "=\"")) {
            r->curr += 2;
            res.type = XAO_ATTR_VALUE;
            res.start = r->curr;
            if (!xao__advance_until(r, "\"")) { r->error = "unfinished attr value"; goto top; }
            res.end = r->curr++;
            return res;
        }

        // Attr name
        res.type = XAO_ATTR_NAME;
        res.start = r->curr;
        if (!xao__advance_until(r, "=")) { r->error = "unfinished attr name"; goto top; }
        res.end = r->curr;
        return res;
    }

    // In element body

    if (xao__is_string(r->curr, r->end, "</")) { r->curr += 1; r->in_tag = true; goto top; }

    // Element opening tag
    if (*r->curr == '<') {
        // Comment
        if (xao__is_string(r->curr, r->end, "<!--")) {
            if (!xao__advance_until(r, "-->")) { r->error = "unfinished comment"; goto top; }
            r->curr += 4;
            goto top;
        }

        // CDATA content
        if (xao__is_string(r->curr, r->end, "<![CDATA[")) {
            res.type = XAO_CONTENT;
            res.start = r->curr += 9;
            if (!xao__advance_until(r, "]]>")) { r->error = "unfinished CDATA"; goto top; }
            res.end = r->curr; r->curr += 3;
            return res;
        }

        // Opening tag (just the name)
        res.type = XAO_TAG;
        r->in_tag = true;
        res.start = ++r->curr;
        while (true) {
            if (r->curr == r->end) { r->error = "unfinished tag name"; goto top; }
            if (xao__is_whitespace(*r->curr) || *r->curr == '>') break;
            r->curr++;
        }
        res.end = r->curr;
        res.depth = ++r->depth;
        return res;
    }

    // Content
    res.type = XAO_CONTENT;
    res.start = r->curr;
    if (!xao__advance_until(r, "<")) { r->error = "unfinished content"; goto top; }
    res.end = r->curr;
    return res;
}}

xao_Reader xao_reader(char *data, size_t len) {
    return (xao_Reader){ .data = data, .curr = data, .end = data + len };
}

bool xao_iter_children(xao_Reader *reader, xao_Value tag, xao_Value *content) {
    return false;
}

bool xao_iter_attrs(xao_Reader *reader, xao_Value tag, xao_Value *attr_name, xao_Value *attr_value) {
    return false;
}

// #endif // #ifdef XAO_IMPL
