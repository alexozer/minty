#include "platform.h"

#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base.h"

void *os_alloc(u64 size) {
    void *buf = mmap(nullptr, (size_t)size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (buf == nullptr) {
        log_fatal("mmap failed");
    }
    return buf;
}

void os_free(void *buf, u64 size) {
    munmap(buf, (size_t)size);
}

Arr_u8 os_read_file(ErrorContext *err, Arena *arena, Str path) {
    Arena *scratch = arena_acquire();
    Scope scope = scope_open(err);

    char *path_cstr = str_to_c(scratch, path);
    int fd = open(path_cstr, O_RDONLY);
    if (fd == -1) {
        err_report(err, "Failed to open file");
    }

    struct stat st = {};
    if (fstat(fd, &st) == -1) {
        err_report(err, "Failed to call stat() on file");
    }
    u64 size = (u64)st.st_size;

    Arr_u8 buf = arena_push_arr(arena, u8, size);
    u64 pos = 0;
    while (pos < size) {
        i64 n = read(fd, buf.ptr + pos, size - pos);
        if (n == -1) break;
        pos += (u64)n;
    }
    if (pos != size) {
        buf = (Arr_u8){};
        err_report(err, "Error while reading file");
    }

    close(fd);
    scope_close(scope, "Read file into memory: '%.*s'", SF(path));
    arena_release(scratch);
    return buf;
}
