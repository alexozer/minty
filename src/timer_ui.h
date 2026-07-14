//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn UI_Box *build_timer_ui(Arena *arena, Session *session, SizePX size);
fn void build_timer_ui_impl(Arena *arena, UI_Box *base, Session *session, SizePX size);
fn void build_game_info_ui(Arena *arena, UI_Box *base, Session *session, SizePX size);
fn void build_segment_ui(Arena *arena, UI_Box *parent, Session *session, u64 segment_idx);
fn void build_padding(Arena *arena, UI_Style *s, UI_Box *parent, f32 pad_px);
fn void build_text_test_ui(Arena *arena, UI_Box *base, Session *session, SizePX size);
