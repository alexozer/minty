#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../base.h"
#include "../platform.h"

#define ASSERT(cond)                                                 \
    ({                                                               \
        if ((cond) == false) {                                       \
            fprintf(stderr, "Assertion failed: %s\n", #cond);        \
            fprintf(stderr, "  ... at %s:%d\n", __FILE__, __LINE__); \
            abort();                                                 \
        }                                                            \
    })

void render_header_prelude(FILE *out) {
    fprintf(out, "//\n");
    fprintf(out, "// GENERATED FILE - DO NOT MODIFY\n");
    fprintf(out, "//\n");
    fprintf(out, "\n");
    fprintf(out, "#pragma once\n");
    fprintf(out, "\n");
    fprintf(out, "#include \"types.h\"\n");
    fprintf(out, "\n");
}

void process_file(ErrorContext *err, Arena *arena, Str in_path, Str out_path) {
    Arena *scratch = arena_acquire();

    char *out_path_cstr = str_to_c(scratch, out_path);
    FILE *out = fopen(out_path_cstr, "wb");
    ASSERT(out != nullptr);

    render_header_prelude(out);

    Str content = os_read_file(err, arena, in_path);
    StrLineIter iter = str_lines(content);
    Str line = {};
    while (str_lines_next(&iter, &line)) {
        line = str_trim(line);

        Str prefix = S("fn ");
        if (str_starts_with(line, prefix)) {
            Str rest = str_slice(content, (u64)line.ptr - (u64)content.ptr, content.count);
            Opt_u64 brace_pos = str_find(rest, S("{"));
            ASSERT(brace_pos.present);
            Str decl = str_trim(str_slice(rest, 0, brace_pos.opt));

            StrLineIter decl_iter = str_lines(decl);
            Str decl_line = {};
            u64 i = 0;
            while (str_lines_next(&decl_iter, &decl_line)) {
                if (i > 0) fprintf(out, "\n");
                fprintf(out, "%.*s", SF(decl_line));
                i++;
            }
            fprintf(out, ";\n");
        }
    }

    fclose(out);
    arena_release(scratch);
}

int main(int argc, char **argv) {
    thread_init();
    Arena *err_arena = arena_acquire();
    ErrorContext err_base = {.arena = err_arena};
    ErrorContext *err = &err_base;

    Arena *arena = arena_acquire();

    for (u64 i = 1; i < argc; i++) {
        Str c_path = str_from_c(argv[i]);
        Str c_path_prefix = str_slice(c_path, 0, c_path.count - 1);
        Str h_path = str_format(arena, "%.*sh", SF(c_path_prefix));
        process_file(err, arena, c_path, h_path);
    }

    if (err_occurred(err)) {
        err_log(err);
    }

    return err_occurred(err) ? EXIT_FAILURE : EXIT_SUCCESS;
}
