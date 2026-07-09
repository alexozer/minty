//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

Texture convert_srgb_surface_to_rgba(Arena *arena, SDL_Surface *surface);
bool eq(xao_Value v, const char *s);
Str xml_str(xao_Value v);
Str xml_inner(xao_Reader *r, xao_Value outer);
SDL_Surface *sdl_load_png_io(ErrorContext *err, SDL_IOStream *stream);
void sdl_destroy_surface(SDL_Surface *surface);
Texture decode_png_to_texture(ErrorContext *err, Arena *arena, Arr_u8 png);
void parse_segment_icon(ErrorContext *err, Arena *arena, SegmentDef *segment, Str base64);
Arr_SegmentDef parse_livesplit_segments(ErrorContext *err,
                                        Arena *arena,
                                        xao_Reader *r,
                                        xao_Value segments_tag);
void parse_livesplit_lss(ErrorContext *err, Arena *arena, FileDef *file, Arr_u8 xml);
void load_livesplit_lss(ErrorContext *err, Arena *arena, Str lss_path, FileDef *file);
