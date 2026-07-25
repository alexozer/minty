//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn FontInst *get_or_create_font_inst(FontSystem *ctx,
                                     FontFile *font_file,
                                     u32 px_per_em,
                                     f32 outline_px);
fn void font_init(FontSystem *ctx, Arena *arena);
fn FontInst *add_font_inst(ErrorContext *err,
                           FontSystem *ctx,
                           FontFile *font_file,
                           u32 face_size_px,
                           f32 outline_px);
fn f32 compute_face_center_y(FontSystem *ctx, FontInst *inst);
fn void destroy_font_inst(FontInst *inst);
fn void rasterize_glyph(FontInst *inst, u32 glyph_id);
fn Arr_ShapedGlyphPX shape_text(Arena *arena, FontSystem *ctx, FontInst *inst, Str text);
fn Arr_ShapedGlyph shape_text_uncached(Arena *arena, FontInst *inst, Str text);
fn Arr_ShapedGlyphPX convert_shape_result_to_px(Arena *arena,
                                                FontInst *inst,
                                                Arr_ShapedGlyph shape_result);
fn void font_prepare_to_render(FontSystem *ctx, UI_Box *box, FVec_QuadRequest *quad_reqs);
fn void ensure_bitmap_set_rasterized(FontInst *inst, u32 glyph_id);
fn RectF get_shaped_text_bbox(FontInst *inst, Arr_ShapedGlyphPX shaped_glyphs);
fn RectF get_text_bbox(FontSystem *ctx, FontFile *font, u32 font_size_px, Str text);
fn Arr_ShapedGlyphPX align_text(FontSystem *ctx,
                                UI_Box *box,
                                FontInst *inst,
                                Arr_ShapedGlyphPX shaped_glyphs);
fn void emit_glyph_quads(FontSystem *ctx,
                         UI_Box *box,
                         FontInst *non_outline_inst,
                         FontInst *outline_inst,
                         Arr_ShapedGlyphPX shaped_glyphs,
                         FVec_QuadRequest *quad_reqs);
fn void emit_glyph_quad(UI_Box *box,
                        GlyphBitmap *bitmap,
                        ShapedGlyphPX shaped_glyph,
                        Color color,
                        u16 depth,
                        f32 center_y_px,
                        FVec_QuadRequest *quad_reqs);
fn GlyphBitmap *get_glyph_bitmap(FontInst *inst, ShapedGlyphPX shaped_glyph);
