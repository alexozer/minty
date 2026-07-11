#include "font.h"
#include "platform.h"

// TODO thread through program properly
Str FONT_PATH = S("data/Roboto-Medium.ttf");
// Str FONT_PATH = S("data/NotoSans-Regular.ttf");
// Str FONT_PATH = S("data/NotoSans-Bold.ttf");
// Str FONT_PATH = S("data/KosugiMaru-Regular.otf");
// constexpr u32 FONT_SIZE_PX = 40;

fn void font_init(ErrorContext *err, FontSystem *ctx) {
    Scope scope = scope_open(err);
    ctx->inst = create_font_inst(err, FONT_PATH, 12);
    scope_close(scope, "Initialize font rendering system");
}

fn FontInst *create_font_inst(ErrorContext *err, Str font_path, u32 face_size_px) {
    Scope scope = scope_open(err);

    Arena *inst_arena = arena_acquire();
    FontInst *inst = arena_push(inst_arena, FontInst);
    inst->arena = inst_arena;

    inst->font_file = os_read_file(err, inst->arena, font_path);
    inst->face_size_px = face_size_px;

    if (FT_Init_FreeType(&inst->ft_ctx) != FT_Err_Ok) {
        err_report(err, "Failed to initialize freetype");
    }
    if (FT_New_Memory_Face(inst->ft_ctx, inst->font_file.ptr, (long)inst->font_file.count, 0,
                           &inst->ft_face) != FT_Err_Ok) {
        err_report(err, "Failed to load font face for glyph rendering");
    }
    if (FT_Set_Pixel_Sizes(inst->ft_face, face_size_px, 0) != FT_Err_Ok) {
        err_report(err, "Failed to set font face size");
    }

    inst->bitmaps = arena_push_arr(inst->arena, GlyphBitmap, (u64)inst->ft_face->num_glyphs);

    inst->kbts_ctx = kbts_CreateShapeContext(0, 0);
    kbts_ShapePushFontFromMemory(inst->kbts_ctx, inst->font_file.ptr, (int)inst->font_file.count,
                                 0);

    scope_close(scope, "Load font: %.*s", SF(font_path));
    return inst;
}

fn void destroy_font_inst(FontInst *inst) {
    // TODO arena allocate kbts to avoid needing to destroy it
    // (but I don't think we can do the same for freetype)
    kbts_DestroyShapeContext(inst->kbts_ctx);
    FT_Done_FreeType(inst->ft_ctx);
}

fn void rasterize_glyph(FontInst *inst, u32 glyph_id) {
    GlyphBitmap *bitmap = &A(inst->bitmaps, glyph_id);
    log_assert(!bitmap->rendered);  // Callers should check before calling

    // TODO re-enable hinting once we can account for spacing discrepancies
    // Also maybe disable on macos for more native look?
    FT_Load_Glyph(inst->ft_face, glyph_id, FT_LOAD_NO_HINTING);
    // if (face->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
    //     bail(err, "TODO: handle bitmap glyph");
    // }
    FT_Render_Glyph(inst->ft_face->glyph, FT_RENDER_MODE_NORMAL);

    FT_Bitmap ft_bitmap = inst->ft_face->glyph->bitmap;
    Arr_u8 tmp_buffer = {.ptr = ft_bitmap.buffer, .count = ft_bitmap.width * ft_bitmap.rows};

    bitmap->rendered = true;

    bitmap->texture.format = GLYPH_TEXTURE_FORMAT;
    bitmap->texture.buffer = arr_clone(inst->arena, tmp_buffer);
    bitmap->texture.dims = (SizePX){(u16)ft_bitmap.width, (u16)ft_bitmap.rows};

    // Convert from 26.6 fixed point pixels to f32 pixels
    bitmap->bearing_px_x = (f32)inst->ft_face->glyph->bitmap_left;
    bitmap->bearing_px_y = (f32)inst->ft_face->glyph->bitmap_top;
}

// TODO check font for errors on load, but afterwards assume it's good
// TODO cache shaping context
// TODO arena allocate kbts stuff
// TODO handling style/direction/face runs etc.
fn Arr_ShapedGlyph shape_text(Arena *arena, FontInst *inst, Str text) {
    if (text.count == 0) return (Arr_ShapedGlyph){};

    Vec_ShapedGlyph output = {};

    kbts_ShapeBegin(inst->kbts_ctx, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(inst->kbts_ctx, (char *)text.ptr, (i32)text.count,
                   KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
    kbts_ShapeEnd(inst->kbts_ctx);

    // Layout runs naively left to right.
    kbts_run Run = {};
    i32 cursor_x = 0, cursor_y = 0;
    // u32 run_idx = 0;
    vec_prealloc(arena, &output, text.count);
    while (kbts_ShapeRun(inst->kbts_ctx, &Run)) {
        kbts_glyph *glyph = nullptr;
        while (kbts_GlyphIteratorNext(&Run.Glyphs, &glyph)) {
            i32 glyph_x = cursor_x + glyph->OffsetX;
            i32 glyph_y = cursor_y + glyph->OffsetY;

            cursor_x += glyph->AdvanceX;
            cursor_y += glyph->AdvanceY;

            ShapedGlyph *g = vec_push_zero(arena, &output);
            g->glyph_id = glyph->Id;
            g->glyph_x_fu = glyph_x;
            g->glyph_y_fu = glyph_y;
        }
        // run_idx++;
    }

    return vec_arr(&output);
}

fn void font_test(FontInst *inst) {
    Arena *scratch = arena_acquire();

    Arr_ShapedGlyph shape_result = shape_text(scratch, inst, S("Hello, world!"));
    for (u64 i = 0; i < shape_result.count; i++) {
        if (!A(inst->bitmaps, i).rendered) {
            rasterize_glyph(inst, A(shape_result, i).glyph_id);
        }
    }

    arena_release(scratch);
}

// u64 make_glyph_mesh(SizePX window_size,
//                     Arr_u8 font_file,
//                     GlyphAtlas *atlas,
//                     Str text,
//                     Mesh *mesh) {
//     Arena *scratch = arena_acquire();
//
//     u64 start_vertex_count = mesh->vertices.count;
//
//     Arr_ShapedGlyph shaped_glyphs = shape_text_naive(scratch, font_file, text);
//     for (u64 i = 0; i < shaped_glyphs.count; i++) {
//         u32 glyph_id = A(shaped_glyphs, i).glyph_id;
//
//         // Shaping position of glyph
//         f32 glyph_px_x =
//             (f32)A(shaped_glyphs, i).glyph_x_fu * atlas->px_per_em / atlas->units_per_em;
//         f32 glyph_px_y =
//             (f32)A(shaped_glyphs, i).glyph_y_fu * atlas->px_per_em / atlas->units_per_em;
//
//         // Position of glyph bitmap
//         f32 bitmap_px_x = glyph_px_x + A(atlas->metrics, glyph_id).bearing_px_x;
//         f32 bitmap_px_y = glyph_px_y + A(atlas->metrics, glyph_id).bearing_px_y;
//
//         // Convert/round to window space pixel coord
//         u16 dest_px_x = (u16)SDL_lroundf(200.f + bitmap_px_x);
//         u16 dest_px_y = (u16)SDL_lroundf(200.f - bitmap_px_y);
//
//         u32 shaped_id = A(shaped_glyphs, i).glyph_id;
//         RectPX src = A(atlas->atlas->placements, shaped_id);
//         RectPX dst = {.x = dest_px_x, .y = dest_px_y, .w = src.w, .h = src.h};
//         Color color = {.r = 255, .g = 255, .b = 255, .a = 255};
//         push_atlas_quad(window_size, atlas->atlas, mesh, src, dst, color);
//     }
//
//     u64 end_vertex_count = mesh->vertices.count;
//     u64 quad_count = (end_vertex_count - start_vertex_count) / 4;
//
//     arena_release(scratch);
//     return quad_count;
// }
