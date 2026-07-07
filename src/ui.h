//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

void ui_flex_x(UI_Box *box, f32 ratio);
void ui_flex_y(UI_Box *box, f32 ratio);
void ui_fixed_x(UI_Box *box, f32 size_px);
void ui_fixed_y(UI_Box *box, f32 size_px);
void ui_parent(Arena *arena, UI_Box *child, UI_Box *parent);
UI_Box *ui_box(Arena *arena, Str id);
UI_Box *ui_template(Arena *arena, UI_Box *template, Str id);
UI_Box *build_ui_segment(Arena *arena, Session *session, u64 idx);
UI_Box *build_ui_segments(Arena *arena, Session *session);
UI_Box *build_ui(Arena *arena, Session *session, SizePX size);
void layout_ui_main_axis(UI_Box *parent, Axis axis);
void layout_ui_cross_axis(UI_Box *parent, Axis axis);
void layout_ui_impl(UI_Box *box);
void layout_ui(UI_Box *root);
