//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

FontInst *get_or_create_font_inst(FontSystem *ctx, FontRequest *req);
void font_init(FontSystem *ctx, Arena *arena);
FontInst *add_font_inst(ErrorContext *err,
                        FontSystem *ctx,
                        FontFile *font_file,
                        u32 face_size_px);
void destroy_font_inst(FontInst *inst);
void rasterize_glyph(FontInst *inst, u32 glyph_id);
Arr_ShapedGlyph
pe_text(Arena *arena, FontInst *inst, Str text, f32 px_per_em, f32 font_units_per_em);
GlyphBitmap *get_glyph_bitmap(FontInst *inst, ShapedGlyph shaped_glyph);
void font_prepare_to_render(FontSystem *ctx,
                            FontRequest *font_req,
                            FVec_QuadRequest *quad_reqs);
