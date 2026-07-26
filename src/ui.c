#include "ui.h"
#include "font.h"
#include "types.h"

constexpr i16 MIN_DEPTH = -4;

fn UI_View ui_view_init(FontSystem *font_system) {
    return (UI_View){
        .font_system = font_system,
        .curr_frame.arena = arena_acquire(),
        .prev_frame.arena = arena_acquire(),
    };
}

fn void ui_view_begin_frame(UI_View *view,
                            SizePX window_size,
                            f32 os_scale,
                            f32 user_scale,
                            bool debug_draw) {
    swap(view->curr_frame, view->prev_frame);
    reset_ui_frame(&view->curr_frame);
    view->curr_frame.window_size = window_size;
    view->curr_frame.os_scale = os_scale;
    view->curr_frame.user_scale = user_scale;
    view->curr_frame.debug_draw = debug_draw;
}

fn void ui_view_end_frame(UI_View *view, FVec_QuadRequest *quad_requests) {
    ui_render(view, quad_requests);
}

fn void reset_ui_frame(UI_ViewFrame *frame) {
    arena_release(frame->arena);
    *frame = (UI_ViewFrame){};
    frame->arena = arena_acquire();
}

// TODO don't require ID for every box
fn UI_Box *ui_box(UI_View *view, UI_Style *s) {
    Arena *frame_arena = view->curr_frame.arena;

    UI_Box *box = arena_push(frame_arena, UI_Box);
    box->flags = s->flags;
    box->input_size = s->input_size;
    box->text_content = s->text_content;
    box->texture = s->texture;
    box->fg_color = s->fg_color;
    box->bg_color = s->bg_color;
    box->font_file = s->font_file;
    box->font_size_px = s->font_size_px;
    box->font_outline_px = s->font_outline_px;
    box->depth = s->depth;
    box->float_pos = s->float_pos;

    if (s->parent != nullptr) {
        box->parent = s->parent;
        if (box->flags & UI_Flag_InsertChildAtIndex) {
            vec_insert(frame_arena, &s->parent->childs, s->child_idx, box);
        } else {
            vec_push(frame_arena, &s->parent->childs, box);
        }
    }

    *s = (UI_Style){};

    return box;
}

fn __attribute__((format(printf, 3, 4))) UI_Box *ui_box_id(UI_View *view,
                                                           UI_Style *s,
                                                           const char *format,
                                                           ...) {
    va_list args;
    va_start(args, format);
    Str id = str_format_v(view->curr_frame.arena, format, args);
    va_end(args);

    UI_Box *box = ui_box(view, s);
    box->id = id;
    maps_set(view->curr_frame.arena, &view->curr_frame.id_box_map, id, box);

    return box;
}

fn void ui_width_px(UI_Style *s, f32 px) {
    s->input_size.w = (UI_Dim){
        .type = UI_DimType_FixedPX,
        .value = px,
    };
}

fn void ui_height_px(UI_Style *s, f32 px) {
    s->input_size.h = (UI_Dim){
        .type = UI_DimType_FixedPX,
        .value = px,
    };
}

fn void ui_width_flex(UI_Style *s) {
    ui_width_flex_ratio(s, 1.f);
}

fn void ui_height_flex(UI_Style *s) {
    ui_height_flex_ratio(s, 1.f);
}

fn void ui_width_flex_ratio(UI_Style *s, f32 ratio) {
    s->input_size.w = (UI_Dim){
        .type = UI_DimType_Flex,
        .value = ratio,
    };
}

fn void ui_height_flex_ratio(UI_Style *s, f32 ratio) {
    s->input_size.h = (UI_Dim){
        .type = UI_DimType_Flex,
        .value = ratio,
    };
}

fn void ui_width_text_content(UI_Style *s) {
    s->input_size.w.type = UI_DimType_TextContent;
}

fn void ui_flags(UI_Style *style, UI_Flag flags) {
    style->flags |= flags;
}

fn void ui_parent(UI_Style *s, UI_Box *parent) {
    s->parent = parent;
}

fn void ui_child_idx(UI_Style *s, u16 idx) {
    s->flags |= UI_Flag_InsertChildAtIndex;
    s->child_idx = idx;
}

fn void ui_text(UI_Style *s, Str text) {
    s->flags |= UI_Flag_DrawText;
    s->text_content = text;
}

fn void ui_text_outline(UI_Style *s, f32 outline_px) {
    s->font_outline_px = outline_px;
}

fn void ui_font(UI_Style *s, FontFile *font_file, u16 font_size_px) {
    s->font_file = font_file;
    s->font_size_px = font_size_px;
}

fn void ui_texture(UI_Style *s, Texture *texture) {
    s->flags |= UI_Flag_DrawTexture;
    s->texture = texture;
}

fn void ui_style(UI_Style *s, UI_Style *ref) {
    SDL_memcpy(s, ref, sizeof(*s));
}

fn void ui_depth(UI_Style *s, i16 depth) {
    s->depth = depth;
}

fn void ui_color_bg(UI_Style *s, Color color) {
    s->flags |= UI_Flag_DrawColoredBG;
    s->bg_color = color;
}

fn void ui_fg_color(UI_Style *s, Color color) {
    s->fg_color = color;
}

fn void ui_bg_color(UI_Style *s, Color color) {
    s->bg_color = color;
}

fn void ui_float_x(UI_Style *s, f32 x) {
    s->float_pos.x = x;
}

fn void ui_float_y(UI_Style *s, f32 y) {
    s->float_pos.y = y;
}

fn void ui_layout(UI_View *view, UI_Box *box, RectF bbox) {
    box->input_size.w.type = UI_DimType_FixedPX;
    box->input_size.w.value = bbox.w;
    box->input_size.h.type = UI_DimType_FixedPX;
    box->input_size.h.value = bbox.h;
    box->bbox = bbox;

    for (u64 i = 0; i < box->childs.count; i++) {
        UI_Box *child = A(box->childs, i);
        scale_ui(view, child);
    }

    animate_ui(view, box);
    layout_ui_impl(view, box);
}

fn void animate_ui(UI_View *view, UI_Box *box) {
    animate_ui_impl(view, box);
}

fn void animate_ui_impl(UI_View *view, UI_Box *box) {
    if (!is_empty(box->id)) {
        UI_Box *prev_box = maps_get(&view->prev_frame.id_box_map, box->id);
        if (prev_box != nullptr) {
            // Time to animate!
            // TODO: framerate-independent lerp
            if (box->flags & UI_Flag_FloatX) {
                box->float_pos.x = lerp(prev_box->float_pos.x, box->float_pos.x, 0.7f);
            }
            if (box->flags & UI_Flag_FloatY) {
                box->float_pos.y = lerp(prev_box->float_pos.y, box->float_pos.y, 0.7f);
            }
        }
    }

    for (u64 i = 0; i < box->childs.count; i++) {
        UI_Box *child = A(box->childs, i);
        animate_ui_impl(view, child);
    }
}

fn void scale_dim(UI_Dim *dim, f32 scale) {
    switch (dim->type) {
    case UI_DimType_FixedPX: {
        dim->value *= scale;
        break;
    }
    case UI_DimType_Flex: {
        // Don't scale flex ratio
        break;
    }
    case UI_DimType_TextContent: {
        // Text content size is scaled by font size
        break;
    }
    }
}

fn void scale_ui(UI_View *view, UI_Box *box) {
    f32 os_scale = view->curr_frame.os_scale;
    f32 user_scale = view->curr_frame.user_scale;
    f32 scale = (box->flags & UI_Flag_IgnoreUserScale) ? os_scale : os_scale * user_scale;
    scale_dim(&box->input_size.w, scale);
    scale_dim(&box->input_size.h, scale);
    box->font_size_px = (u16)SDL_lroundf(box->font_size_px * scale);
    box->font_outline_px *= scale;

    for (u64 i = 0; i < box->childs.count; i++) {
        UI_Box *child = A(box->childs, i);
        scale_ui(view, child);
    }
}

fn void convert_text_content_dims_to_fixed_px(UI_View *view, UI_Box *parent, Axis axis) {
    // Convert text content size constraints to fixed pixel size constraints
    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Box *child = A(parent->childs, i);
        UI_Dim *child_input = &child->input_size.dims[axis];

        if (child_input->type == UI_DimType_TextContent) {
            RectF text_bbox = get_text_bbox(view->font_system, child->font_file,
                                            child->font_size_px, child->text_content);
            child_input->type = UI_DimType_FixedPX;
            child_input->value = text_bbox.w;
        }
    }
}

fn bool is_float(UI_Box *box, Axis axis) {
    bool x_float = box->flags & UI_Flag_FloatX && axis == Axis_X;
    bool y_float = box->flags & UI_Flag_FloatY && axis == Axis_Y;
    return x_float || y_float;
}

fn void layout_ui_main_axis(UI_View *view, UI_Box *parent, Axis axis) {
    // Uh oh, unbounded array access?!? Call the safety police
    log_assert(axis < c_arr_count(parent->bbox.size.dims));

    convert_text_content_dims_to_fixed_px(view, parent, axis);

    f32 parent_pos = parent->bbox.pos.dims[axis];
    f32 parent_size = parent->bbox.size.dims[axis];
    f32 total_fixed_px = 0;
    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Box *child = A(parent->childs, i);
        UI_Dim *child_input = &child->input_size.dims[axis];

        if (!is_float(child, axis) && child_input->type == UI_DimType_FixedPX) {
            total_fixed_px += child_input->value;
        }
    }

    // Compute total flex units
    f32 total_flex_units = 0;
    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Box *child = A(parent->childs, i);
        UI_Dim *in_size = &child->input_size.dims[axis];

        if (!is_float(child, axis) && in_size->type == UI_DimType_Flex) {
            total_flex_units += in_size->value;
        }
    }

    // Compute all children pos/size
    f32 total_flex_px = max(0.f, parent_size - total_fixed_px);
    f32 current_pos_px = 0;
    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Box *child = A(parent->childs, i);
        UI_Dim *in_size = &child->input_size.dims[axis];
        f32 *out_size = &child->bbox.size.dims[axis];
        f32 *out_pos = &child->bbox.pos.dims[axis];

        switch (in_size->type) {
        case UI_DimType_FixedPX: {
            *out_size = in_size->value;
            break;
        }
        case UI_DimType_Flex: {
            *out_size = in_size->value / total_flex_units * total_flex_px;
            break;
        }
        case UI_DimType_TextContent: {
            // Converted these to FixedPX
            log_unreachable();
        }
        }

        // Floating children should have their output position set manually
        if (is_float(child, axis)) {
            *out_pos = parent_pos + child->float_pos.dims[axis];
        } else {
            *out_pos = current_pos_px + parent_pos;
            current_pos_px += *out_size;
        }
    }
}

fn void layout_ui_cross_axis(UI_View *view, UI_Box *parent, Axis axis) {
    // Uh oh, unbounded array access?!? Call the safety police
    log_assert(axis < c_arr_count(parent->bbox.size.dims));

    convert_text_content_dims_to_fixed_px(view, parent, axis);

    f32 parent_size = parent->bbox.size.dims[axis];

    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Box *child = A(parent->childs, i);
        UI_Dim *in_size = &child->input_size.dims[axis];
        f32 *out_size = &child->bbox.size.dims[axis];
        f32 *out_pos = &child->bbox.pos.dims[axis];

        switch (in_size->type) {
        case UI_DimType_FixedPX: {
            *out_size = in_size->value;
            break;
        }
        case UI_DimType_Flex: {
            *out_size = parent_size;
            break;
        }
        case UI_DimType_TextContent: {
            // Converted these to FixedPX
            log_unreachable();
        }
        }

        if (is_float(child, axis)) {
            *out_pos = parent->bbox.pos.dims[axis] + child->float_pos.dims[axis];
        } else {
            *out_pos = parent->bbox.pos.dims[axis];
        }
    }
}

// Compute layout of children, assuming root pos/size is computed
fn void layout_ui_impl(UI_View *view, UI_Box *box) {
    if (box->input_size.w.type != UI_DimType_TextContent) {
        log_assert(box->input_size.w.value > 0);
    }
    if (box->input_size.h.type != UI_DimType_TextContent) {
        log_assert(box->input_size.h.value > 0);
    }

    log_assert(box->depth >= MIN_DEPTH);
    box->depth -= MIN_DEPTH;

    if (box->flags & UI_Flag_ChildLayoutX) {
        layout_ui_main_axis(view, box, Axis_X);
        layout_ui_cross_axis(view, box, Axis_Y);
    } else if (box->flags & UI_Flag_ChildLayoutY) {
        layout_ui_main_axis(view, box, Axis_Y);
        layout_ui_cross_axis(view, box, Axis_X);
    } else if (box->flags & UI_Flag_ChildLayoutZ) {
        // Simple stack
        for (u64 i = 0; i < box->childs.count; i++) {
            UI_Box *child = A(box->childs, i);
            child->bbox = box->bbox;
        }
    } else if (box->childs.count > 0) {
        log_fatal("No child layout direciton provided");
    }

    // Recursively compute child layouts
    for (u64 i = 0; i < box->childs.count; i++) {
        UI_Box *child = A(box->childs, i);
        layout_ui_impl(view, child);
    }
}

fn void ui_render(UI_View *view, FVec_QuadRequest *requests) {
    UI_Box *box = view->curr_frame.root;
    ui_render_impl(view, box, requests);
    if (view->curr_frame.debug_draw) {
        debug_render_ui_impl(box, 0, requests);
    }
}

fn void ui_render_impl(UI_View *view, UI_Box *box, FVec_QuadRequest *quad_reqs) {
    Opt_P_RectF clip_rect = {};
    if (box->flags & UI_Flag_ClipChilds) {
        clip_rect = some(&box->bbox, P_RectF);
    }

    if (box->flags & UI_Flag_DrawTexture) {
        log_assert(box->texture != nullptr);

        bool zoom = false;
        if (box->flags & UI_Flag_TextureZoom) {
            zoom = true;
        } else if (box->flags & UI_Flag_TextureContain) {
            zoom = false;
        } else {
            log_fatal("Texture scale mode required");
        }

        Color color = COLOR_WHITE;
        if (box->flags & UI_Flag_TextureBlendColor) {
            color = box->fg_color;
        }

        f32 texture_aspect_ratio = (f32)box->texture->dims.w / box->texture->dims.h;
        RectF transform = scale_rect_proportionally(box->bbox, texture_aspect_ratio, zoom);
        QuadRequest *quad_req = fvec_push_zero(quad_reqs);
        quad_req->texture = some(box->texture, P_Texture);
        quad_req->transform = transform;
        quad_req->top_left_color = color;
        quad_req->top_right_color = color;
        quad_req->bottom_left_color = color;
        quad_req->bottom_right_color = color;
        quad_req->depth = (u16)(box->depth);
    }

    if (box->flags & UI_Flag_DrawText) {
        font_prepare_to_render(view->font_system, box, quad_reqs);
    }

    if (box->flags & UI_Flag_DrawColoredBG) {
        QuadRequest *quad_req = fvec_push_zero(quad_reqs);
        quad_req->transform = box->bbox;
        quad_req->top_left_color = box->bg_color;
        quad_req->top_right_color = box->bg_color;
        quad_req->bottom_left_color = box->bg_color;
        quad_req->bottom_right_color = box->bg_color;
        quad_req->depth = (u16)(box->depth);
    }

    for (u64 i = 0; i < box->childs.count; i++) {
        UI_Box *child = A(box->childs, i);
        if (!clip_rect.present || rectf_contains(clip_rect.opt, &child->bbox)) {
            ui_render_impl(view, child, quad_reqs);
        }
    }
}

fn bool rectf_contains(RectF *outer, RectF *inner) {
    bool left = inner->x + TOLERANCE_BIG >= outer->x;
    bool right = (inner->x + inner->w) <= (outer->x + outer->w) + TOLERANCE_BIG;
    bool top = inner->y + TOLERANCE_BIG >= outer->y;
    bool bottom = (inner->y + inner->h) <= (outer->y + outer->h) + TOLERANCE_BIG;
    return left && right && top && bottom;
}

// Scale and center a rectangle inside of another, preserving aspect ratio.
fn RectF scale_rect_proportionally(RectF outer, f32 inner_aspect_ratio, bool zoom) {
    f32 outer_aspect_ratio = outer.w / outer.h;
    RectF inner = {};
    if ((inner_aspect_ratio > outer_aspect_ratio) ^ zoom) {
        inner.w = outer.w;
        inner.h = outer.w / inner_aspect_ratio;
        inner.x = outer.x;
        inner.y = outer.y + (outer.h - inner.h) / 2.f;
    } else {
        inner.w = outer.h * inner_aspect_ratio;
        inner.h = outer.h;
        inner.x = outer.x + (outer.w - inner.w) / 2.f;
        inner.y = outer.y;
    }
    return inner;
}

fn void debug_render_ui(UI_View *view, FVec_QuadRequest *requests) {
    UI_Box *root = view->curr_frame.root;
    debug_render_ui_impl(root, 0, requests);
}

fn void debug_render_ui_impl(UI_Box *box, u64 depth, FVec_QuadRequest *reqs) {
    constexpr f32 BORDER_THICKNESS_PX = 2.f;

    RectF box_tf = box->bbox;

    Color color = {.g = 0xff, .a = (u8)((1.f / ((f32)depth + 1)) * 0xff)};

    QuadRequest *top = fvec_push_zero(reqs);
    top->transform.x = box_tf.x;
    top->transform.y = box_tf.y;
    top->transform.w = box_tf.w;
    top->transform.h = BORDER_THICKNESS_PX;
    top->top_left_color = color;
    top->top_right_color = color;
    top->bottom_left_color = color;
    top->bottom_right_color = color;
    top->depth = 15;  // Arbitrary high value

    QuadRequest *bottom = fvec_push_zero(reqs);
    bottom->transform.x = box_tf.x;
    bottom->transform.y = box_tf.y + box_tf.h - BORDER_THICKNESS_PX;
    bottom->transform.w = box_tf.w;
    bottom->transform.h = BORDER_THICKNESS_PX;
    bottom->top_left_color = color;
    bottom->top_right_color = color;
    bottom->bottom_left_color = color;
    bottom->bottom_right_color = color;
    top->depth = 15;  // Arbitrary high value

    QuadRequest *left = fvec_push_zero(reqs);
    left->transform.x = box_tf.x;
    left->transform.y = box_tf.y;
    left->transform.w = BORDER_THICKNESS_PX;
    left->transform.h = box_tf.h;
    left->top_left_color = color;
    left->top_right_color = color;
    left->bottom_left_color = color;
    left->bottom_right_color = color;
    top->depth = 15;  // Arbitrary high value

    QuadRequest *right = fvec_push_zero(reqs);
    right->transform.x = box_tf.x + box_tf.w - BORDER_THICKNESS_PX;
    right->transform.y = box_tf.y;
    right->transform.w = BORDER_THICKNESS_PX;
    right->transform.h = box_tf.h;
    right->top_left_color = color;
    right->top_right_color = color;
    right->bottom_left_color = color;
    right->bottom_right_color = color;
    top->depth = 15;  // Arbitrary high value

    for (u64 i = 0; i < box->childs.count; i++) {
        debug_render_ui_impl(A(box->childs, i), depth + 1, reqs);
    }
}

fn UI_Box *ui_find_box(UI_View *view, Str id) {
    return maps_get(&view->curr_frame.id_box_map, id);
}

fn RectF ui_get_unscaled_bbox(UI_View *view, UI_Box *box) {
    f32 scale = view->curr_frame.os_scale * view->curr_frame.user_scale;
    return (RectF){
        .x = box->bbox.x / scale,
        .y = box->bbox.y / scale,
        .w = box->bbox.w / scale,
        .h = box->bbox.h / scale,
    };
}
