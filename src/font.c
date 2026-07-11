#include "font.h"
#include "base.h"
#include "platform.h"

// TODO thread through program properly

// TODO thread through program properly
// Str FONT_PATH = S("data/Roboto-Medium.ttf");
// Str FONT_PATH = S("data/NotoSans-Regular.ttf");
// Str FONT_PATH = S("data/NotoSans-Bold.ttf");
Str FONT_PATH = S("data/NunitoSans-Bold.ttf");
// Str FONT_PATH = S("data/KosugiMaru-Regular.otf");
constexpr u32 FONT_SIZE_PX = 40;

fn void font_init(ErrorContext *err, FontSystem *ctx) {
    Scope scope = scope_open(err);
    ctx->inst = create_font_inst(err, FONT_PATH, FONT_SIZE_PX);
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

    inst->bitmap_sets = arena_push_arr(inst->arena, GlyphBitmapSet, (u64)inst->ft_face->num_glyphs);

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
    arena_release(inst->arena);
}

fn void rasterize_glyph(FontInst *inst, u32 glyph_id) {
    GlyphBitmapSet *bitmap_set = &A(inst->bitmap_sets, glyph_id);
    log_assert(!bitmap_set->rendered);  // Callers should check before calling

    bitmap_set->rendered = true;
    bitmap_set->steps = arena_push_arr(inst->arena, GlyphBitmap, 4);

    for (u64 step_idx = 0; step_idx < bitmap_set->steps.count; step_idx++) {
        GlyphBitmap *bitmap = &A(bitmap_set->steps, step_idx);

        // 2x2 transform matrix with 16.16 fixed-point coefficients
        FT_Matrix matrix = {.xx = 0x10000, .yy = 0x10000};

        // We rasterize at quarter-pixel midpoints. This allows us to floor() the glyph's float X
        // pixel position when determining the subpixel glyph. round() has a problem: if for ex.
        // pixel position 0.8f (subpixel 3.2) is rounded up to 1.0f (subpixel 4.0), this will wrap
        // from step 4 -> step 0 but require offsetting the final glyph position by +1 pixel.
        // Rasterizing at quarter-pixel midpoints avoids needing to distinguish the round-up and
        // round-down cases for step 0.
        //
        // Pen position in 26.6 fixed-point pixels (so 64 = 1 pixel)
        FT_Vector pen = {.x = (i32)(8 + 16 * step_idx), .y = 0};
        FT_Set_Transform(inst->ft_face, &matrix, &pen);

        // TODO re-enable hinting once we can account for spacing discrepancies
        // Also maybe disable on macos for more native look?
        FT_Load_Glyph(inst->ft_face, glyph_id, FT_LOAD_NO_HINTING);
        // if (face->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
        //     bail(err, "TODO: handle bitmap glyph");
        // }
        FT_Render_Glyph(inst->ft_face->glyph, FT_RENDER_MODE_NORMAL);

        FT_Bitmap ft_bitmap = inst->ft_face->glyph->bitmap;
        Arr_u8 tmp_buffer = {.ptr = ft_bitmap.buffer, .count = ft_bitmap.width * ft_bitmap.rows};

        bitmap->texture.format = GLYPH_TEXTURE_FORMAT;
        bitmap->texture.buffer = arr_clone(inst->arena, tmp_buffer);
        bitmap->texture.dims = (SizePX){(u16)ft_bitmap.width, (u16)ft_bitmap.rows};

        bitmap->offset_x = (i16)inst->ft_face->glyph->bitmap_left;
        bitmap->offset_y = (i16)inst->ft_face->glyph->bitmap_top;
    }
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

// TODO REMOVE!!
i32 debug_glyph_step;

fn void font_prepare_to_render(FontSystem *ctx, Str text, PosF pos, FVec_TextureRequest *reqs) {
    Arena *scratch = arena_acquire();

    Arr_ShapedGlyph shape_result = shape_text(scratch, ctx->inst, text);
    for (u64 i = 0; i < shape_result.count; i++) {
        ShapedGlyph shaped_glyph = A(shape_result, i);
        u32 glyph_id = shaped_glyph.glyph_id;
        GlyphBitmapSet *bitmap_set = &A(ctx->inst->bitmap_sets, glyph_id);

        if (!bitmap_set->rendered) {
            rasterize_glyph(ctx->inst, glyph_id);
        }

        // Shaping position of glyph
        f32 glyph_px_x = (f32)shaped_glyph.glyph_x_fu * (f32)ctx->inst->face_size_px /
                         (f32)ctx->inst->ft_face->units_per_EM;
        f32 glyph_px_y = (f32)shaped_glyph.glyph_y_fu * (f32)ctx->inst->face_size_px /
                         (f32)ctx->inst->ft_face->units_per_EM;

        // Calculate X subpixel position without bitmap X offset because
        // 1) We can't know bitmap X offset until we compute subpixel position -> bitmap step
        // 2) Bitmap X offset is in integer pixels
        // Also, refer to subpixel rasterization to understand why this is floor() and not round().
        u64 step_idx = (u64)SDL_floorf((glyph_px_x) * 4.f) % 4;
        if (debug_glyph_step == 0) {
            step_idx = 0;
        }
        GlyphBitmap *bitmap = &A(bitmap_set->steps, step_idx);

        Color color = COLOR_WHITE;
        if (debug_glyph_step == 2) {
            if (step_idx == 0) {
                color = COLOR_WHITE;
            } else if (step_idx == 1) {
                color = COLOR_RED;
            } else if (step_idx == 2) {
                color = COLOR_GREEN;
            } else if (step_idx == 3) {
                color = COLOR_BLUE;
            }
        }

        if (bitmap->texture.dims.w > 0 && bitmap->texture.dims.h > 0) {
            TextureRequest *req = fvec_push_zero(reqs);
            req->texture = some(&bitmap->texture, P_Texture);
            req->top_left_color = color;
            req->top_right_color = color;
            req->bottom_left_color = color;
            req->bottom_right_color = color;

            // Position of glyph bitmap
            f32 window_px_x = pos.x + glyph_px_x + (f32)bitmap->offset_x;
            f32 window_px_y = pos.y - (glyph_px_y + (f32)bitmap->offset_y);

            // Snap position nearest pixel to render glyph pixel-perfect
            // (maybe we shouldn't snap during animations?)
            req->transform = (RectF){
                // Floor because we round to nearest subpixel in bitmap selection
                .x = SDL_floorf(window_px_x),
                // Snap to nearest pixel
                .y = SDL_roundf(window_px_y),
                .w = (f32)bitmap->texture.dims.w,
                .h = (f32)bitmap->texture.dims.h,
            };
        }
    }

    arena_release(scratch);
}
