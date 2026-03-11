#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

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

//
// Math
//

#define Kilobytes(n) (n * 1024)
#define Megabytes(n) (n * 1024 * 1024)
#define WordAlign(n) ((n + 7) & (~7))

#define Min(a, b) (((a) < (b)) ? a : b)
#define Max(a, b) (((a) > (b)) ? a : b)

// https://jameshfisher.com/2018/03/30/round-up-power-2/
u64 NextPow2(u64 x) {
    x--;
    x |= x>>1;
    x |= x>>2;
    x |= x>>4;
    x |= x>>8;
    x |= x>>16;
    x |= x>>32;
    x++;
    return x;
}

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
// Array
//

// Embedded into user defined array structs.
#define _ArrayHeader_ struct { u64 count; u64 capacity; }
typedef struct { u64 count; u64 capacity; } ArrayHeader;

#define MIN_ARRAY_COUNT 8

#define ArrayHeaderCast(a) ((ArrayHeader *)(&a))
#define ArrayItemSize(a) (sizeof(*(a).v))

void *ArrayGrow(Arena *arena, ArrayHeader *header, void *array, u64 item_size, u64 count) {
    const u64 old_size = header->count * item_size;
    const u64 new_size = (header->count + Max(count, MIN_ARRAY_COUNT)) * item_size;

    if (new_size > header->capacity) {
        header->capacity = NextPow2(new_size);
        void *new_array = ArenaPush(arena, header->capacity);
        memcpy(new_array, array, old_size);
        return new_array;
    }

    return array;
}

#define ArrayPush(arena, a, value) \
    (*((void **)&(a).v) = ArrayGrow((arena), ArrayHeaderCast(a), (a).v, ArrayItemSize(a), 1), \
     (a).v[(a).count++] = (value))

#define ArrayClear(a) ((a).size = 0)

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

// Validates utf8 basically.
// Returns empty string on invalid utf8 lol
String StrFromBytes(void *buf, u64 size) {
    // TODO implement

    // Skip utf8 BOM
    char *s = (char *)buf;
    if (size >= 3 && s[0] == '\xef' && s[1] == '\xbb' && s[2] == '\xbf') {
        s += 3;
        size -= 3;
    }

    return (String){.data = s, .size = size};
}

typedef struct {
    _ArrayHeader_;
    String *v;
} StringArray;

void StrSplit(Arena *arena, String src, char delim, StringArray *out) {
    u64 i = 0;
    String substr = {.data = src.data, .size = 0};
    while (i < src.size) {
        if (src.data[i] == delim) {
            ArrayPush(arena, *out, substr);
            while (src.data[i] == delim) i++;
            substr = (String){.data = src.data + i, .size = 0};
        } else {
            substr.size++;
            i++;
        }
    }
    if (substr.size > 0) {
        ArrayPush(arena, *out, substr);
    }
}

// Certainly possible to do this simply and w/o an iterator object, but just messin around
typedef struct {
    String base;
    u64 pos;
} LineIter;

LineIter StrIterLines(String s) {
     return (LineIter){.base = s, .pos = 0};
}

bool LineIterHasNext(LineIter* iter) {
    return iter->pos < iter->base.size;
}

String LineIterNext(LineIter* iter) {
    u64 line_start = iter->pos;
    char *data = iter->base.data;
    const u64 size = iter->base.size;

    // Advance until next line break
    u64 line_end = line_start;
    while (line_end < size && data[line_end] != '\r' && data[line_end] != '\n') {
        line_end++;
    }

    // Advance past line breaks
    u64 next_line_start = line_end;
    while (next_line_start < size && (data[next_line_start] == '\r')) {
        next_line_start++;
    }
    if (next_line_start < size && (data[next_line_start] == '\n')) {
        next_line_start++;
    }

    iter->pos = next_line_start;
    return (String){.data = iter->base.data + line_start, .size = line_end - line_start};
}

u64 StrCountLines(String s) {
    u64 line_count = 0;
    LineIter iter = StrIterLines(s);
    while (LineIterHasNext(&iter)) {
        LineIterNext(&iter);
        line_count++;
    }
    return line_count;
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

    return StrFromBytes(buf, st.st_size);
}

String CliGetArg(Arena *arena, int argc, char **argv, u64 idx) {
    if (idx + 1 >= argc) {
        return (String){};
    }
    return StrFromCStr(arena, argv[idx + 1]);
}

//
// LSS parse
//

// Goal: count lines in file
int main(int argc, char **argv) {
    Arena arena = {};

    String fname = CliGetArg(&arena, argc, argv, 0);
    String f = MmapFileAsString(&arena, fname);

    LineIter iter = StrIterLines(f);
    String line = LineIterNext(&iter);
    printf("Line: '%s'\n", StrToCStr(&arena, line));

    StringArray words = {};
    StrSplit(&arena, line, ' ', &words);
    printf("%lld words\n", words.count);
    for (u64 i = 0; i < words.count; i++) {
         printf("``%s`` ", StrToCStr(&arena, words.v[i]));
    }
    printf("\n");

    return EXIT_SUCCESS;
}
