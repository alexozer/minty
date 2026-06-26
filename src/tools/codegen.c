#include <stdlib.h>
#include "../base.h"

typedef struct {
    u8 *ptr;
    u64 count;
    u64 cap;
} StrBuilder;

Str ARR_INSTS[] = {
    S("u64"),
    S("Str"),
};

Str read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    log_assert(fp != NULL);

    log_assert(fseek(fp, 0, SEEK_END) == 0);
    i64 size = ftell(fp);
    log_assert(fseek(fp, 0, SEEK_SET) == 0);

    u8 *buf = calloc((u64)size + 1, 1);
    log_assert(buf != NULL);
    log_assert(fread(buf, 1, (u64)size, fp) > 0);

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
    while (str_lines_next(&iter, &line)) {
        if (str_starts_with(line, S("BEGIN_TEMPLATE_FUNCTION"))) {
        }
    }
    return EXIT_SUCCESS;
}
