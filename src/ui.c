#include "ui.h"
#include "font.h"
#include "types.h"

// TODO don't require ID for every box
fn UI_Box *ui_box(Arena *frame_arena, UI_Style *s) {
    UI_Box *box = arena_push(frame_arena, UI_Box);
    box->flags = s->flags;
    box->input_size = s->input_size;
    box->text_content = s->text_content;
    box->texture = s->texture;
    box->color = s->color;
    box->font_file = s->font_file;
    box->font_size_px = s->font_size_px;

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

fn void ui_text(UI_Style *s, Str text, FontFile *font_file, u32 font_size_px) {
    s->flags |= UI_Flag_DrawText;
    s->text_content = text;
    s->font_file = font_file;
    s->font_size_px = font_size_px;
}

fn void ui_texture(UI_Style *s, Texture *texture) {
    s->flags |= UI_Flag_DrawTexture;
    s->texture = texture;
}

fn void layout_ui(UI_Box *root) {
    log_assert(root->input_size.w.type == UI_DimType_FixedPX);
    log_assert(root->input_size.h.type == UI_DimType_FixedPX);
    root->output_size.x = 0;
    root->output_size.y = 0;
    root->output_size.w = root->input_size.w.value;
    root->output_size.h = root->input_size.h.value;
    layout_ui_impl(root);
}

fn void layout_ui_main_axis(UI_Box *parent, Axis axis) {
    // Uh oh, unbounded array access?!? Call the safety police
    log_assert(axis < c_arr_count(parent->output_size.size.dims));

    f32 parent_pos = parent->output_size.pos.dims[axis];
    f32 parent_size = parent->output_size.size.dims[axis];
    f32 total_fixed_px = 0;
    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Dim *child_input = &A(parent->childs, i)->input_size.dims[axis];

        if (child_input->type == UI_DimType_FixedPX) {
            total_fixed_px += child_input->value;
        }
    }

    // Compute total flex units
    f32 total_flex_units = 0;
    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Dim *in_size = &A(parent->childs, i)->input_size.dims[axis];

        if (in_size->type == UI_DimType_Flex) {
            total_flex_units += in_size->value;
        }
    }

    // Compute all children pos/size
    f32 total_flex_px = max(0.f, parent_size - total_fixed_px);
    f32 current_pos_px = 0;
    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Dim *in_size = &A(parent->childs, i)->input_size.dims[axis];
        f32 *out_size = &A(parent->childs, i)->output_size.size.dims[axis];
        f32 *out_pos = &A(parent->childs, i)->output_size.pos.dims[axis];

        *out_pos = current_pos_px + parent_pos;

        switch (in_size->type) {
        case UI_DimType_FixedPX: {
            *out_size = in_size->value;
            break;
        }
        case UI_DimType_Flex: {
            *out_size = in_size->value / total_flex_units * total_flex_px;
            break;
        }
        }

        current_pos_px += *out_size;
    }
}

fn void layout_ui_cross_axis(UI_Box *parent, Axis axis) {
    // Uh oh, unbounded array access?!? Call the safety police
    log_assert(axis < c_arr_count(parent->output_size.size.dims));

    f32 parent_size = parent->output_size.size.dims[axis];

    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Dim *in_size = &A(parent->childs, i)->input_size.dims[axis];
        f32 *out_size = &A(parent->childs, i)->output_size.size.dims[axis];
        f32 *out_pos = &A(parent->childs, i)->output_size.pos.dims[axis];

        *out_pos = parent->output_size.pos.dims[axis];

        switch (in_size->type) {
        case UI_DimType_FixedPX: {
            *out_size = in_size->value;
            break;
        }
        case UI_DimType_Flex: {
            *out_size = parent_size;
            break;
        }
        }
    }
}

// Compute layout of children, assuming root pos/size is computed
fn void layout_ui_impl(UI_Box *box) {
    log_assert(box->input_size.dims[Axis_X].value > 0);
    log_assert(box->input_size.dims[Axis_Y].value > 0);

    Axis main_axis = Axis_X;
    if (box->flags & UI_Flag_ChildLayoutX) {
        main_axis = Axis_X;
    } else if (box->flags & UI_Flag_ChildLayoutY) {
        main_axis = Axis_Y;
    } else {
        log_assert(is_empty(box->childs));
    }
    layout_ui_main_axis(box, main_axis);
    layout_ui_cross_axis(box, main_axis == Axis_X ? Axis_Y : Axis_X);

    // Recursively compute child layouts
    for (u64 i = 0; i < box->childs.count; i++) {
        UI_Box *child = A(box->childs, i);
        layout_ui_impl(child);
    }
}

fn void render_ui(Arena *frame_arena,
                  UI_Box *root,
                  FontSystem *font_system,
                  FVec_QuadRequest *requests) {
    render_ui_impl(frame_arena, root, font_system, requests);
}

fn void render_ui_impl(Arena *frame_arena,
                       UI_Box *box,
                       FontSystem *font_system,
                       FVec_QuadRequest *quad_reqs) {
    if (box->flags & UI_Flag_DrawTexture) {
        log_assert(box->texture != nullptr);

        f32 texture_aspect_ratio = (f32)box->texture->dims.w / box->texture->dims.h;
        RectF transform = scale_rect_proportionally_to_fit(box->output_size, texture_aspect_ratio);
        QuadRequest *quad_req = fvec_push_zero(quad_reqs);
        quad_req->texture = some(box->texture, P_Texture);
        quad_req->transform = transform;
        quad_req->top_left_color = COLOR_WHITE;
        quad_req->top_right_color = COLOR_WHITE;
        quad_req->bottom_left_color = COLOR_WHITE;
        quad_req->bottom_right_color = COLOR_WHITE;
    }

    if (box->flags & UI_Flag_DrawText) {
        FontRequest font_req = {
            .text = box->text_content,
            .bbox = box->output_size,
            .font_file = box->font_file,
            .font_size_px = box->font_size_px,
            .color = COLOR_WHITE,
        };
        font_prepare_to_render(font_system, &font_req, quad_reqs);
    }

    for (u64 i = 0; i < box->childs.count; i++) {
        render_ui_impl(frame_arena, A(box->childs, i), font_system, quad_reqs);
    }
}

fn RectF scale_rect_proportionally_to_fit(RectF outer, f32 inner_aspect_ratio) {
    f32 outer_aspect_ratio = outer.w / outer.h;
    RectF inner = {};
    if (inner_aspect_ratio > outer_aspect_ratio) {
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

fn void debug_render_ui(Arena *frame_arena, UI_Box *root, FVec_QuadRequest *requests) {
    debug_render_ui_impl(root, 0, requests);
}

fn void debug_render_ui_impl(UI_Box *box, u64 depth, FVec_QuadRequest *reqs) {
    constexpr f32 BORDER_THICKNESS_PX = 2.f;

    RectF box_tf = box->output_size;

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

    QuadRequest *bottom = fvec_push_zero(reqs);
    bottom->transform.x = box_tf.x;
    bottom->transform.y = box_tf.y + box_tf.h - BORDER_THICKNESS_PX;
    bottom->transform.w = box_tf.w;
    bottom->transform.h = BORDER_THICKNESS_PX;
    bottom->top_left_color = color;
    bottom->top_right_color = color;
    bottom->bottom_left_color = color;
    bottom->bottom_right_color = color;

    QuadRequest *left = fvec_push_zero(reqs);
    left->transform.x = box_tf.x;
    left->transform.y = box_tf.y;
    left->transform.w = BORDER_THICKNESS_PX;
    left->transform.h = box_tf.h;
    left->top_left_color = color;
    left->top_right_color = color;
    left->bottom_left_color = color;
    left->bottom_right_color = color;

    QuadRequest *right = fvec_push_zero(reqs);
    right->transform.x = box_tf.x + box_tf.w - BORDER_THICKNESS_PX;
    right->transform.y = box_tf.y;
    right->transform.w = BORDER_THICKNESS_PX;
    right->transform.h = box_tf.h;
    right->top_left_color = color;
    right->top_right_color = color;
    right->bottom_left_color = color;
    right->bottom_right_color = color;

    for (u64 i = 0; i < box->childs.count; i++) {
        debug_render_ui_impl(A(box->childs, i), depth + 1, reqs);
    }
}
