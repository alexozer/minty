#include "timer_ui.h"
#include "ui.h"

fn UI_Box *build_timer_ui(Arena *arena, Session *session, SizePX size) {
    UI_Style *s = arena_push(arena, UI_Style);

    ui_width_px(s, size.w);
    ui_height_px(s, size.h);
    ui_flags(s, UI_Flag_ChildLayoutY);
    UI_Box *root = ui_box(arena, s);

    // Game name
    ui_parent(s, root);
    ui_width_flex(s);
    ui_height_px(s, 80);
    ui_text(s, session->file.game_name);
    ui_box(arena, s);

    // Category name
    ui_parent(s, root);
    ui_width_flex(s);
    ui_height_px(s, 80);
    ui_text(s, session->file.category_name);
    ui_box(arena, s);

    // Segments container
    ui_parent(s, root);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_flags(s, UI_Flag_ChildLayoutY);
    UI_Box *segments_container = ui_box(arena, s);

    build_ui_segments(arena, s, segments_container, session);

    layout_ui(root);

    return root;
}

fn void build_ui_segments(Arena *arena, UI_Style *s, UI_Box *parent, Session *session) {
    for (u64 i = 0; i < session->file.segments.count; i++) {
        build_segment_ui(arena, s, parent, session, i);
    }
}

fn void build_segment_ui(Arena *arena,
                         UI_Style *s,
                         UI_Box *parent,
                         Session *session,
                         u64 segment_idx) {
    SegmentDef *segment = &A(session->file.segments, segment_idx);

    // Row
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, 80);
    ui_flags(s, UI_Flag_ChildLayoutX);
    UI_Box *row = ui_box(arena, s);

    // Icon
    ui_parent(s, row);
    ui_width_px(s, 80);
    ui_height_flex(s);
    if (segment->icon_texture.dims.w > 0 && segment->icon_texture.dims.h > 0) {
        ui_texture(s, &segment->icon_texture);
    }
    ui_box(arena, s);

    for (u64 i = 0; i < 3; i++) {
        ui_parent(s, row);
        ui_width_flex(s);
        ui_height_flex(s);
        ui_box(arena, s);
    }
}
