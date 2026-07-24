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
fn Arr_Duration parse_segment_history(ErrorContext *err,
                                      Arena *arena,
                                      xao_Reader *r,
                                      xao_Value history_tag);
fn Opt_Duration parse_pb(ErrorContext *err, xao_Reader *r, xao_Value split_times_tag);
fn Opt_Duration parse_realtime(ErrorContext *err, xao_Reader *r, xao_Value outer_tag);
fn bool has_attr(xao_Reader *r, xao_Value tag, Str key, Str value);
fn Opt_Duration parse_opt_duration(ErrorContext *err, Str s);
fn bool eq(xao_Value v, Str s);
fn Str xml_str(xao_Value v);
fn Str xml_inner(xao_Reader *r, xao_Value outer);
fn Opt_Texture parse_image(ErrorContext *err, Arena *arena, xao_Reader *r, xao_Value elem);
fn void load_livesplit_layout(ErrorContext *err, Arena *arena, Str lsl_path, Layout *layout);
fn void parse_livesplit_lsl(ErrorContext *err, Arena *arena, Arr_u8 xml, Layout *layout);
fn void parse_lsl_settings(ErrorContext *err,
                           Arena *arena,
                           xao_Reader *r,
                           xao_Value settings_tag,
                           Layout *layout);
fn Color parse_livesplit_color(ErrorContext *err, xao_Reader *r, xao_Value elem);
fn FontFile parse_livesplit_font(ErrorContext *err, Arena *arena, xao_Reader *r, xao_Value elem);
