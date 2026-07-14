#include "timer_ui.h"
#include "ui.h"

fn UI_Box *build_timer_ui(Arena *arena, Session *session, SizePX size) {
    UI_Style style = {};
    UI_Style *s = &style;

    constexpr f32 OUTER_PADDING = 12.f;

    // Outer root
    ui_width_px(s, size.w);
    ui_height_px(s, size.h);
    UI_Box *root = ui_box(arena, s);

    // Inner root
    build_padding(arena, s, root, OUTER_PADDING);
    ui_flags(s, UI_Flag_ChildLayoutY);
    UI_Box *base = ui_box(arena, s);

    // Game name
    ui_parent(s, base);
    ui_width_flex(s);
    ui_height_px(s, 40);
    ui_text(s, session->file.game_name, &session->layout.nunito_sans_bold, 30);
    ui_box(arena, s);

    // Category name
    ui_parent(s, base);
    ui_width_flex(s);
    ui_height_px(s, 40);
    ui_text(s, session->file.category_name, &session->layout.nunito_sans_bold, 30);
    ui_box(arena, s);

    // Debug row 1
    ui_parent(s, base);
    ui_width_flex(s);
    ui_height_px(s, 40);
    ui_text(s, S("iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii"),
            &session->layout.nunito_sans_bold, 20);
    ui_box(arena, s);

    // Debug row 2
    ui_parent(s, base);
    ui_width_flex(s);
    ui_height_px(s, 40);
    ui_text(s, S("人類社会のすべての構成員の固有の尊厳と平等で"),
            &session->layout.kosugi_maru_regular, 25);
    ui_flags(s, UI_Flag_TextAlignRight);
    ui_box(arena, s);

    // Debug row 3
    ui_parent(s, base);
    ui_width_flex(s);
    ui_height_px(s, 40);
    ui_text(s, S("Flight 0x9428 is departing (NOW)."), &session->layout.departure_mono_regular, 22);
    ui_flags(s, UI_Flag_TextAlignLeft);
    ui_box(arena, s);

    // Segments container
    ui_parent(s, base);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_flags(s, UI_Flag_ChildLayoutY);
    UI_Box *segments_container = ui_box(arena, s);

    for (u64 i = 0; i < session->file.segments.count; i++) {
        build_segment_ui(arena, segments_container, session, i);
    }

    layout_ui(root);

    return root;
}

fn void build_segment_ui(Arena *arena, UI_Box *parent, Session *session, u64 segment_idx) {
    UI_Style style = {};
    UI_Style *s = &style;

    constexpr f32 INFO_HEIGHT_PX = 80.f;
    constexpr f32 SEGMENT_HEIGHT_PX = 100.f;
    constexpr f32 ICON_PADDING_PX = 8.f;

    SegmentDef *segment = &A(session->file.segments, segment_idx);

    // Row
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, INFO_HEIGHT_PX);
    ui_flags(s, UI_Flag_ChildLayoutX);
    UI_Box *row = ui_box(arena, s);

    // Icon outer
    ui_parent(s, row);
    ui_width_px(s, SEGMENT_HEIGHT_PX);
    ui_height_flex(s);
    UI_Box *icon_outer = ui_box(arena, s);

    // Icon inner
    if (segment->icon_texture.dims.w > 0 && segment->icon_texture.dims.h > 0) {
        build_padding(arena, s, icon_outer, ICON_PADDING_PX);
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

fn void build_padding(Arena *arena, UI_Style *s, UI_Box *parent, f32 pad_px) {
    // Don't tell anyone we're not using UI_Style
    parent->flags |= UI_Flag_ChildLayoutY;

    // Top pad
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, pad_px);
    ui_box(arena, s);

    // Middle row
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_flags(s, UI_Flag_ChildLayoutX);
    UI_Box *middle_row = ui_box(arena, s);

    // Bottom pad
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, pad_px);
    ui_box(arena, s);

    // Left pad
    ui_parent(s, middle_row);
    ui_width_px(s, pad_px);
    ui_height_flex(s);
    ui_box(arena, s);

    // Right pad
    ui_parent(s, middle_row);
    ui_width_px(s, pad_px);
    ui_height_flex(s);
    ui_box(arena, s);

    // Middle box
    ui_parent(s, middle_row);
    ui_child_idx(s, 1);
    ui_width_flex(s);
    ui_height_flex(s);
    // ... applied to style, so next box will be inserted in the correct position
}
