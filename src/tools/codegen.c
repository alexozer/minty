#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../bootstrap.h"

// Shitty vec implementations just to get us by

typedef struct Vec_u8 {
    u8 ptr[256];
    u64 count;
} Vec_u8;

typedef struct Vec_Str {
    Str ptr[256];
    u64 count;
} Vec_Str;

typedef struct Template {
    Str name;
    Vec_Str lines;
} Template;

typedef struct Vec_Template {
    Template ptr[32];
    u64 count;
} Vec_Template;

#define vec_push(vec, value)                            \
    log_assert((vec)->count < c_arr_count((vec)->ptr)); \
    (vec)->ptr[(vec)->count++] = value;

#define vec_push_zero(vec)                                  \
    ({                                                      \
        log_assert((vec)->count < c_arr_count((vec)->ptr)); \
        (vec)->ptr + (vec)->count++;                        \
    })

Str ELEM_TYPES[] = {
    S("u64"),
    S("Str"),
};

// Only does one replacement atm
void str_replace(Str s, Str before, Str after, FILE *out) {
    u64 pos = 0;
    if (str_find(s, before, &pos)) {
        Str left = str_slice(s, 0, pos);
        Str right = str_slice(s, pos + before.count, s.count);
        fprintf(out, "%.*s%.*s%.*s\n", SF(left), SF(after), SF(right));
    } else {
        fprintf(out, "%.*s\n", SF(s));
    }
}

Str read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    assert(fp != nullptr);

    assert(fseek(fp, 0, SEEK_END) == 0);
    i64 size = ftell(fp);
    assert(fseek(fp, 0, SEEK_SET) == 0);

    u8 *buf = calloc((u64)size + 1, 1);
    assert(buf != nullptr);
    assert(fread(buf, 1, (u64)size, fp) > 0);

    fclose(fp);

    return (Str){.ptr = (u8 *)buf, .count = (u64)size};
}

int main(int argc, char **argv) {
    if (argc < 2) {
        log_info("Usage: <filepath>");
        return EXIT_FAILURE;
    }

    Str contents = read_file(argv[1]);
    StrLineIter iter = str_lines(contents);
    Str line = {};

    FILE *out = stdout;

    Vec_Template templates = {};
    Template *curr_template = {};

    while (str_lines_next(&iter, &line)) {
        Str trimmed_line = str_trim(line);
        trimmed_line = str_trim_prefix(trimmed_line, S("//"));
        trimmed_line = str_trim(trimmed_line);

        if (str_starts_with(trimmed_line, S("BEGIN_TEMPLATE_STRUCT"))) {
            curr_template = vec_push_zero(&templates);
            Pair_Str pair = str_split2(trimmed_line, C(' '));
            curr_template->name = pair.right;

        } else if (str_starts_with(trimmed_line, S("END_TEMPLATE_STRUCT"))) {
            curr_template = nullptr;

        } else if (curr_template != nullptr) {
            vec_push(&curr_template->lines, line);
        }
    }

    fprintf(out, "#pragma once\n\n");
    for (u64 et_idx = 0; et_idx < c_arr_count(ELEM_TYPES); et_idx++) {
        Str elem_type = ELEM_TYPES[et_idx];

        for (u64 template_idx = 0; template_idx < templates.count; template_idx++) {
            Template *template = &templates.ptr[template_idx];
            for (u64 i = 0; i < template->lines.count; i++) {
                str_replace(template->lines.ptr[i], S("TYPE"), elem_type, out);
            }
            fprintf(out, "\n");
        }
    }

    fflush(out);
    fclose(out);
    return EXIT_SUCCESS;
}
