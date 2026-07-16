#include "font.h"
#include <freetype/ftglyph.h>
#include <freetype/ftstroke.h>
#include "base.h"

constexpr u32 LOAD_GLYPH_FLAGS = FT_LOAD_NO_HINTING;

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
        bool outline_eq = curr_inst->outline_radius_px == outline_px;
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
    ctx->fonts = fvec_alloc(arena, FontInst, (u64)8);
}

fn FontInst *add_font_inst(ErrorContext *err,
                           FontSystem *ctx,
                           FontFile *font_file,
                           u32 face_size_px,
                           f32 outline_radius_px) {
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
        inst->outline_radius_px = outline_radius_px;
        inst->bitmap_sets =
            arena_push_arr(inst->arena, GlyphBitmapSet, (u64)inst->ft_face->num_glyphs);

        inst->kbts_ctx = kbts_CreateShapeContext(0, 0);
        kbts_ShapePushFontFromMemory(inst->kbts_ctx, inst->font_file->contents.ptr,
                                     (int)inst->font_file->contents.count, 0);

        inst->center_y_px = compute_face_center_y(inst);

        // Prefetch the glyph ID of ellipsis, if the font has it
        Arr_ShapedGlyph shaped_glyphs = shape_text(inst->arena, inst, S("…"));
        u32 ellipsis_glyph_id = A(shaped_glyphs, 0).glyph_id;
        if (ellipsis_glyph_id != 0) {
            inst->ellipsis_glyph_id = some(ellipsis_glyph_id, u32);
            rasterize_glyph(inst, ellipsis_glyph_id);
        }
    }

    scope_close(scope, "Load font: family = '%.*s', style = '%.*s'", SF(inst->family_name),
                SF(inst->style_name));
    return inst;
}

fn f32 compute_face_center_y(FontInst *inst) {
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

    Arr_ShapedGlyph shaped_glyphs = shape_text(scratch, inst, S("A"));
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

        if (inst->outline_radius_px > 0) {
            FT_Stroker stroker = {};
            FT_Stroker_New(inst->ft_ctx, &stroker);

            // TODO "radius is in the same units as the outline coordinates"...
            // I literally don't know if this means font units, 16.16 pixels, or 26.6 pixels...
            FT_Stroker_Set(stroker, (FT_Fixed)SDL_floorf(inst->outline_radius_px / 64.f),
                           FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
            FT_Glyph_Stroke(&ft_glyph, stroker, true);

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
            g->pos_px.x = (f32)glyph_x * (f32)inst->px_per_em / (f32)inst->ft_face->units_per_EM;
            g->pos_px.y = (f32)glyph_y * (f32)inst->px_per_em / (f32)inst->ft_face->units_per_EM;

            cursor_x += glyph->AdvanceX;
            cursor_y += glyph->AdvanceY;
        }
        // run_idx++;
    }

    return vec_arr(&output);
}

fn void font_prepare_to_render(FontSystem *ctx,
                               UI_Box *box,
                               u16 depth,
                               FVec_QuadRequest *quad_reqs) {
    Arena *scratch = arena_acquire();

    FontInst *inst = get_or_create_font_inst(ctx, box->font_file, box->font_size_px, 0);
    Arr_ShapedGlyph shaped_glyphs = shape_and_align_text(scratch, inst, box);
    emit_glyph_quads(inst, box, shaped_glyphs, depth, quad_reqs);

    arena_release(scratch);
}

fn Arr_ShapedGlyph shape_and_align_text(Arena *arena, FontInst *inst, UI_Box *box) {
    Arr_ShapedGlyph shaped_glyphs = shape_text(arena, inst, box->text_content);
    if (shaped_glyphs.count > 0) {
        // Rasterize glyphs
        for (u64 i = 0; i < shaped_glyphs.count; i++) {
            u32 glyph_id = A(shaped_glyphs, i).glyph_id;
            GlyphBitmapSet *bitmap_set = &A(inst->bitmap_sets, glyph_id);
            if (!bitmap_set->rendered) {
                rasterize_glyph(inst, glyph_id);
            }
        }

        // Calculate left and right bound, and clip if necessary
        f32 x_left_rt_line = 0;
        f32 x_right_rt_line = 0;
        while (true) {
            if (shaped_glyphs.count == 0) {
                break;
            }

            GlyphBitmap *left_bitmap = get_glyph_bitmap(inst, A(shaped_glyphs, 0));
            GlyphBitmap *right_bitmap = get_glyph_bitmap(inst, arr_last(shaped_glyphs));
            x_left_rt_line = A(shaped_glyphs, 0).pos_px.x + left_bitmap->bbox.x;
            x_right_rt_line =
                arr_last(shaped_glyphs).pos_px.x + right_bitmap->bbox.x + right_bitmap->bbox.w;

            f32 width = x_right_rt_line - x_left_rt_line;
            bool clip = box->flags & UI_Flag_TextClipEllipsis;
            if (!clip || width <= box->output_size.w) {
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
                    shaped_glyphs = (Arr_ShapedGlyph){};
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
            x_ref_rt_line = x_right_rt_line;
            x_ref_rt_window = box->output_size.x + box->output_size.w;
        } else if (box->flags & UI_Flag_TextAlignCenter) {
            x_ref_rt_line = (x_left_rt_line + x_right_rt_line) / 2.f;
            x_ref_rt_window = box->output_size.x + (box->output_size.w / 2.f);
        } else {
            // Default to left align
            x_ref_rt_line = x_left_rt_line;
            x_ref_rt_window = box->output_size.x;
        }

        // Reposition shape result so that subpixel bitmap selection respects center transform
        for (u64 i = 0; i < shaped_glyphs.count; i++) {
            ShapedGlyph *g = &A(shaped_glyphs, i);
            g->pos_px.x = g->pos_px.x + x_ref_rt_window - x_ref_rt_line;
        }
    }
    return shaped_glyphs;
}

fn void emit_glyph_quads(FontInst *inst,
                         UI_Box *box,
                         Arr_ShapedGlyph shaped_glyphs,
                         u16 depth,
                         FVec_QuadRequest *quad_reqs) {
    for (u64 i = 0; i < shaped_glyphs.count; i++) {
        ShapedGlyph shaped_glyph = A(shaped_glyphs, i);
        GlyphBitmap *bitmap = get_glyph_bitmap(inst, A(shaped_glyphs, i));

        Color color = COLOR_WHITE;

        if (bitmap->texture.dims.w > 0 && bitmap->texture.dims.h > 0) {
            QuadRequest *quad_req = fvec_push_zero(quad_reqs);
            quad_req->texture = some(&bitmap->texture, P_Texture);
            quad_req->top_left_color = color;
            quad_req->top_right_color = color;
            quad_req->bottom_left_color = color;
            quad_req->bottom_right_color = color;
            quad_req->depth = depth;

            // Position of glyph bitmap
            f32 window_px_x = shaped_glyph.pos_px.x + (f32)bitmap->offset_x;
            f32 center_y_rt_window = box->output_size.y + (box->output_size.h / 2);
            f32 baseline_y_rt_line = (shaped_glyph.pos_px.y + (f32)bitmap->offset_y);
            f32 window_px_y = center_y_rt_window - baseline_y_rt_line + inst->center_y_px;

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
}

fn GlyphBitmap *get_glyph_bitmap(FontInst *inst, ShapedGlyph shaped_glyph) {
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
