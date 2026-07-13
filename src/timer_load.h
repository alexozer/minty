//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn Session *make_session(ErrorContext *err, Arena *arena, App *app, Str path);
fn void load_livesplit_lss(ErrorContext *err, Arena *arena, Str lss_path, FileDef *file);
fn void parse_livesplit_lss(ErrorContext *err, Arena *arena, FileDef *file, Arr_u8 xml);
fn Arr_SegmentDef parse_livesplit_segments(ErrorContext *err,
                                           Arena *arena,
                                           xao_Reader *r,
                                           xao_Value segments_tag);
fn bool eq(xao_Value v, const char *s);
fn Str xml_str(xao_Value v);
fn Str xml_inner(xao_Reader *r, xao_Value outer);
fn void parse_segment_icon(ErrorContext *err, Arena *arena, SegmentDef *segment, Str base64);
fn Texture convert_srgb_surface_to_rgba(Arena *arena, SDL_Surface *surface);
fn SDL_Surface *sdl_load_png_io(ErrorContext *err, SDL_IOStream *stream);
fn void sdl_destroy_surface(SDL_Surface *surface);
fn Texture decode_png_to_texture(ErrorContext *err, Arena *arena, Arr_u8 png);
