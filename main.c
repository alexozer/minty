// TODO clean up includes
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

#define Kilobytes(n) (n * 1024LL)
#define Megabytes(n) (Kilobytes(n) * 1024LL)

#define AlignTo(n, a) (((n) + (a - 1)) & ~(a - 1))
#define DEFAULT_ALIGNMENT 8

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
    u64 reserved;
    u64 offset;
} Arena;

void _ArenaEnsureInit(Arena *arena) {
    if (arena->data == nullptr) {
        const u64 data_size = Megabytes(16);
        arena->data = calloc(1, data_size);
        arena->reserved = data_size;
    }
}

// TODO deal with e.g. string nonalignment
void *_ArenaPush(Arena *arena, u64 size, u64 alignment) {
    _ArenaEnsureInit(arena);

    void *pos = (void *)((u64)arena->data + arena->offset);
    size = AlignTo(size, alignment);
    arena->offset += size;
    if (arena->offset > arena->reserved) {
         fprintf(stderr, "Arena over!\n");
         exit(EXIT_FAILURE);
    }
    return pos;
}

#define ArenaPush(arena, size) (_ArenaPush((arena), size, DEFAULT_ALIGNMENT))
#define ArenaPushStruct(arena, type) (((type)*)ArenaPush((arena), sizeof(type)))
#define ArenaPushArray(arena, count_, type) \
    ( \
      (type##Array){ \
      .v = _ArenaPush((arena), (count_) * sizeof(type), alignof(type)), \
      .count = (count_), \
      } \
      )

void ArenaRelease(Arena *arena) {
    if (arena->data != nullptr) {
        free(arena->data);
        *arena = (Arena){};
    }
}

#define DefineArray(type) typedef struct { type *v; u64 count; } type##Array

//
// Vec
//

// Embedded into user defined array structs.
#define _VecHeader_ struct { u64 count; u64 capacity; }
typedef struct { u64 count; u64 capacity; } VecHeader;

#define MIN_VEC_CAPACITY 8

#define VecHeaderCast(a) ((VecHeader *)(&a))
#define VecItemSize(a) (sizeof(*(a).v))

void *VecGrow(Arena *arena, VecHeader *header, void *array, u64 item_size, u64 count) {
    const u64 old_size = header->count * item_size;
    const u64 new_size = (header->count + Max(count, MIN_VEC_CAPACITY)) * item_size;

    if (new_size > header->capacity) {
        header->capacity = NextPow2(new_size);
        void *new_array = ArenaPush(arena, header->capacity);
        memcpy(new_array, array, old_size);
        return new_array;
    }

    return array;
}

#define VecPush(arena, a, value) \
    (*((void **)&(a).v) = VecGrow((arena), VecHeaderCast((a)), (a).v, VecItemSize((a)), 1), \
     (a).v[(a).count++] = (value))

#define VecExtend(arena, a, count, values) \
    (*((void **)&(a).v) = VecGrow((arena), VecHeaderCast((a)), (a).v, VecItemSize((a)), count), \
     memcpy((a).v, values, VecItemSize((a)) * count), \
     (a).count += count)

#define VecClear(a) ((a).size = 0)

//
// Strings
//

typedef struct {
    u8 *data;
    u64 size;
} String;

#define S(s) ((String){.data = (u8 *)(s), .size = (sizeof(s)) - 1})

char *StrToCStr(Arena *arena, String s) {
    char *cstr = (char *)ArenaPush(arena, s.size + 1);
    // Compiler plz vectorize
    for (u64 i = 0; i < s.size; i++) {
        cstr[i] = s.data[i];
    }
    // Arena allocation is already zeroed, so null terminator is in place
    return cstr;
}

String StrFromCStr(char *cstr) {
    u64 len = 0;
    for (u64 i = 0; cstr[i] != '\0'; i++) {
        len++;
    }
    return (String){.data = (u8 *)cstr, .size = len};
}

bool StrIsEmpty(String s) {
    return s.size == 0;
}

// Returns a string from a utf8 byte buffer. Doesn't validate if it's actually utf8.
String StrFromBytes(void *buf, u64 size) {
    // Skip utf8 BOM
    u8 *s = (u8 *)buf;
    if (size >= 3 && s[0] == u8'\xef' && s[1] == u8'\xbb' && s[2] == u8'\xbf') {
        s += 3;
        size -= 3;
    }

    return (String){.data = s, .size = size};
}

typedef struct {
    _VecHeader_;
    String *v;
} StringVec;

void StrSplit(Arena *arena, String src, u8 delim, StringVec *out) {
    u64 i = 0;
    String substr = {.data = src.data, .size = 0};
    while (i < src.size) {
        if (src.data[i] == delim) {
            VecPush(arena, *out, substr);
            while (src.data[i] == delim) i++;
            substr = (String){.data = src.data + i, .size = 0};
        } else {
            substr.size++;
            i++;
        }
    }
    if (substr.size > 0) {
        VecPush(arena, *out, substr);
    }
}

// Super loose definition probably
bool CharIsWhitespace(u8 c) {
    return c == u8' ' || c == u8'\r' || c == u8'\n';
}

String StrTrim(String s) {
    u64 start = 0;
    while (start < s.size && CharIsWhitespace(s.data[start])) {
        start++;
    }

    i64 end = ((i64)s.size) - 1;
    while (end >= 0 && CharIsWhitespace(s.data[end])) {
        end--;
    }
    
    return (String){.data = s.data + start, .size = (u64)(end + 1) - start};
}

String StrClone(Arena *arena, String s) {
    void *data = ArenaPush(arena, s.size);
    memcpy(data, s.data, s.size);
    return (String){.data = data, .size = s.size};
}

typedef enum {
    SSF_None = 0,
    SSF_SearchBackwards = 1 << 1,
} StringSearchFlags;

String StrTrimUntil(String s, u8 c, StringSearchFlags flags) {
    if (flags & SSF_SearchBackwards) {
        i64 i = (i64)s.size;
        for (; i >= 0 && s.data[i] != c; i--)
            ;
        return (String){.data = s.data, .size = (u64)(i + 1)};
    } else {
        u64 i = 0;
        for (; i < s.size && s.data[i] != c; i++)
            ;
        return (String){.data = s.data + i, .size = s.size - i};
    }
}

bool StrStartsWith(String s, String prefix) {
    return prefix.size <= s.size && memcmp(s.data, prefix.data, prefix.size) == 0;
}

bool StrEquals(String a, String b) {
     return a.size == b.size && memcmp(a.data, b.data, a.size) == 0;
}

// Certainly possible to do this simply and w/o an iterator object, but just messin around
typedef struct {
    String base;
    u64 pos;
} LineIter;

LineIter StrIterLines(String s) {
     return (LineIter){.base = s, .pos = 0};
}

bool LineIterNext(LineIter* iter, String *out_line) {
    if (iter->pos >= iter->base.size) {
        return false;
    }

    u64 line_start = iter->pos;
    u8 *data = iter->base.data;
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

    if (out_line != nullptr) {
        out_line->data = iter->base.data + line_start;
        out_line->size = line_end - line_start;
    }

    return true;
}

u64 StrCountLines(String s) {
    u64 line_count = 0;
    LineIter iter = StrIterLines(s);
    while (LineIterNext(&iter, nullptr)) {
        line_count++;
    }
    return line_count;
}

// typedef struct {
//     struct { _VecHeader_; u8 *v; } arr;
// } StringBuilder;

// void SBPushStr(Arena *arena, StringBuilder *builder, String s) {
//     VecExtend(arena, builder->arr, s.size, s.data);
// }
//
// // Only works for single byte "characters"
// void SBPushChar(Arena *arena, StringBuilder *builder, u8 c) {
//     VecPush(arena, builder->arr, c);
// }

//
// mmap
//

String MmapFileAsString(Arena *arena, String filepath) {
    char *filepath_cstr = StrToCStr(arena, filepath);

    const i32 fd = open(filepath_cstr, O_RDONLY);
    if (fd == -1) {
        return (String){};
    }
    // Defer(arena /* , close(fd) */);

    struct stat st;
    if (fstat(fd, &st) == -1) {
        return (String){};
    }

    void *buf = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buf == MAP_FAILED) {
        return (String){};
    }
    // Defer(arena /* munmap(buf, st.st_size) */);

    return StrFromBytes(buf, st.st_size);
}

//
// Main
//

DefineArray(String);

StringArray CliGetArgs(Arena *arena, int argc, char **argv) {
    StringArray args = ArenaPushArray(arena, (u64)argc, String);
    for (u64 i = 0; i < args.count; i++) {
        args.v[i] = StrFromCStr(argv[i]);
    }
    return args;
}

// Goal: count lines in file
int main(int argc, char **argv) {
    Arena arena = {};

    StringArray args = CliGetArgs(&arena, argc, argv);
    if (args.count < 2) {
        fprintf(stderr, "Expected arg\n");
        return EXIT_FAILURE;
    }
    String f = MmapFileAsString(&arena, args.v[1]);

    LineIter line_iter = StrIterLines(f);
    String line = {};
    u64 line_num = 1;
    while (LineIterNext(&line_iter, &line)) {
        printf("%03lld: '%s'\n", line_num, StrToCStr(&arena, line));
        line_num++;
    }

    // StringVec words = {};
    // StrSplit(&arena, line, ' ', &words);
    // printf("%lld words\n", words.count);
    // for (u64 i = 0; i < words.count; i++) {
    //      printf("``%s`` ", StrToCStr(&arena, words.v[i]));
    // }
    // printf("\n");

    return EXIT_SUCCESS;
}
