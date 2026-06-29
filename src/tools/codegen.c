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

typedef struct Type {
    Str name;
    u64 pointer_levels;
} Type;

typedef struct Binding {
    Str var_name;
    Type type;
} Binding;

// Shitty vec implementations just to get us by

typedef struct Vec_Str {
    Str ptr[128];
    u64 count;
} Vec_Str;

typedef struct Vec_Binding {
    Binding ptr[128];
    u64 count;
} Vec_Binding;

typedef struct StructTemplate {
    Str name;
    Vec_Str lines;
} StructTemplate;

typedef struct FunctionTemplate {
    Str name;
    Vec_Binding args;
    Vec_Str lines;
} FunctionTemplate;

typedef struct Vec_StructTemplate {
    StructTemplate ptr[32];
    u64 count;
} Vec_StructTemplate;

typedef struct Vec_FunctionTemplate {
    FunctionTemplate ptr[32];
    u64 count;
} Vec_FunctionTemplate;

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

void render_template(Vec_Str *lines, FILE *out) {
    for (u64 et_idx = 0; et_idx < c_arr_count(ELEM_TYPES); et_idx++) {
        Str elem_type = ELEM_TYPES[et_idx];
        for (u64 i = 0; i < lines->count; i++) {
            str_replace(lines->ptr[i], S("TYPE"), elem_type, out);
        }
        fprintf(out, "\n");
    }
}

void render_generic_function(Str name, Vec_Binding *args, FILE *out) {
    u64 generic_arg_idx = args->count;
    for (u64 i = 0; i < args->count; i++) {
        if (str_contains(args->ptr[i].type.name, S("TYPE"))) {
            generic_arg_idx = i;
            break;
        }
    }
    if (generic_arg_idx == args->count) {
        for (u64 i = 0; i < args->count; i++) {
            log_error("Arg %" PRIu64 ": %.*s", i, SF(args->ptr[i].type.name));
        }
        log_fatal("Function '%.*s' is not generic!", SF(name));
    }

    // Intro
    fprintf(out, "#define %.*s(", SF(name));
    for (u64 i = 0; i < args->count; i++) {
        if (i != 0) {
            fprintf(out, ", ");
        }
        fprintf(out, "%.*s", SF(args->ptr[i].var_name));
    }
    Str generic_arg_name = args->ptr[generic_arg_idx].var_name;
    fprintf(out, ") _Generic((%.*s), \\\n", SF(generic_arg_name));

    // Type -> function mapping
    for (u64 i = 0; i < c_arr_count(ELEM_TYPES); i++) {
        Str elem_type = ELEM_TYPES[i];
        Str comma = i == c_arr_count(ELEM_TYPES) - 1 ? S("") : S(",");
        fprintf(out, "    %.*s: %.*s_%.*s%.*s \\\n", SF(elem_type), SF(name), SF(elem_type),
                SF(comma));
    }

    // Invocation
    fprintf(out, ")(");
    for (u64 i = 0; i < args->count; i++) {
        if (i != 0) {
            fprintf(out, ", ");
        }
        fprintf(out, "%.*s", SF(args->ptr[i].var_name));
    }
    fprintf(out, ")\n\n");
}

Binding parse_binding(Str s) {
    Binding binding = {};
    u64 star_start = 0;
    if (str_find(s, S("*"), &star_start)) {
        u64 star_end = star_start;
        while (s.ptr[star_end] == '*') {
            star_end++;
        }
        binding.type.name = str_trim(str_slice(s, 0, star_start));
        binding.var_name = str_trim(str_slice(s, star_end, s.count));
        binding.type.pointer_levels = star_end - star_start;
    } else {
        Pair_Str pair = str_split2(s, ' ');
        binding.type.name = str_trim(pair.left);
        binding.var_name = str_trim(pair.right);
        binding.type.pointer_levels = 0;
    }
    return binding;
}

void parse_signature(Str body, Vec_Binding *out_args) {
    u64 start_paren = 0;
    u64 end_paren = 0;
    log_assert(str_find(body, S("("), &start_paren));
    log_assert(str_find(body, S(")"), &end_paren));
    Str args = str_slice(body, start_paren + 1, end_paren);

    while (true) {
        if (str_is_empty(args)) break;
        Pair_Str pair = str_split2(args, C(','));
        vec_push(out_args, parse_binding(str_trim(pair.left)));
        args = pair.right;
    }
}

void write_generic_type(Str base_name, Type type, FILE *out) {
    u64 pos = 0;
    log_assert(str_find(base_name, S("TYPE"), &pos));
    Str prefix = str_slice(base_name, 0, pos);
    fprintf(out, "%.*s", SF(prefix));

    for (u64 i = 0; i < type.pointer_levels; i++) {
        fprintf(out, "_P");
    }

    fprintf(out, "_%.*s", SF(type.name));
}

void process_template(const char *template_path, const char *gencode_path) {
    Str contents = read_file(template_path);
    StrLineIter iter = str_lines(contents);
    Str line = {};

    Vec_StructTemplate struct_templates = {};
    Vec_FunctionTemplate function_templates = {};
    StructTemplate *curr_struct_template = {};
    FunctionTemplate *curr_function_template = {};

    while (str_lines_next(&iter, &line)) {
        Str trimmed_line = str_trim(line);
        trimmed_line = str_trim_prefix(trimmed_line, S("//"));
        trimmed_line = str_trim(trimmed_line);

        if (str_starts_with(trimmed_line, S("BEGIN_TEMPLATE_STRUCT"))) {
            curr_struct_template = vec_push_zero(&struct_templates);
            Pair_Str pair = str_split2(trimmed_line, C(' '));
            curr_struct_template->name = pair.right;

        } else if (str_starts_with(trimmed_line, S("END_TEMPLATE_STRUCT"))) {
            curr_struct_template = nullptr;
        }

        else if (str_starts_with(trimmed_line, S("BEGIN_TEMPLATE_FUNCTION"))) {
            curr_function_template = vec_push_zero(&function_templates);
            Pair_Str pair = str_split2(trimmed_line, C(' '));
            curr_function_template->name = pair.right;

            Str body = str_slice(contents, (u64)(line.ptr - contents.ptr), contents.count);
            parse_signature(body, &curr_function_template->args);

        } else if (str_starts_with(trimmed_line, S("END_TEMPLATE_FUNCTION"))) {
            curr_function_template = nullptr;

        } else if (curr_struct_template != nullptr) {
            vec_push(&curr_struct_template->lines, line);

        } else if (curr_function_template != nullptr) {
            vec_push(&curr_function_template->lines, line);
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
        render_template(&struct_templates.ptr[i].lines, out);
    }
    for (u64 i = 0; i < function_templates.count; i++) {
        render_template(&function_templates.ptr[i].lines, out);
        render_generic_function(function_templates.ptr[i].name, &function_templates.ptr[i].args,
                                out);
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
