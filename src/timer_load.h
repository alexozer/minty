//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn Session *make_session(ErrorContext *err, Arena *arena, App *app, Str lss_path);
fn void load_livesplit_lss(ErrorContext *err, Arena *arena, Str lss_path, FileDef *file);
fn void parse_livesplit_lss(ErrorContext *err, Arena *arena, Arr_u8 xml, FileDef *file);
fn Arr_SegmentDef parse_livesplit_segments(ErrorContext *err,
                                           Arena *arena,
                                           xao_Reader *r,
                                           xao_Value segments_tag);
fn bool eq(xao_Value v, const char *s);
fn Str xml_str(xao_Value v);
fn Str xml_inner(xao_Reader *r, xao_Value outer);
fn Opt_Texture parse_texture(ErrorContext *err, Arena *arena, xao_Reader *r, xao_Value elem);
fn void load_livesplit_layout(ErrorContext *err, Arena *arena, Str lsl_path, Layout *layout);
fn void parse_livesplit_lsl(ErrorContext *err, Arena *arena, Arr_u8 xml, Layout *layout);
fn void parse_lsl_settings(ErrorContext *err,
                           Arena *arena,
                           xao_Reader *r,
                           xao_Value settings_tag,
                           Layout *layout);
fn Color parse_livesplit_color(ErrorContext *err, xao_Reader *r, xao_Value elem);
fn FontFile parse_livesplit_font(ErrorContext *err, Arena *arena, xao_Reader *r, xao_Value elem);
