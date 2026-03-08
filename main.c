#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef float f32;
typedef double f64;

#define Kilobytes(n) (n * 1024)
#define Megabytes(n) (n * 1024 * 1024)
#define WordAlign(n) ((n + 7) & (~7))

//
// Arenas
//

typedef struct {
    void *data;
    u64 data_reserved;
    u64 data_offset;

    // Resources cleanup commands
    void *res;
    u64 res_reserved;
    u64 res_offset;
} Arena;

void _ArenaEnsureInit(Arena *arena) {
    if (arena->data == nullptr) {
        const u64 data_size = Megabytes(16);
        arena->data = calloc(1, data_size);
        arena->data_reserved = data_size;

        const u64 res_size = Kilobytes(4);
        arena->res = calloc(1, data_size);
        arena->res_reserved = res_size;
    }
}

void *ArenaPush(Arena *arena, u64 size) {
    _ArenaEnsureInit(arena);

    void *pos = (void *)((u64)arena->data + arena->data_offset);
    size = WordAlign(size);
    arena->data_offset += size;
    if (arena->data_offset > arena->data_reserved) {
         fprintf(stderr, "Arena over!\n");
         exit(EXIT_FAILURE);
    }
    return pos;
}

void ArenaRelease(Arena *arena) {
    if (arena->data != nullptr) {
        free(arena->data);
        free(arena->res);
        *arena = (Arena){};
    }
}

void Defer(Arena *arena) {
}

//
// Strings
//

typedef struct {
    char *data;
    u64 size;
} String;

#define S(s) ((String){.data = (char *)(s), .size = (sizeof(s)) - 1})

char *StrToCStr(Arena *arena, String s) {
    const u64 cstr_size = WordAlign(s.size + 1);
    char *cstr = (char *)ArenaPush(arena, cstr_size);
    // Compiler plz vectorize
    for (u64 i = 0; i < s.size; i++) {
        cstr[i] = s.data[i];
    }
    // Arena allocation is already zeroed, so null terminator is in place
    return cstr;
}

String StrFromCStr(Arena *arena, const char *cstr) {
    u64 len = 0;
    for (u64 i = 0; cstr[i] != '\0'; i++) {
        len++;
    }
    char *data = (char *)ArenaPush(arena, len);
    for (u64 i = 0; i < len; i++) {
         data[i] = cstr[i];
    }
    return (String){.data = data, .size = len};
}

u64 StrCountLines(String s) {
    u64 line_count = 0;
    for (u64 i = 0; i < s.size; i++) {
        if (s.data[i] == '\n') {
            line_count++;
        }
    }
    return line_count;
}

String StrSliceUntil(String s, char c) {
    String substr = {.data = s.data, .size = 0};
    for (u64 i = 0; i < s.size && s.data[i] != c; i++) {
        substr.size++;
    }
    return substr;
}

bool StrIsEmpty(String s) {
    return s.size == 0;
}

//
// mmap
//

String MmapFileAsString(Arena *arena, String filepath) {
    char *filepath_cstr = StrToCStr(arena, filepath);

    const i32 fd = open(filepath_cstr, O_RDONLY);
    if (fd == -1) {
        return (String){};
    }
    Defer(arena /* , close(fd) */);

    struct stat st;
    if (fstat(fd, &st) == -1) {
        return (String){};
    }

    void *buf = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buf == MAP_FAILED) {
        return (String){};
    }
    Defer(arena /* munmap(buf, st.st_size) */);

    // TODO: validate UTF-8

    return (String){.data = (char *)buf, .size = st.st_size};
}

String CliGetArg(Arena *arena, int argc, char **argv, i64 idx) {
    if (idx + 1 >= argc) {
        return (String){};
    }
    return StrFromCStr(arena, argv[idx + 1]);
}

// Goal: count lines in file
int main(int argc, char **argv) {
    Arena arena = {};

    String fname = CliGetArg(&arena, argc, argv, 0);
    // if (StrIsEmpty(fname)) {
    //     fprintf(stderr, "Please provide a file to open\n");
    //     return EXIT_FAILURE;
    // }
    String f = MmapFileAsString(&arena, fname);
    // if (StrIsEmpty(f)) {
    //     fprintf(stderr, "Empty file, or something\n");
    //     return EXIT_FAILURE;
    // }
    const u64 line_count = StrCountLines(f);
    printf("Opened file. Size: %lld, lines: %lld\n", f.size, line_count);

    String first_line = StrSliceUntil(f, '\n');
    printf("First line of file: '%s'\n", StrToCStr(&arena, first_line));

    printf("Arena size: %lld\n", arena.data_offset);

    return EXIT_SUCCESS;
}
