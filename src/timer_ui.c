#include "timer_ui.h"
#include "ui.h"

fn UI_Box *build_timer_ui(Arena *arena, Session *session, SizePX size) {
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

    layout_ui(root);

    return root;
}

fn UI_Box *build_ui_segments(Arena *arena, Session *session) {
    UI_Box *parent = ui_box(arena, S("segments parent"));
    parent->flags |= UI_Flag_ChildLayoutY;
    ui_flex_x(parent, 1);
    ui_flex_y(parent, 1);

    for (u64 i = 0; i < session->file.segments.count; i++) {
        UI_Box *row = build_segment_ui(arena, session, i);
        ui_parent(arena, row, parent);
    }

    return parent;
}

fn UI_Box *build_segment_ui(Arena *arena, Session *session, u64 segment_idx) {
    SegmentDef *segment = &A(session->file.segments, segment_idx);

    UI_Box *row = ui_box(arena, str_format(arena, "row%" PRIu64, segment_idx));
    ui_flex_x(row, 1);
    ui_fixed_y(row, 80);
    row->flags |= UI_Flag_ChildLayoutX;
    row->id = str_format(arena, "icon%" PRIu64, segment_idx);

    UI_Box *icon = ui_box(arena, S("space1"));
    ui_parent(arena, icon, row);
    ui_fixed_x(icon, 80);
    ui_flex_y(icon, 1);

    if (segment->icon_texture.dims.w > 0 && segment->icon_texture.dims.h > 0) {
        icon->flags |= UI_Flag_DrawTexture;
        icon->texture = &segment->icon_texture;
    }

    for (u64 i = 0; i < 3; i++) {
        UI_Box *col = ui_box(arena, S("space"));
        ui_parent(arena, col, row);
        ui_flex_x(col, 1);
        ui_flex_y(col, 1);
    }

    return row;
}
