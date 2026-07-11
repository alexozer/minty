//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

UI_Box *ui_box(Arena *frame_arena, UI_Style *s);
void ui_width_px(UI_Style *s, f32 px);
void ui_height_px(UI_Style *s, f32 px);
void ui_width_flex(UI_Style *s);
void ui_height_flex(UI_Style *s);
void ui_width_flex_ratio(UI_Style *s, f32 ratio);
void ui_height_flex_ratio(UI_Style *s, f32 ratio);
void ui_flags(UI_Style *style, UI_Flag flags);
void ui_parent(UI_Style *s, UI_Box *parent);
void ui_child_idx(UI_Style *s, u16 idx);
void ui_text(UI_Style *s, Str text, FontFile *font_file, u32 font_size_px);
void ui_texture(UI_Style *s, Texture *texture);
void layout_ui(UI_Box *root);
void layout_ui_main_axis(UI_Box *parent, Axis axis);
void layout_ui_cross_axis(UI_Box *parent, Axis axis);
void layout_ui_impl(UI_Box *box);
void render_ui(Arena *frame_arena,
               UI_Box *root,
               FontSystem *font_system,
               FVec_QuadRequest *requests);
void render_ui_impl(Arena *frame_arena,
                    UI_Box *box,
                    FontSystem *font_system,
                    FVec_QuadRequest *quad_reqs);
RectF scale_rect_proportionally_to_fit(RectF outer, f32 inner_aspect_ratio);
void debug_render_ui(Arena *frame_arena, UI_Box *root, FVec_QuadRequest *requests);
void debug_render_ui_impl(UI_Box *box, u64 depth, FVec_QuadRequest *reqs);
