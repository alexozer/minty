#include "font.h"

#include <freetype/ftglyph.h>
#include <freetype/ftstroke.h>

#include "base.h"

constexpr u32 LOAD_GLYPH_FLAGS = FT_LOAD_NO_HINTING;
// This shouldn't be necessary with proper cache eviction
constexpr u64 MAX_FONTS = 128;

bool fn font_handle_eq(FontHandle h1, FontHandle h2) {
    return h1.idx == h2.idx && h1.generation == h2.generation;
}

FontHandle next_font_handle(FontSystem *ctx) {
    ctx->last_handle.idx++;
    return ctx->last_handle;
}

fn FontInst *get_or_create_font_inst(FontSystem *ctx,
                                     FontFile *font_file,
                                     u32 px_per_em,
                                     f32 outline_px) {
    FontInst *inst = nullptr;
    for (u64 i = 0; i < ctx->fonts.count; i++) {
        FontInst *curr_inst = &A(ctx->fonts, i);
        bool handles_eq = font_handle_eq(curr_inst->font_file->handle, font_file->handle);
        bool size_eq = curr_inst->px_per_em == px_per_em;
        bool outline_eq = curr_inst->outline_px == outline_px;
        if (handles_eq && size_eq && outline_eq) {
            inst = curr_inst;
        }
    }

    if (inst == nullptr) {
        // TODO do font loading / error handling outside render loop
        Arena *err_arena = arena_acquire();
        ErrorContext err_base = {.arena = err_arena};
        ErrorContext *err = &err_base;

        inst = add_font_inst(err, ctx, font_file, px_per_em, outline_px);

        if (err_occurred(err)) {
            err_log(err);
            abort();
        }
        arena_release(err_arena);
    }

    return inst;
}

fn void font_init(FontSystem *ctx, Arena *arena) {
    ctx->arena = arena_acquire();
    ctx->fonts = fvec_alloc(ctx->arena, FontInst, MAX_FONTS);
}

fn FontInst *add_font_inst(ErrorContext *err,
                           FontSystem *ctx,
                           FontFile *font_file,
                           u32 face_size_px,
                           f32 outline_px) {
    Arena *scratch = arena_acquire();
    Scope scope = scope_open(err);

    FontInst *inst = fvec_push_zero(&ctx->fonts);

    Arena *inst_arena = arena_acquire();
    inst->arena = inst_arena;
    inst->font_file = font_file;

    FT_Error ft_err = 0;

    ft_err = FT_Init_FreeType(&inst->ft_ctx);
    if (ft_err != FT_Err_Ok) {
        err_report(err, "Failed to initialize freetype: %s", FT_Error_String(ft_err));
    }

    ft_err = FT_New_Memory_Face(inst->ft_ctx, inst->font_file->contents.ptr,
                                (long)inst->font_file->contents.count, 0, &inst->ft_face);
    if (ft_err != FT_Err_Ok) {
        err_report(err, "Failed to load font face: %s", FT_Error_String(ft_err));
    }

    ft_err = FT_Set_Pixel_Sizes(inst->ft_face, face_size_px, 0);
    if (ft_err != FT_Err_Ok) {
        err_report(err, "Failed to set font face size: %s", FT_Error_String(ft_err));
    }

    if (!err_occurred(err)) {
        font_file->handle = next_font_handle(ctx);
        inst->font_file = font_file;
        inst->px_per_em = face_size_px;
        inst->family_name = str_clone(inst->arena, str_from_c(inst->ft_face->family_name));
        inst->style_name = str_clone(inst->arena, str_from_c(inst->ft_face->style_name));
        inst->outline_px = outline_px;
        inst->bitmap_sets =
            arena_push_arr(inst->arena, GlyphBitmapSet, (u64)inst->ft_face->num_glyphs);

        inst->kbts_ctx = kbts_CreateShapeContext(0, 0);
        kbts_font *font = kbts_ShapePushFontFromMemory(
            inst->kbts_ctx, inst->font_file->contents.ptr, (int)inst->font_file->contents.count, 0);
        if (font == nullptr) {
            err_report(err, "Failed to parse font");
        } else {
            inst->center_y_px = compute_face_center_y(ctx, inst);

            // Prefetch the glyph ID of ellipsis, if the font has it
            Arr_ShapedGlyphPX shaped_glyphs = shape_text(scratch, ctx, inst, S("…"));
            u32 ellipsis_glyph_id = A(shaped_glyphs, 0).glyph_id;
            if (ellipsis_glyph_id != 0) {
                inst->ellipsis_glyph_id = some(ellipsis_glyph_id, u32);
                rasterize_glyph(inst, ellipsis_glyph_id);
            }
        }
    }

    scope_close(scope, "Load font: family = \"%.*s\", style = \"%.*s\"", SF(inst->family_name),
                SF(inst->style_name));
    arena_release(scratch);
    return inst;
}

fn f32 compute_face_center_y(FontSystem *ctx, FontInst *inst) {
    // https://tonsky.me/blog/centering/
    //
    // This blog post argues that text should be centered by "cap height". Apparently,
    // well-behaved fonts should allow you to compute this with `ascender - descender`, but it
    // didn't work with the test fonts I'm currently using so...
    //
    // Figma has a "cap height to baseline" setting for this, which I think might actually use the
    // height of a capital letter in the font. So let's try this:
    //
    // 1) If the font has the glyph for character "A", use its height as cap height and halve it to
    // get the vertical face center.
    // 2) Otherwise, center about the face bounding box.

    Arena *scratch = arena_acquire();
    f32 center_y_px = 0.f;

    Arr_ShapedGlyphPX shaped_glyphs = shape_text(scratch, ctx, inst, S("A"));
    u32 glyph_id = A(shaped_glyphs, 0).glyph_id;
    if (glyph_id != 0) {
        // 'A' glyph is in font
        FT_Load_Glyph(inst->ft_face, glyph_id, LOAD_GLYPH_FLAGS);
        FT_BBox ft_bbox = {};
        {
            FT_Glyph ft_glyph = {};
            FT_Get_Glyph(inst->ft_face->glyph, &ft_glyph);
            // TODO wtf is grid fitting?
            FT_Glyph_Get_CBox(ft_glyph, FT_GLYPH_BBOX_SUBPIXELS, &ft_bbox);
            FT_Done_Glyph(ft_glyph);
        }
        f32 y_max_px = (f32)(ft_bbox.yMax) / 64.f;
        center_y_px = y_max_px / 2.f;

    } else {
        // Fallback to total glyph bounding box
        f32 y_min_px =
            (f32)inst->ft_face->bbox.yMin * (f32)inst->px_per_em / (f32)inst->ft_face->units_per_EM;
        f32 y_max_px =
            (f32)inst->ft_face->bbox.yMax * (f32)inst->px_per_em / (f32)inst->ft_face->units_per_EM;
        center_y_px = (y_max_px + y_min_px) / 2.f;
    }

    arena_release(scratch);
    return center_y_px;
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

        // Another option is to rasterize at quarter-pixel midpoints instead of starts, but that
        // breaks pixel fonts which really expect to be rendered at whole pixel offsets.
        // Pen position in 26.6 fixed-point pixels (so 64 = 1 pixel)
        FT_Vector pen = {.x = (i32)(16 * step_idx), .y = 0};

        // TODO re-enable hinting once we can account for spacing discrepancies
        // Also maybe disable on macos for more native look?
        FT_Load_Glyph(inst->ft_face, glyph_id, LOAD_GLYPH_FLAGS);
        FT_Glyph ft_glyph = {};
        FT_Get_Glyph(inst->ft_face->glyph, &ft_glyph);

        if (inst->outline_px > 0) {
            FT_Stroker stroker = {};
            FT_Stroker_New(inst->ft_ctx, &stroker);

            // Freetype claims radius has units of "same units as the outline coordinates". I don't
            // know what that is. Font units doesn't seem to be correct - 26.6 pixels seems
            // plausibly correct visually...
            FT_Fixed radius = (FT_Fixed)SDL_roundf(inst->outline_px * (1 << 6));
            FT_Stroker_Set(stroker, radius, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
            bool stroke_inside = false;
            bool destroy_original_glyph = true;
            FT_Glyph_StrokeBorder(&ft_glyph, stroker, stroke_inside, destroy_original_glyph);

            FT_Stroker_Done(stroker);
        }

        FT_BBox ft_bbox = {};
        {
            // TODO wtf is grid fitting?
            FT_Glyph_Get_CBox(ft_glyph, FT_GLYPH_BBOX_SUBPIXELS, &ft_bbox);
        }

        FT_Glyph_To_Bitmap(&ft_glyph, FT_RENDER_MODE_NORMAL, &pen, true);

        FT_BitmapGlyph ft_bitmap = (FT_BitmapGlyph)ft_glyph;
        Arr_u8 tmp_buffer = {.ptr = ft_bitmap->bitmap.buffer,
                             .count = ft_bitmap->bitmap.width * ft_bitmap->bitmap.rows};

        *bitmap = (GlyphBitmap){
            .texture.format = GLYPH_TEXTURE_FORMAT,
            .texture.buffer = arr_clone(inst->arena, tmp_buffer),
            .texture.dims = (SizePX){(u16)ft_bitmap->bitmap.width, (u16)ft_bitmap->bitmap.rows},
            .bbox.x = (f32)ft_bbox.xMin / 64.f,
            .bbox.y = (f32)ft_bbox.yMin / 64.f,
            .bbox.w = (f32)(ft_bbox.xMax - ft_bbox.xMin) / 64.f,
            .bbox.h = (f32)(ft_bbox.yMax - ft_bbox.yMin) / 64.f,
            .offset_x = (i16)ft_bitmap->left,
            .offset_y = (i16)ft_bitmap->top,
        };

        FT_Done_Glyph(ft_glyph);
    }
}

fn Arr_ShapedGlyphPX shape_text(Arena *arena, FontSystem *ctx, FontInst *inst, Str text) {
    Arr_ShapedGlyph shape_result = {};

    if (maps_has(&ctx->shape_cache, text)) {
        // TODO avoid double hash
        shape_result = maps_get(&ctx->shape_cache, text);
    } else {
        // TODO may need to heap allocate to invalidate
        Str stable_text = str_clone(ctx->arena, text);
        shape_result = shape_text_uncached(ctx->arena, inst, text);
        maps_set(ctx->arena, &ctx->shape_cache, stable_text, shape_result);
    }

    return convert_shape_result_to_px(arena, inst, shape_result);
}

// TODO check font for errors on load, but afterwards assume it's good
// TODO arena allocate kbts stuff
// TODO handling style/direction/face runs etc.
fn Arr_ShapedGlyph shape_text_uncached(Arena *arena, FontInst *inst, Str text) {
    if (is_empty(text)) return (Arr_ShapedGlyph){};

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
            g->x_fu = glyph_x;
            g->y_fu = glyph_y;
            g->x_advance_fu = glyph->AdvanceX;

            cursor_x += glyph->AdvanceX;
            cursor_y += glyph->AdvanceY;
        }
        // run_idx++;
    }

    return vec_arr(&output);
}

fn Arr_ShapedGlyphPX convert_shape_result_to_px(Arena *arena,
                                                FontInst *inst,
                                                Arr_ShapedGlyph shape_result) {
    Arr_ShapedGlyphPX shape_result_px = arena_push_arr(arena, ShapedGlyphPX, shape_result.count);
    for (u64 i = 0; i < shape_result_px.count; i++) {
        ShapedGlyph glyph = A(shape_result, i);
        ShapedGlyphPX *glyph_px = &A(shape_result_px, i);
        glyph_px->glyph_id = glyph.glyph_id;
        glyph_px->pos_px.x =
            (f32)glyph.x_fu * (f32)inst->px_per_em / (f32)inst->ft_face->units_per_EM;
        glyph_px->pos_px.y =
            (f32)glyph.y_fu * (f32)inst->px_per_em / (f32)inst->ft_face->units_per_EM;
        glyph_px->x_advance_px =
            (f32)glyph.x_advance_fu * (f32)inst->px_per_em / (f32)inst->ft_face->units_per_EM;
    }
    return shape_result_px;
}

fn void font_prepare_to_render(FontSystem *ctx, UI_Box *box, FVec_QuadRequest *quad_reqs) {
    Arena *scratch = arena_acquire();

    FontInst *non_outline_inst = get_or_create_font_inst(ctx, box->font_file, box->font_size_px, 0);
    FontInst *outline_inst =
        get_or_create_font_inst(ctx, box->font_file, box->font_size_px, box->font_outline_px);

    // Clone because we may mutate when clipping with `…`
    Arr_ShapedGlyphPX shaped_glyphs = shape_text(scratch, ctx, non_outline_inst, box->text_content);

    if (shaped_glyphs.count > 0) {
        // Rasterize glyphs. Must be done before alignment so glyph metrics are available
        for (u64 i = 0; i < shaped_glyphs.count; i++) {
            u32 glyph_id = A(shaped_glyphs, i).glyph_id;
            ensure_bitmap_set_rasterized(non_outline_inst, glyph_id);
            ensure_bitmap_set_rasterized(outline_inst, glyph_id);
        }

        shaped_glyphs = align_text(ctx, box, non_outline_inst, shaped_glyphs);
        emit_glyph_quads(ctx, box, non_outline_inst, outline_inst, shaped_glyphs, quad_reqs);
    }

    arena_release(scratch);
}

fn void ensure_bitmap_set_rasterized(FontInst *inst, u32 glyph_id) {
    GlyphBitmapSet *bitmap_set = &A(inst->bitmap_sets, glyph_id);
    if (!bitmap_set->rendered) {
        rasterize_glyph(inst, glyph_id);
    }
}

fn RectF get_shaped_text_bbox(FontInst *inst,
                              Arr_ShapedGlyphPX shaped_glyphs,
                              TextBBoxType bbox_type) {
    switch (bbox_type) {
    case TextBBoxType_Glyph: {
        GlyphBitmap *left_bitmap = get_glyph_bitmap(inst, A(shaped_glyphs, 0));
        GlyphBitmap *right_bitmap = get_glyph_bitmap(inst, arr_last(shaped_glyphs));
        f32 x_left_rt_line = A(shaped_glyphs, 0).pos_px.x + left_bitmap->bbox.x;
        f32 x_right_rt_line =
            arr_last(shaped_glyphs).pos_px.x + right_bitmap->bbox.x + right_bitmap->bbox.w;
        return (RectF){.x = x_left_rt_line, .w = x_right_rt_line - x_left_rt_line};
    }
    case TextBBoxType_Pen: {
        f32 x_right_rt_line =
            arr_last(shaped_glyphs).pos_px.x + arr_last(shaped_glyphs).x_advance_px;
        return (RectF){.w = x_right_rt_line};
    }
    }
}

fn RectF
get_text_bbox(FontSystem *ctx, FontFile *font, u32 font_size_px, TextBBoxType bbox_type, Str text) {
    Arena *scratch = arena_acquire();

    FontInst *inst = get_or_create_font_inst(ctx, font, font_size_px, 0);
    Arr_ShapedGlyphPX shaped_glyphs = shape_text(scratch, ctx, inst, text);
    RectF bbox = get_shaped_text_bbox(inst, shaped_glyphs, bbox_type);

    arena_release(scratch);
    return bbox;
}

fn Arr_ShapedGlyphPX align_text(FontSystem *ctx,
                                UI_Box *box,
                                FontInst *inst,
                                Arr_ShapedGlyphPX shaped_glyphs) {
    // Calculate left and right bound, and clip if necessary
    RectF text_bbox = {};
    while (true) {
        if (is_empty(shaped_glyphs)) {
            break;
        }

        TextBBoxType bbox_type =
            (box->flags & UI_Flag_TextBBoxGlyph) ? TextBBoxType_Glyph : TextBBoxType_Pen;
        text_bbox = get_shaped_text_bbox(inst, shaped_glyphs, bbox_type);

        f32 width = text_bbox.w;
        bool clip = box->flags & UI_Flag_TextClipEllipsis;
        if (!clip || width <= box->bbox.w) {
            break;
        }

        // If we're going to clip, force left-alignment to prevent jittering
        box->flags &= ~(UI_Flag_TextAlignCenter | UI_Flag_TextAlignRight);
        box->flags |= UI_Flag_TextAlignLeft;

        if (inst->ellipsis_glyph_id.present) {
            u32 ellipsis_id = inst->ellipsis_glyph_id.opt;
            // Add ellipsis and try again
            if (shaped_glyphs.count == 1) {
                // Chop off final glyph :(
                shaped_glyphs = (Arr_ShapedGlyphPX){};
            } else if (arr_last(shaped_glyphs).glyph_id == ellipsis_id) {
                // We already have an ellipsis, so chop off two glyphs
                shaped_glyphs = arr_slice(shaped_glyphs, 0, shaped_glyphs.count - 1);
                arr_last(shaped_glyphs).glyph_id = ellipsis_id;
            } else {
                // Just make the final glyph an ellipsis
                arr_last(shaped_glyphs).glyph_id = ellipsis_id;
            }
        } else {
            // Just chop off a glyph and try again
            shaped_glyphs = arr_slice(shaped_glyphs, 0, shaped_glyphs.count - 1);
        }
    }

    f32 x_ref_rt_line = 0;    // Alignment point (left/center/right) relative to start of line
    f32 x_ref_rt_window = 0;  // Alignment point in window coordinates
    if (box->flags & UI_Flag_TextAlignRight) {
        // TODO debug why right align can overflow right boundary by 1-2px
        x_ref_rt_line = text_bbox.x + text_bbox.w;
        x_ref_rt_window = box->bbox.x + box->bbox.w;
    } else if (box->flags & UI_Flag_TextAlignCenter) {
        x_ref_rt_line = text_bbox.x + (text_bbox.w / 2.f);
        x_ref_rt_window = box->bbox.x + (box->bbox.w / 2.f);
    } else {
        // Default to left align
        x_ref_rt_line = text_bbox.x;
        x_ref_rt_window = box->bbox.x;
    }

    // Reposition shape result so that subpixel bitmap selection respects center transform
    for (u64 i = 0; i < shaped_glyphs.count; i++) {
        ShapedGlyphPX *g = &A(shaped_glyphs, i);
        g->pos_px.x = g->pos_px.x + x_ref_rt_window - x_ref_rt_line;
    }

    return shaped_glyphs;
}

fn void emit_glyph_quads(FontSystem *ctx,
                         UI_Box *box,
                         FontInst *non_outline_inst,
                         FontInst *outline_inst,
                         Arr_ShapedGlyphPX shaped_glyphs,
                         FVec_QuadRequest *quad_reqs) {
    for (u64 i = 0; i < shaped_glyphs.count; i++) {
        ShapedGlyphPX shaped_glyph = A(shaped_glyphs, i);

        if (box->font_outline_px > 0) {
            // Draw outline glyphs at same positions as non-outline glyphs
            GlyphBitmap *bitmap = get_glyph_bitmap(outline_inst, A(shaped_glyphs, i));
            emit_glyph_quad(box, bitmap, shaped_glyph, COLOR_BLACK, (u16)box->depth,
                            non_outline_inst->center_y_px, quad_reqs);
        }
        GlyphBitmap *bitmap = get_glyph_bitmap(non_outline_inst, A(shaped_glyphs, i));
        emit_glyph_quad(box, bitmap, shaped_glyph, COLOR_WHITE, (u16)(box->depth + 1),
                        non_outline_inst->center_y_px, quad_reqs);
    }
}

fn void emit_glyph_quad(UI_Box *box,
                        GlyphBitmap *bitmap,
                        ShapedGlyphPX shaped_glyph,
                        Color color,
                        u16 depth,
                        f32 center_y_px,
                        FVec_QuadRequest *quad_reqs) {
    if (bitmap->texture.dims.w > 0 && bitmap->texture.dims.h > 0) {
        QuadRequest *quad_req = fvec_push_zero(quad_reqs);
        quad_req->texture = some(&bitmap->texture, P_Texture);
        quad_req->top_left_color = color;
        quad_req->top_right_color = color;
        quad_req->bottom_left_color = color;
        quad_req->bottom_right_color = color;
        quad_req->depth = depth;

        // Position of glyph bitmap
        // TODO we should probably group this positioning with the previous positioning during
        // shaping?
        f32 window_px_x = shaped_glyph.pos_px.x + (f32)bitmap->offset_x;
        f32 center_y_rt_window = box->bbox.y + (box->bbox.h / 2);
        f32 baseline_y_rt_line = (shaped_glyph.pos_px.y + (f32)bitmap->offset_y);
        f32 window_px_y = center_y_rt_window - baseline_y_rt_line + center_y_px;

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

fn GlyphBitmap *get_glyph_bitmap(FontInst *inst, ShapedGlyphPX shaped_glyph) {
    GlyphBitmapSet *bitmap_set = &A(inst->bitmap_sets, shaped_glyph.glyph_id);
    // Calculate X subpixel position without bitmap X offset because
    // 1) We can't know bitmap X offset until we compute subpixel position -> bitmap step
    // 2) Bitmap X offset is in integer pixels
    // Why floor() and not round()? Technically round() picks the more correct subpixel
    // offset glyph, BUT rounding e.g. offset 0.8 up to 1.0 would require adding 1 to the x
    // offset (as opposed to rounding an offset of 0.2 down to 0). floor() avoids this, at
    // the cost of incorrectly shifting glyphs -0.125px. This is fine for now.
    u64 step_idx = pos_mod((i64)SDL_floorf((shaped_glyph.pos_px.x) * 4.f), 4);
    return &A(bitmap_set->steps, step_idx);
}
