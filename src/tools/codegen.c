#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../bootstrap.h"

Str ELEM_TYPES[] = {
    S("u64"),
    S("Str"),
};

const char *TEMPLATES[] = {
    "src/templates.h",
    "src/templates.c",
};

const char *GENCODES[] = {
    "src/generated.h",
    "src/generated.c",
};

// Shitty vec implementations just to get us by

typedef struct Vec_Str {
    Str ptr[128];
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

// Only does one replacement atm
void str_replace(Str s, Str before, Str after, FILE *out) {
    while (true) {
        u64 pos = 0;
        if (str_find(s, before, &pos)) {
            Str left = str_slice(s, 0, pos);
            fprintf(out, "%.*s%.*s", SF(left), SF(after));
            s = str_slice(s, pos + before.count, s.count);
        } else {
            break;
        }
    }
    fprintf(out, "%.*s\n", SF(s));
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

void render_template(Template *template, FILE *out) {
    for (u64 et_idx = 0; et_idx < c_arr_count(ELEM_TYPES); et_idx++) {
        Str elem_type = ELEM_TYPES[et_idx];
        for (u64 i = 0; i < template->lines.count; i++) {
            str_replace(template->lines.ptr[i], S("TYPE"), elem_type, out);
        }
        fprintf(out, "\n");
    }
}

void process_template(const char *template_path, const char *gencode_path) {
    Str contents = read_file(template_path);
    StrLineIter iter = str_lines(contents);
    Str line = {};

    Vec_Template struct_templates = {};
    Vec_Template function_templates = {};
    Template *curr_template = {};

    while (str_lines_next(&iter, &line)) {
        Str trimmed_line = str_trim(line);
        trimmed_line = str_trim_prefix(trimmed_line, S("//"));
        trimmed_line = str_trim(trimmed_line);

        if (str_starts_with(trimmed_line, S("BEGIN_TEMPLATE_STRUCT"))) {
            curr_template = vec_push_zero(&struct_templates);
            Pair_Str pair = str_split2(trimmed_line, C(' '));
            curr_template->name = pair.right;

        } else if (str_starts_with(trimmed_line, S("END_TEMPLATE_STRUCT"))) {
            curr_template = nullptr;
        }

        else if (str_starts_with(trimmed_line, S("BEGIN_TEMPLATE_FUNCTION"))) {
            curr_template = vec_push_zero(&function_templates);
            Pair_Str pair = str_split2(trimmed_line, C(' '));
            curr_template->name = pair.right;

        } else if (str_starts_with(trimmed_line, S("END_TEMPLATE_FUNCTION"))) {
            curr_template = nullptr;

        } else if (curr_template != nullptr) {
            vec_push(&curr_template->lines, line);
        }
    }

    FILE *out = fopen(gencode_path, "wb");
    log_assert(out != nullptr);

    fprintf(out, "//\n");
    fprintf(out, "// GENERATED FILE - DO NOT MODIFY\n");
    fprintf(out, "// Generated from template '%s', modify this instead\n", template_path);
    fprintf(out, "//\n");
    fprintf(out, "\n");
    fprintf(out, "#pragma once\n");
    fprintf(out, "\n");
    fprintf(out, "#include \"bootstrap.h\"\n");
    fprintf(out, "#include \"generated.h\"\n");
    fprintf(out, "\n");

    for (u64 i = 0; i < struct_templates.count; i++) {
        render_template(&struct_templates.ptr[i], out);
    }
    for (u64 i = 0; i < function_templates.count; i++) {
        render_template(&function_templates.ptr[i], out);
    }

    fflush(out);
    fclose(out);
}

int main(int argc, char **argv) {
    thread_init();

    for (u64 i = 0; i < c_arr_count(TEMPLATES); i++) {
        process_template(TEMPLATES[i], GENCODES[i]);
    }
}
