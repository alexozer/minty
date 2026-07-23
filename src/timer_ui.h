//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn UI_Box *build_timer_ui(Arena *arena,
                          Session *session,
                          SizePX device_size,
                          f32 os_scale,
                          f32 user_scale);
fn void build_timer_ui_impl(Arena *arena, UI_Box *base, Session *session);
fn void build_game_info_ui(Arena *arena, UI_Box *base, Session *session);
fn void build_segment_ui(Arena *arena,
                         UI_Box *parent,
                         Session *session,
                         Arr_SegSummary summaries,
                         u64 segment_idx);
fn void build_segment_times(Arena *arena,
                            Session *session,
                            Arr_SegSummary summaries,
                            u64 segment_idx,
                            UI_Box *row);
fn void build_bottom_timer_ui(Arena *arena, Session *session, UI_Box *parent);
fn void build_bottom_stat(Arena *arena, UI_Box *parent, Session *session, Str label, Str value);
fn void build_padding(Arena *arena, UI_Style *s, UI_Box *parent, f32 pad_px, UI_Flag flags);
fn void build_text_test_ui(Arena *arena, UI_Box *base, Session *session);
