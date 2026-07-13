#include "font.h"
#include "base.h"
#include "freetype/ftglyph.h"

// TODO thread through program properly

// TODO thread through program properly
// Str FONT_PATH = S("data/Roboto-Medium.ttf");
// Str FONT_PATH = S("data/NotoSans-Regular.ttf");
// Str FONT_PATH = S("data/NotoSans-Bold.ttf");
Str FONT_PATH = S("data/NunitoSans-Bold.ttf");
// Str FONT_PATH = S("data/KosugiMaru-Regular.otf");

bool fn font_handle_eq(FontHandle h1, FontHandle h2) {
    return h1.idx == h2.idx && h1.generation == h2.generation;
}

FontHandle next_font_handle(FontSystem *ctx) {
    ctx->last_handle.idx++;
    return ctx->last_handle;
}

fn FontInst *get_or_create_font_inst(FontSystem *ctx, FontRequest *req) {
    FontInst *inst = nullptr;
    for (u64 i = 0; i < ctx->fonts.count; i++) {
        FontInst *curr_inst = &A(ctx->fonts, i);
        bool handles_eq = font_handle_eq(curr_inst->font_file->handle, req->font_file->handle);
        bool size_eq = curr_inst->face_size_px == req->font_size_px;
        if (handles_eq && size_eq) {
            inst = curr_inst;
        }
    }

    if (inst == nullptr) {
        // TODO do font loading / error handling outside render loop
        Arena *err_arena = arena_acquire();
        ErrorContext err_base = {.arena = err_arena};
        ErrorContext *err = &err_base;

        inst = add_font_inst(err, ctx, req->font_file, req->font_size_px);

        if (err_occurred(err)) {
            err_log(err);
            abort();
        }
        arena_release(err_arena);
    }

    return inst;
}

fn void font_init(FontSystem *ctx, Arena *arena) {
    ctx->fonts = fvec_alloc(arena, FontInst, (u64)8);
}

fn FontInst *add_font_inst(ErrorContext *err,
                           FontSystem *ctx,
                           FontFile *font_file,
                           u32 face_size_px) {
    Scope scope = scope_open(err);

    FontInst *inst = fvec_push_zero(&ctx->fonts);

    Arena *inst_arena = arena_acquire();
    inst->arena = inst_arena;
    inst->font_file = font_file;

    if (FT_Init_FreeType(&inst->ft_ctx) != FT_Err_Ok) {
        err_report(err, "Failed to initialize freetype");
    }
    if (FT_New_Memory_Face(inst->ft_ctx, inst->font_file->contents.ptr,
                           (long)inst->font_file->contents.count, 0, &inst->ft_face) != FT_Err_Ok) {
        err_report(err, "Failed to load font face for glyph rendering");
    }
    if (FT_Set_Pixel_Sizes(inst->ft_face, face_size_px, 0) != FT_Err_Ok) {
        err_report(err, "Failed to set font face size");
    }

    font_file->handle = next_font_handle(ctx);
    inst->font_file = font_file;
    inst->face_size_px = face_size_px;
    inst->family_name = str_clone(inst->arena, str_from_c(inst->ft_face->family_name));
    inst->style_name = str_clone(inst->arena, str_from_c(inst->ft_face->style_name));
    inst->bitmap_sets = arena_push_arr(inst->arena, GlyphBitmapSet, (u64)inst->ft_face->num_glyphs);

    inst->kbts_ctx = kbts_CreateShapeContext(0, 0);
    kbts_ShapePushFontFromMemory(inst->kbts_ctx, inst->font_file->contents.ptr,
                                 (int)inst->font_file->contents.count, 0);

    scope_close(scope, "Load font: family = '%.*s', style = '%.*s'", SF(inst->family_name),
                SF(inst->style_name));
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

        // Another option is to rasterize at quarter-pixel midpoints instead of starts, but that
        // breaks pixel fonts which really expect to be rendered at whole pixel offsets.
        // Pen position in 26.6 fixed-point pixels (so 64 = 1 pixel)
        FT_Vector pen = {.x = (i32)(16 * step_idx), .y = 0};
        FT_Set_Transform(inst->ft_face, &matrix, &pen);

        // TODO re-enable hinting once we can account for spacing discrepancies
        // Also maybe disable on macos for more native look?
        FT_Load_Glyph(inst->ft_face, glyph_id, FT_LOAD_NO_HINTING);

        FT_BBox ft_bbox = {};
        {
            FT_Glyph ft_glyph = {};
            FT_Get_Glyph(inst->ft_face->glyph, &ft_glyph);
            // TODO wtf is grid fitting?
            FT_Glyph_Get_CBox(ft_glyph, FT_GLYPH_BBOX_SUBPIXELS, &ft_bbox);
            FT_Done_Glyph(ft_glyph);
        }

        FT_Render_Glyph(inst->ft_face->glyph, FT_RENDER_MODE_NORMAL);

        FT_Bitmap ft_bitmap = inst->ft_face->glyph->bitmap;
        Arr_u8 tmp_buffer = {.ptr = ft_bitmap.buffer, .count = ft_bitmap.width * ft_bitmap.rows};

        *bitmap = (GlyphBitmap){
            .texture.format = GLYPH_TEXTURE_FORMAT,
            .texture.buffer = arr_clone(inst->arena, tmp_buffer),
            .texture.dims = (SizePX){(u16)ft_bitmap.width, (u16)ft_bitmap.rows},
            .bbox.x = (f32)ft_bbox.xMin / 64.f,
            .bbox.y = (f32)ft_bbox.yMin / 64.f,
            .bbox.w = (f32)(ft_bbox.xMax - ft_bbox.xMin) / 64.f,
            .bbox.h = (f32)(ft_bbox.yMax - ft_bbox.yMin) / 64.f,
            .offset_x = (i16)inst->ft_face->glyph->bitmap_left,
            .offset_y = (i16)inst->ft_face->glyph->bitmap_top,
        };
    }
}

// TODO check font for errors on load, but afterwards assume it's good
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

            ShapedGlyph *g = vec_push_zero(arena, &output);
            g->glyph_id = glyph->Id;
            g->glyph_x_fu = glyph_x;
            g->glyph_y_fu = glyph_y;

            cursor_x += glyph->AdvanceX;
            cursor_y += glyph->AdvanceY;
        }
        // run_idx++;
    }

    return vec_arr(&output);
}

fn void font_prepare_to_render(FontSystem *ctx,
                               FontRequest *font_req,
                               FVec_QuadRequest *quad_reqs) {
    Arena *scratch = arena_acquire();

    FontInst *inst = get_or_create_font_inst(ctx, font_req);

    Arr_ShapedGlyph shape_result = shape_text(scratch, inst, font_req->text);
    for (u64 i = 0; i < shape_result.count; i++) {
        ShapedGlyph shaped_glyph = A(shape_result, i);
        u32 glyph_id = shaped_glyph.glyph_id;
        GlyphBitmapSet *bitmap_set = &A(inst->bitmap_sets, glyph_id);

        if (!bitmap_set->rendered) {
            rasterize_glyph(inst, glyph_id);
        }

        // Shaping position of glyph
        f32 glyph_px_x = (f32)shaped_glyph.glyph_x_fu * (f32)inst->face_size_px /
                         (f32)inst->ft_face->units_per_EM;
        f32 glyph_px_y = (f32)shaped_glyph.glyph_y_fu * (f32)inst->face_size_px /
                         (f32)inst->ft_face->units_per_EM;

        // Calculate X subpixel position without bitmap X offset because
        // 1) We can't know bitmap X offset until we compute subpixel position -> bitmap step
        // 2) Bitmap X offset is in integer pixels
        // Why floor() and not round()? Technically round() picks the more correct subpixel offset
        // glyph, BUT rounding e.g. offset 0.8 up to 1.0 would require adding 1 to the x offset (as
        // opposed to rounding an offset of 0.2 down to 0). floor() avoids this, at the cost of
        // incorrectly shifting glyphs -0.125px. This is fine for now.
        u64 step_idx = (u64)SDL_floorf((glyph_px_x) * 4.f) % 4;
        GlyphBitmap *bitmap = &A(bitmap_set->steps, step_idx);

        Color color = COLOR_WHITE;

        if (bitmap->texture.dims.w > 0 && bitmap->texture.dims.h > 0) {
            QuadRequest *quad_req = fvec_push_zero(quad_reqs);
            quad_req->texture = some(&bitmap->texture, P_Texture);
            quad_req->top_left_color = color;
            quad_req->top_right_color = color;
            quad_req->bottom_left_color = color;
            quad_req->bottom_right_color = color;

            // Position of glyph bitmap
            f32 window_px_x = font_req->bbox.x + glyph_px_x + (f32)bitmap->offset_x;
            f32 window_px_y = font_req->bbox.y - (glyph_px_y + (f32)bitmap->offset_y);

            // Snap position nearest pixel to render glyph pixel-perfect
            // (maybe we shouldn't snap during animations?)
            quad_req->transform = (RectF){
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
