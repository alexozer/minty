//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn void build_timer_ui(UI_View *view, Session *session);
fn void build_timer_ui_impl(UI_View *view, UI_Box *base, Session *session);
fn void build_game_info_ui(UI_View *view, UI_Box *base, Session *session);
fn void build_segment_ui(UI_View *view,
                         UI_Box *parent,
                         Session *session,
                         Arr_SegSummary summaries,
                         u64 segment_idx);
fn void build_segment_times(UI_View *view,
                            Session *session,
                            Arr_SegSummary summaries,
                            u64 segment_idx,
                            UI_Box *row);
fn void build_segment_seg_time(UI_View *view,
                               Session *session,
                               Arr_SegSummary summaries,
                               u64 segment_idx,
                               UI_Box *row,
                               UI_Style *style_template);
fn void build_segment_split_time(UI_View *view,
                                 Session *session,
                                 Arr_SegSummary summaries,
                                 u64 segment_idx,
                                 UI_Box *row,
                                 UI_Style *style_template);
fn void build_segment_delta_time(UI_View *view,
                                 Session *session,
                                 Arr_SegSummary summaries,
                                 u64 segment_idx,
                                 UI_Box *row,
                                 UI_Style *style_template);
fn void pad_box(UI_View *view, UI_Box *parent, f32 pad_px);
fn void build_big_timer(UI_View *view, Session *session, Arr_SegSummary summaries, UI_Box *parent);
fn void build_bottom_stats(UI_View *view,
                           Session *session,
                           Arr_SegSummary summaries,
                           UI_Box *parent);
fn Color get_delta_color(Session *session, Arr_SegSummary summaries, u64 idx);
fn Color get_gained_color(Session *session, bool ahead, bool gained);
fn void build_bottom_stat(UI_View *view,
                          UI_Box *parent,
                          Session *session,
                          Str label,
                          Str value,
                          Color value_color);
fn void build_padding(UI_View *view, UI_Style *s, UI_Box *parent, f32 pad_px, UI_Flag flags);
fn void build_text_test_ui(UI_View *view, UI_Box *base, Session *session);
