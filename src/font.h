//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

void font_init(ErrorContext *err, FontSystem *ctx);
FontInst *create_font_inst(ErrorContext *err, Str font_path, u32 face_size_px);
void destroy_font_inst(FontInst *inst);
void rasterize_glyph(FontInst *inst, u32 glyph_id);
Arr_ShapedGlyph shape_text(Arena *arena, FontInst *inst, Str text);
void font_test(FontInst *inst);
