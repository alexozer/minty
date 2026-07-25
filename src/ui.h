//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn void ui_view_begin_frame(UI_View *view);
fn void reset_ui_frame(UI_ViewFrame *frame);
fn UI_Box *ui_box(Arena *frame_arena, UI_Style *s);
fn void ui_width_px(UI_Style *s, f32 px);
fn void ui_height_px(UI_Style *s, f32 px);
fn void ui_width_flex(UI_Style *s);
fn void ui_height_flex(UI_Style *s);
fn void ui_width_flex_ratio(UI_Style *s, f32 ratio);
fn void ui_height_flex_ratio(UI_Style *s, f32 ratio);
fn void ui_width_text_content(UI_Style *s);
fn void ui_flags(UI_Style *style, UI_Flag flags);
fn void ui_parent(UI_Style *s, UI_Box *parent);
fn void ui_child_idx(UI_Style *s, u16 idx);
fn void ui_text(UI_Style *s, Str text);
fn void ui_text_outline(UI_Style *s, f32 outline_px);
fn void ui_font(UI_Style *s, FontFile *font_file, u16 font_size_px);
fn void ui_texture(UI_Style *s, Texture *texture);
fn void ui_style(UI_Style *s, UI_Style *ref);
fn void ui_depth(UI_Style *s, i16 depth);
fn void ui_color_bg(UI_Style *s, Color color);
fn void ui_fg_color(UI_Style *s, Color color);
fn void ui_bg_color(UI_Style *s, Color color);
fn void layout_ui(FontSystem *font_system,
                  UI_Box *root,
                  SizePX device_size,
                  f32 os_scale,
                  f32 user_scale);
fn void scale_dim(UI_Dim *dim, f32 scale);
fn void scale_ui(UI_Box *box, f32 os_scale, f32 user_scale);
fn void convert_text_content_dims_to_fixed_px(FontSystem *font_system, UI_Box *parent, Axis axis);
fn void layout_ui_main_axis(FontSystem *font_system, UI_Box *parent, Axis axis);
fn void layout_ui_cross_axis(FontSystem *font_system, UI_Box *parent, Axis axis);
fn void layout_ui_impl(FontSystem *font_system, UI_Box *box);
fn void render_ui(Arena *frame_arena,
                  UI_Box *root,
                  FontSystem *font_system,
                  FVec_QuadRequest *requests);
fn void render_ui_impl(Arena *frame_arena,
                       UI_Box *box,
                       FontSystem *font_system,
                       FVec_QuadRequest *quad_reqs);
fn bool rectf_contains(RectF *outer, RectF *inner);
fn RectF scale_rect_proportionally(RectF outer, f32 inner_aspect_ratio, bool zoom);
fn void debug_render_ui(Arena *frame_arena, UI_Box *root, FVec_QuadRequest *requests);
fn void debug_render_ui_impl(UI_Box *box, u64 depth, FVec_QuadRequest *reqs);
