#include "font.h"

#include <kb_text_shape.h>

// TODO check font for errors on load, but afterwards assume it's good
// TODO cache shaping context
// TODO arena allocate kbts stuff
// TODO handling style/direction/face runs etc.
fn Arr_ShapedGlyph shape_text_naive(Arena *arena, Arr_u8 font, Str text) {
    if (text.count == 0) return (Arr_ShapedGlyph){};

    Vec_ShapedGlyph output = {};
    kbts_shape_context *context = NULL;

    context = kbts_CreateShapeContext(0, 0);
    kbts_ShapePushFontFromMemory(context, font.ptr, (int)font.count, 0);

    kbts_ShapeBegin(context, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(context, (char *)text.ptr, (i32)text.count,
                   KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
    kbts_ShapeEnd(context);

    // Layout runs naively left to right.
    kbts_run Run = {};
    i32 cursor_x = 0, cursor_y = 0;
    // u32 run_idx = 0;
    vec_prealloc(arena, &output, text.count);
    while (kbts_ShapeRun(context, &Run)) {
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

    kbts_DestroyShapeContext(context);
    return vec_arr(&output);
}

// TODO split fallible font sanity check vs. infallible atlas creation
// GlyphAtlas make_and_upload_glyph_atlas(ErrorContext *err,
//                                        Arena *arena,
//                                        SDL_GPUDevice *device,
//                                        SDL_GPUCommandBuffer *command_buffer,
//                                        SDL_GPUGraphicsPipeline *clear_texture_pipeline,
//                                        FT_Library freetype_handle,
//                                        Arr_u8 font_file,
//                                        u16 face_size_px) {
//     if (!command_buffer) return (GlyphAtlas){};
//     if (!clear_texture_pipeline) return (GlyphAtlas){};
//     if (!freetype_handle) return (GlyphAtlas){};
//
//     log_assert(face_size_px > 0);
//     Arena *scratch = arena_acquire();
//     Scope scope = scope_open(err);
//
//     FT_Face face = {};
//     if (FT_New_Memory_Face(freetype_handle, font_file.ptr, (long)font_file.count, 0, &face) !=
//         FT_Err_Ok) {
//         // TODO
//         // break;
//     }
//
//     if (FT_Set_Pixel_Sizes(face, face_size_px, 0) != FT_Err_Ok) {
//         // TODO
//         // break;
//     }
//
//     Arr_Texture textures = arena_push_arr(scratch, Texture, (u64)face->num_glyphs);
//     Arr_GlyphMetrics metrics = arena_push_arr(arena, GlyphMetrics, (u64)face->num_glyphs);
//
//     for (u64 glyph_idx = 0; glyph_idx < face->num_glyphs; glyph_idx++) {
//         // TODO re-enable hinting once we can account for spacing discrepancies
//         // Also maybe disable on macos for more native look?
//         FT_Load_Glyph(face, (u32)glyph_idx, FT_LOAD_NO_HINTING);
//         // if (face->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
//         //     bail(err, "TODO: handle bitmap glyph");
//         // }
//         FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
//
//         FT_Bitmap bitmap = face->glyph->bitmap;
//         Texture *texture = &A(textures, glyph_idx);
//         Arr_u8 tmp_buffer = {.ptr = bitmap.buffer, .count = bitmap.width * bitmap.rows};
//         texture->format = GLYPH_TEXTURE_FORMAT;
//         texture->buffer = arr_clone(scratch, tmp_buffer);
//         texture->dims = (SizePX){(u16)bitmap.width, (u16)bitmap.rows};
//
//         // Convert from 26.6 fixed point pixels to f32 pixels
//         A(metrics, glyph_idx).bearing_px_x = (f32)face->glyph->bitmap_left;
//         A(metrics, glyph_idx).bearing_px_y = (f32)face->glyph->bitmap_top;
//     }
//
//     SizePX font_atlas_size = {.w = 2048, .h = 2048};
//     Str texture_name = str_format(scratch, "Glyph atlas: family = '%s', style = '%s', size =
//     %dpx",
//                                   face->family_name, face->style_name, face_size_px);
//     Atlas *atlas =
//         make_and_upload_atlas(err, arena, device, command_buffer, clear_texture_pipeline,
//                               texture_name, textures, font_atlas_size, FilterType_Nearest);
//     GlyphAtlas ret = {
//         .atlas = atlas,
//         .px_per_em = face_size_px,
//         .units_per_em = face->units_per_EM,
//         .metrics = metrics,
//     };
//
//     FT_Done_Face(face);
//     scope_close(scope, "Create and upload glyph atlas");
//     arena_release(scratch);
//     return ret;
// }

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
