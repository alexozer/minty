//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn FontInst *get_or_create_font_inst(FontSystem *ctx, UI_Box *box);
fn void font_init(FontSystem *ctx, Arena *arena);
fn FontInst *add_font_inst(ErrorContext *err,
                           FontSystem *ctx,
                           FontFile *font_file,
                           u32 face_size_px);
fn f32 compute_face_center_y(FontInst *inst);
fn void destroy_font_inst(FontInst *inst);
fn void rasterize_glyph(FontInst *inst, u32 glyph_id);
fn Arr_ShapedGlyph shape_text(Arena *arena, FontInst *inst, Str text);
fn void font_prepare_to_render(FontSystem *ctx,
                               UI_Box *box,
                               u16 depth,
                               FVec_QuadRequest *quad_reqs);
fn Arr_ShapedGlyph shape_and_align_text(Arena *arena, FontInst *inst, UI_Box *box);
fn void emit_glyph_quads(FontInst *inst,
                         UI_Box *box,
                         Arr_ShapedGlyph shaped_glyphs,
                         u16 depth,
                         FVec_QuadRequest *quad_reqs);
fn GlyphBitmap *get_glyph_bitmap(FontInst *inst, ShapedGlyph shaped_glyph);
