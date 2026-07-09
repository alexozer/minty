//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

UI_Box *ui_box(Arena *arena, Str id);
void ui_flex_x(UI_Box *box, f32 ratio);
void ui_flex_y(UI_Box *box, f32 ratio);
void ui_fixed_x(UI_Box *box, f32 size_px);
void ui_fixed_y(UI_Box *box, f32 size_px);
void ui_parent(Arena *arena, UI_Box *child, UI_Box *parent);
UI_Box *ui_template(Arena *arena, UI_Box *template, Str id);
void layout_ui(UI_Box *root);
void layout_ui_main_axis(UI_Box *parent, Axis axis);
void layout_ui_cross_axis(UI_Box *parent, Axis axis);
void layout_ui_impl(UI_Box *box);
Arr_TextureRequest render_ui(Arena *frame_arena, UI_Box *root);
void render_ui_impl(Arena *frame_arena, UI_Box *box, Vec_TextureRequest *reqs);
RectF scale_rect_proportionally_to_fit(RectF outer, f32 inner_aspect_ratio);
Arr_TextureRequest debug_render_ui(Arena *frame_arena, UI_Box *root);
void debug_render_ui_impl(Arena *frame_arena, UI_Box *box, u64 depth, Vec_TextureRequest *reqs);
