#include "base.h"
#include "bootstrap.h"
#include "platform.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <simdutf_c.h>

// char *str_to_c(Arena *arena, Str s) {
//     Arr<char> cstr = arena_push_arr<char>(arena, s.count + 1);
//     SDL_memcpy(cstr.ptr, s.ptr, s.count);
//     return cstr.ptr;
// }
//
// Str str_from_c(const char *cstr) {
//     u64 count = 0;
//     while (cstr[count] != '\0')
//         count++;
//     return (Str){.ptr = (u8 *)cstr, .count = count};
// }
//
// Str str_from_c_len(const char *cstr, u64 len) {
//     return {.ptr = (u8 *)cstr, .count = len};
// }

//
// Encoding/Decoding
//

u64 parse_u64(ErrorContext *err, Str s) {
    Scope scope = scope_open(err);

    if (str_is_empty(s)) {
        err_report(err, "Empty string");
    }

    u64 result = 0;
    for (u64 i = 0; i < s.count; i++) {
        if (A(s, i) < C('0') || A(s, i) > C('9')) {
            err_report(err, "Non-numeric character");
        }
        u64 new_result = result * 10 + (s.ptr[i] - C('0'));
        if (new_result < result) {
            err_report(err, "Overflow");
        }
        result = new_result;
    }

    // In general, should fallible functions try to return "reasonable" values on failure?
    if (err_occurred(err)) result = 0;
    scope_close(scope, "Parse '%.*s' as u64", SF(s));
    return result;
}

//
// Idk
//

void thread_init() {
    arena_pool_init();
}

SDL_IOStream *sdl_io_from_mem(ErrorContext *err, Arr_u8 buf) {
    SDL_IOStream *stream = SDL_IOFromMem(buf.ptr, buf.count);
    if (!stream) {
        err_report(err, "%s", SDL_GetError());
    }
    return stream;
}

SDL_IOStream *sdl_io_from_file(ErrorContext *err, Str path, Str mode) {
    Arena *scratch = arena_acquire();

    char *path_c = str_to_c(scratch, path);
    char *mode_c = str_to_c(scratch, mode);
    SDL_IOStream *stream = SDL_IOFromFile(path_c, mode_c);
    if (!stream) {
        err_report(err, "%s", SDL_GetError());
    }

    arena_release(scratch);
    return stream;
}

Arr_u8 sdl_read_entire_stream(ErrorContext *err, Arena *arena, SDL_IOStream *stream) {
    if (!stream) return (Arr_u8){};
    Scope scope = scope_open(err);

    Arr_u8 buffer = {};
    i64 size = SDL_GetIOSize(stream);
    if (size < 0) {
        err_report(err, "%s", SDL_GetError());
    } else {
        buffer = arena_push_arr(arena, u8, (u64)size);
        u64 offset = 0;
        while (offset < size && SDL_GetIOStatus(stream) == SDL_IO_STATUS_READY) {
            offset += SDL_ReadIO(stream, buffer.ptr + offset, (u64)size - offset);
        }
        if (offset != size) {
            err_report(err, "%s", SDL_GetError());
        }
    }

    scope_close(scope, "Read stream to buffer");
    return buffer;
}

void sdl_close_io(SDL_IOStream *stream) {
    if (stream) SDL_CloseIO(stream);
}

Arr_u8 fs_load_file(ErrorContext *err, Arena *arena, Str path) {
    Scope scope = scope_open(err);

    SDL_IOStream *stream = sdl_io_from_file(err, path, S("rb"));
    Arr_u8 buffer = sdl_read_entire_stream(err, arena, stream);
    sdl_close_io(stream);

    scope_close(scope, "Load file '%.*s'", SF(path));
    return buffer;
}

//
// Time
//

Instant get_current_monotonic_time() {
    return (Instant){.time_nanoseconds = (i64)SDL_GetTicksNS()};
}

inline static Instant instant_from_sdl_nanos(u64 nanos) {
    return (Instant){.time_nanoseconds = (i64)nanos};
}

//
// Errors
//

Scope scope_open(ErrorContext *err) {
    return (Scope){
        .err = err,
        .last_err_stack_pos = err->ctx_stack.count,
    };
}

__attribute__((format(printf, 2, 3))) void err_report(ErrorContext *err, const char *format, ...) {
    log_assert(err != nullptr);
    if (err->ctx_stack.count == 0) {
        va_list args;
        va_start(args, format);
        Str msg = str_format_v(err->arena, format, args);
        vec_push(err->arena, &err->ctx_stack, msg);
        va_end(args);
    }
}

__attribute__((format(printf, 2, 3))) void err_ctx(Scope scope, const char *format, ...) {
    log_assert(scope.err != nullptr);
    if (scope.err->ctx_stack.count > scope.last_err_stack_pos) {
        va_list args;
        va_start(args, format);
        Str msg = str_format_v(scope.err->arena, format, args);
        vec_push(scope.err->arena, &scope.err->ctx_stack, msg);
        va_end(args);
    }
}

bool err_occurred(ErrorContext *err) {
    return err->ctx_stack.count > 0;
}

//
// Encoding/decoding
//

Arr_u8 decode_base64(ErrorContext *err, Arena *arena, Str s) {
    Scope scope = scope_open(err);

    u64 max_out_size = simdutf_maximal_binary_length_from_base64((const char *)s.ptr, s.count);
    Arr_u8 out = arena_push_arr(arena, u8, max_out_size);
    simdutf_result result =
        simdutf_base64_to_binary((const char *)s.ptr, s.count, (char *)out.ptr,
                                 SIMDUTF_BASE64_DEFAULT, SIMDUTF_LAST_CHUNK_STRICT);
    if (result.error != SIMDUTF_ERROR_SUCCESS) {
        err_report(err, "Invalid base64. Error Code = %d", result.error);
    } else {
        out = arr_slice(out, 0, result.count);
    }

    scope_close(scope, "Decode base64");
    return out;
}
