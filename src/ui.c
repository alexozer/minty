#include "ui.h"
#include "types.h"

// TODO don't require ID for every box
fn UI_Box *ui_box(Arena *arena, Str id) {
    UI_Box *box = arena_push(arena, UI_Box);
    return box;
}

fn void ui_flex_x(UI_Box *box, f32 ratio) {
    box->input_size.w = (UI_Dim){
        .type = UI_DimType_Flex,
        .value = ratio,
    };
}

fn void ui_flex_y(UI_Box *box, f32 ratio) {
    box->input_size.h = (UI_Dim){
        .type = UI_DimType_Flex,
        .value = ratio,
    };
}

fn void ui_fixed_x(UI_Box *box, f32 size_px) {
    box->input_size.w = (UI_Dim){
        .type = UI_DimType_FixedPX,
        .value = size_px,
    };
}
fn void ui_fixed_y(UI_Box *box, f32 size_px) {
    box->input_size.h = (UI_Dim){
        .type = UI_DimType_FixedPX,
        .value = size_px,
    };
}

fn void ui_parent(Arena *arena, UI_Box *child, UI_Box *parent) {
    child->parent = parent;
    vec_push(arena, &parent->childs, child);
}

fn UI_Box *ui_template(Arena *arena, UI_Box *template, Str id) {
    // TODO do this in a more principled way
    UI_Box *box = ui_box(arena, id);
    box->input_size = template->input_size;
    box->text_content = template->text_content;
    box->flags = template->flags;
    box->texture = template->texture;
    if (template->parent) {
        ui_parent(arena, box, template->parent);
    }
    return box;
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
    if (total_fixed_px > parent_size) {
        // Out of room! Make 'em flex instead!
        total_fixed_px = 0;
        for (u64 i = 0; i < parent->childs.count; i++) {
            UI_Dim *in_size = &A(parent->childs, i)->input_size.dims[axis];

            switch (in_size->type) {
            case UI_DimType_FixedPX: {
                in_size->type = UI_DimType_Flex;
                in_size->value /= total_fixed_px;
                break;
            }
            case UI_DimType_Flex: {
                in_size->value = 0;
                break;
            }
            }
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
    f32 total_flex_px = parent_size - total_fixed_px;
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

fn Arr_TextureRequest render_ui(Arena *frame_arena, UI_Box *root) {
    Vec_TextureRequest reqs = {};
    render_ui_impl(frame_arena, root, &reqs);
    return vec_arr(&reqs);
}

fn void render_ui_impl(Arena *frame_arena, UI_Box *box, Vec_TextureRequest *reqs) {
    // if (box->flags & UI_Flag_DrawTexture) {
    // log_assert(box->texture != nullptr);

    // f32 texture_aspect_ratio = (f32)box->texture->dims.w / box->texture->dims.h;
    // RectF transform = scale_rect_proportionally_to_fit(box->output_size,
    // texture_aspect_ratio);
    if (box->childs.count == 0) {
        RectF transform = box->output_size;
        TextureRequest *req = vec_push_zero(frame_arena, reqs);
        // req->texture = box->texture;
        req->transform = transform;
        req->color = COLOR_WHITE;
    }

    for (u64 i = 0; i < box->childs.count; i++) {
        render_ui_impl(frame_arena, A(box->childs, i), reqs);
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
