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

#define ArenaPush(arena, size) (ArenaPush((arena), size, DEFAULT_ALIGNMENT))
#define ArenaPushStruct(arena, type) (((type)*)ArenaPush((arena), sizeof(type)))
#define ArenaPushArray(arena, count, type) \
    (((type)*)ArenaPush((arena), (count) * sizeof(type), alignof((type)[0])))

void ArenaRelease(Arena *arena) {
    if (arena->data != nullptr) {
        free(arena->data);
        free(arena->res);
        *arena = (Arena){};
    }
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

// Super loose definition probably
bool CharIsWhitespace(char c) {
    return c == ' ' || c == '\r' || c == '\n';
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

String StrTrimUntil(String s, char c, StringSearchFlags flags) {
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
// LSS parse
//

typedef struct {
    String game_name;
    String category_name;
    String attempt_count; // TODO parse u64
} LivesplitSplits;

String XmlParseElemText(String line) {
    String start = StrTrimUntil(line, '>', SSF_None);
    return StrTrimUntil(start, '<', SSF_SearchBackwards);
}

LivesplitSplits *ParseLss(Arena *arena, String lss) {
    LivesplitSplits splits = {};

    LineIter iter = StrIterLines(lss);
    while (LineIterHasNext(&iter)) {
        String line = StrTrim(LineIterNext(&iter));
        if (StrStartsWith(line, S("<GameName>"))) {
            splits.game_name = StrClone(arena, XmlParseElemText(line));
        } else if (StrStartsWith(line, S("<CategoryName>"))) {
            splits.game_name = StrClone(arena, XmlParseElemText(line));
        } else if (StrStartsWith(line, S("<AttemptCount>"))) {
            splits.game_name = StrClone(arena, XmlParseElemText(line));
        }
    }

    return splits;
}

//
// Main
//

String CliGetArg(Arena *arena, int argc, char **argv, u64 idx) {
    if (idx + 1 >= argc) {
        return (String){};
    }
    return StrFromCStr(arena, argv[idx + 1]);
}

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

    LivesplitSplits* splits = ParseLss(&arena, f);
    printf("name: '%s', cat: '%s', attempts: %lld\n", 
            StrToCStr(&arena, splits.game_name), 
            StrToCStr(&arena, splits.category_name),
            splits.attempts);

    return EXIT_SUCCESS;
}
