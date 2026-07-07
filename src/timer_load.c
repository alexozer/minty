#include "timer_load.h"
#include "platform.h"

fn CPUTexture convert_srgb_surface_to_rgba(Arena *arena, SDL_Surface *surface) {
    if (!surface) return (CPUTexture){};

    u64 dest_size = (u64)(surface->w * surface->h * 4);
    Arr_u8 buffer = {
        .ptr = (u8 *)arena_push_bytes(arena, dest_size, 8),
        .count = dest_size,
    };
    CPUTexture texture = {
        .format = ICON_TEXTURE_FORMAT,
        .buffer = buffer,
        .dims = {.w = (u16)surface->w, .h = (u16)surface->h},
    };
    log_assert(SDL_ConvertPixels(surface->w, surface->h, surface->format, surface->pixels,
                                 surface->pitch, SDL_PIXELFORMAT_RGBA32, texture.buffer.ptr,
                                 surface->w * 4));
    return texture;
}

fn bool eq(xao_Value v, const char *s) {
    u64 size = (u64)v.end - (u64)v.start;
    return size == SDL_strlen(s) && SDL_memcmp(v.start, s, size) == 0;
}

fn Str xml_str(xao_Value v) {
    return (Str){.ptr = (u8 *)v.start, .count = (u64)v.end - (u64)v.start};
}

fn Str xml_inner(xao_Reader *r, xao_Value outer) {
    xao_Value inner = {};
    xao_iter_content(r, outer, &inner);
    return xml_str(inner);
}

// TODO I've been neglectful of `const`ness in my APIs, but obviously this
// should be const
u8 PNG_HEADER[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

fn SDL_Surface *sdl_load_png_io(ErrorContext *err, SDL_IOStream *stream) {
    if (!stream) return nullptr;
    Scope scope = scope_open(err);

    SDL_Surface *surface = SDL_LoadPNG_IO(stream, false);
    if (!surface) {
        err_report(err, "%s", SDL_GetError());
    }

    scope_close(scope, "Load PNG from stream");
    return surface;
}

fn void sdl_destroy_surface(SDL_Surface *surface) {
    if (surface) SDL_DestroySurface(surface);
}

fn CPUTexture decode_png_to_texture(ErrorContext *err, Arena *arena, Arr_u8 png) {
    Scope scope = scope_open(err);

    SDL_IOStream *stream = sdl_io_from_mem(err, png);
    SDL_Surface *surface = sdl_load_png_io(err, stream);
    CPUTexture texture = convert_srgb_surface_to_rgba(arena, surface);
    sdl_destroy_surface(surface);
    sdl_close_io(stream);

    scope_close(scope, "Decode png to texture");
    return texture;
}

fn void parse_segment_icon(ErrorContext *err, Arena *arena, SegmentDef *segment, Str base64) {
    Scope scope = scope_open(err);

    segment->icon_png = decode_base64(err, arena, base64);
    Opt_u64 png_header_offset = str_find(segment->icon_png, ARR(PNG_HEADER));
    if (png_header_offset.present) {
        Arr_u8 png_buf =
            arr_slice(segment->icon_png, png_header_offset.opt, segment->icon_png.count);
        segment->icon_texture = decode_png_to_texture(err, arena, png_buf);
    } else {
        err_report(err, "PNG header not found");
    }

    scope_close(scope, "Decode icon for segment '%.*s'", SF(segment->name));
}

fn Arr_SegmentDef parse_livesplit_segments(ErrorContext *err,
                                           Arena *arena,
                                           xao_Reader *r,
                                           xao_Value segments_tag) {
    Scope scope = scope_open(err);

    Vec_SegmentDef segments = {};
    xao_Value seg_tag = {};
    while (xao_iter_tags(r, segments_tag, &seg_tag)) {
        SegmentDef *seg = vec_push_zero(arena, &segments);
        xao_Value attr_tag = {};
        while (xao_iter_tags(r, seg_tag, &attr_tag)) {
            if (eq(attr_tag, "Name")) {
                seg->name = str_clone(arena, xml_inner(r, attr_tag));

            } else if (eq(attr_tag, "Icon")) {
                Str base64 = xml_inner(r, attr_tag);
                if (!is_empty(base64)) {
                    parse_segment_icon(err, arena, seg, base64);
                }
            }
        }
    }

    scope_close(scope, "Parse LiveSplit LSS segments");
    return vec_arr(&segments);
}

fn void parse_livesplit_lss(ErrorContext *err, Arena *arena, FileDef *file, Arr_u8 xml) {
    Scope scope = scope_open(err);

    if (!str_is_valid_utf8(xml)) {
        err_report(err, "Invalid UTF-8");
    }

    xao_Reader r = xao_reader((char *)xml.ptr, xml.count);
    xao_Value root = {};
    xao_Value root_tag = {};
    while (xao_iter_tags(&r, root, &root_tag)) {
        if (eq(root_tag, "Run")) {
            xao_Value run_tag = {};
            while (xao_iter_tags(&r, root_tag, &run_tag)) {
                if (eq(run_tag, "GameName")) {
                    file->game_name = str_clone(arena, xml_inner(&r, run_tag));

                } else if (eq(run_tag, "CategoryName")) {
                    file->category_name = str_clone(arena, xml_inner(&r, run_tag));

                } else if (eq(run_tag, "AttemptCount")) {
                    Scope attempt_count_scope = scope_open(err);
                    Str attempts_str = str_clone(arena, xml_inner(&r, run_tag));
                    file->total_attempts = parse_u64(err, attempts_str);
                    scope_close(attempt_count_scope, "Parse AttemptCount");

                } else if (eq(run_tag, "Segments")) {
                    file->segments = parse_livesplit_segments(err, arena, &r, run_tag);
                }
            }
        }
    }

    if (r.error != nullptr) {
        err_report(err, "Failed to parse LSS XML: %s", r.error);
    }

    // Basic validation
    if (is_empty(file->segments)) {
        err_report(err, "No segments found");
    }
    for (u64 i = 0; i < file->segments.count; i++) {
        if (is_empty(A(file->segments, i).name)) {
            err_report(err, "Segment %" PRIu64 " has no name", i + 1);
        }
    }
    if (is_empty(file->game_name)) {
        err_report(err, "Empty game name");
    }

    scope_close(scope, "Parse LiveSplit LSS");
}

fn void load_livesplit_lss(ErrorContext *err, Arena *arena, Str lss_path, FileDef *file) {
    Arena *scratch = arena_acquire();
    Scope scope = scope_open(err);

    Arr_u8 xml = os_read_file(err, scratch, lss_path);
    parse_livesplit_lss(err, arena, file, xml);

    scope_close(scope, "Load LiveSplit LSS file '%.*s'", SF(lss_path));
    arena_release(scratch);
}
