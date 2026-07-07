#include "ui.h"

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

// TODO don't require ID for every box
fn UI_Box *ui_box(Arena *arena, Str id) {
    UI_Box *box = arena_push(arena, UI_Box);
    box->id = id;
    return box;
}

fn UI_Box *ui_template(Arena *arena, UI_Box *template, Str id) {
    // TODO do this in a more principled way
    UI_Box *box = ui_box(arena, id);
    box->input_size = template->input_size;
    box->text_content = template->text_content;
    box->flags = template->flags;
    box->texture_id = template->texture_id;
    if (template->parent) {
        ui_parent(arena, box, template->parent);
    }
    return box;
}

fn UI_Box *build_ui_segment(Arena *arena, Session *session, u64 idx) {
    UI_Box *row = ui_box(arena, str_format(arena, "row%" PRIu64, idx));
    ui_flex_x(row, 1);
    ui_fixed_y(row, 80);
    row->flags |= UI_Flag_ChildLayoutX;
    row->id = str_format(arena, "icon%" PRIu64, idx);

    UI_Box *icon = ui_box(arena, S("space1"));
    ui_parent(arena, icon, row);
    ui_fixed_x(icon, 80);
    ui_flex_y(icon, 1);
    icon->flags |= UI_Flag_DrawTexture;

    for (u64 i = 0; i < 3; i++) {
        UI_Box *col = ui_box(arena, S("space"));
        ui_parent(arena, col, row);
        ui_flex_x(col, 1);
        ui_flex_y(col, 1);
    }

    return row;
}

fn UI_Box *build_ui_segments(Arena *arena, Session *session) {
    UI_Box *parent = ui_box(arena, S("segments parent"));
    parent->flags |= UI_Flag_ChildLayoutY;
    ui_flex_x(parent, 1);
    ui_flex_y(parent, 1);

    // for (u64 i = 0; i < session->file.segments.count; i++) {
    UI_Box *row = build_ui_segment(arena, session, 0);
    ui_parent(arena, row, parent);
    // }

    return parent;
}

fn UI_Box *build_ui(Arena *arena, Session *session, SizePX size) {
    UI_Box *root = ui_box(arena, S("root"));
    ui_fixed_x(root, size.w);
    ui_fixed_y(root, size.h);
    root->flags |= UI_Flag_ChildLayoutY;

    UI_Box *child_template = ui_box(arena, S("child template"));
    ui_flex_x(child_template, 1);
    ui_fixed_y(child_template, 40);
    child_template->parent = root;

    // Game name
    UI_Box *game_name = ui_template(arena, child_template, S("game name"));
    game_name->flags |= UI_Flag_DrawText;
    game_name->text_content = session->file.game_name;

    // Category name
    UI_Box *category_name = ui_template(arena, child_template, S("category name"));
    category_name->flags |= UI_Flag_DrawText;
    category_name->text_content = session->file.game_name;

    UI_Box *segments = build_ui_segments(arena, session);
    ui_parent(arena, segments, root);

    return root;
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

fn void layout_ui(UI_Box *root) {
    log_assert(root->input_size.w.type == UI_DimType_FixedPX);
    log_assert(root->input_size.h.type == UI_DimType_FixedPX);
    root->output_size.x = 0;
    root->output_size.y = 0;
    root->output_size.w = root->input_size.w.value;
    root->output_size.h = root->input_size.h.value;
    layout_ui_impl(root);
}

// u64 make_ui_mesh(Arena *arena, SizePX window_size, UI_Box *box, MeshBuilder *mesh) {
//     u64 start_quad_count = mesh->vertices.count / 4;
//
//     // Fake atlas for now
//     Atlas atlas = {.size = window_size};
//     RectPX rect_px = {
//         .x = (u16)SDL_lroundf(box->output_size.x),
//         .y = (u16)SDL_lroundf(box->output_size.y),
//         .w = (u16)SDL_lroundf(box->output_size.w),
//         .h = (u16)SDL_lroundf(box->output_size.h),
//     };
//     push_atlas_quad(window_size, &atlas, mesh, rect_px, rect_px, (Color){});
//
//     for (u64 i = 0; i < box->childs.count; i++) {
//         UI_Box *child = A(box->childs, i);
//         make_ui_mesh(arena, window_size, child, mesh);
//     }
//
//     u64 end_quad_count = mesh->vertices.count / 4;
//     return end_quad_count - start_quad_count;
// }
