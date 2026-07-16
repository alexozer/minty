#include "timer_ui.h"
#include "timer_update.h"
#include "ui.h"

// TODO not a lot of principles behind these constants atm
constexpr f32 INFO_HEIGHT_PX = 80.f;
constexpr f32 SEGMENT_HEIGHT_PX = 100.f;
constexpr f32 ICON_PADDING_PX = 8.f;
constexpr f32 SMALL_TIME_WIDTH_PX = 150.f;
constexpr u32 BIG_TIME_FONT_SIZE_PX = 90;

fn UI_Box *build_timer_ui(Arena *arena, Session *session, SizePX size) {
    constexpr f32 OUTER_PADDING = 12.f;

    UI_Style style = {};
    UI_Style *s = &style;

    // Outer root
    ui_width_px(s, size.w);
    ui_height_px(s, size.h);
    if (session->layout.background_image.present) {
        ui_texture(s, &session->layout.background_image.opt);
        ui_flags(s, UI_Flag_TextureBlendColor);
        s->color = (Color){.r = 255, .g = 255, .b = 255, .a = 70};
    }
    ui_flags(s, UI_Flag_TextureZoom);
    ui_depth(s, -1);
    UI_Box *root = ui_box(arena, s);

    // Inner root
    build_padding(arena, s, root, OUTER_PADDING);
    ui_flags(s, UI_Flag_ChildLayoutY);
    UI_Box *base = ui_box(arena, s);

    // build_timer_ui_impl(arena, base, session, size);
    build_text_test_ui(arena, base, session, size);

    layout_ui(root);
    return root;
}

fn void build_timer_ui_impl(Arena *arena, UI_Box *base, Session *session, SizePX size) {
    UI_Style style = {};
    UI_Style *s = &style;

    build_game_info_ui(arena, base, session, size);

    // Segments container
    ui_parent(s, base);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_flags(s, UI_Flag_ChildLayoutY | UI_Flag_ClipChilds);
    UI_Box *segments_container = ui_box(arena, s);

    for (u64 i = 0; i < session->file.segments.count; i++) {
        build_segment_ui(arena, segments_container, session, i);
    }

    build_bottom_timer_ui(arena, session, base);
}

fn void build_game_info_ui(Arena *arena, UI_Box *base, Session *session, SizePX size) {
    UI_Style b1 = {};
    UI_Style *s = &b1;

    UI_Style b2 = {};
    UI_Style *s_header = &b2;

    ui_parent(s_header, base);
    ui_width_flex(s_header);
    ui_height_px(s_header, 40);
    ui_font(s_header, &session->layout.nunito_sans_bold, 32);
    ui_flags(s_header, UI_Flag_TextAlignCenter | UI_Flag_TextClipEllipsis);

    // Game name
    ui_style(s, s_header);
    ui_text(s, session->file.game_name);
    ui_box(arena, s);

    // Category name
    ui_style(s, s_header);
    ui_text(s, session->file.category_name);
    ui_box(arena, s);
}

fn void build_segment_ui(Arena *arena, UI_Box *parent, Session *session, u64 segment_idx) {
    UI_Style style = {};
    UI_Style *s = &style;

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
    if (segment->icon_texture.present) {
        build_padding(arena, s, icon_outer, ICON_PADDING_PX);
        ui_texture(s, &segment->icon_texture.opt);
        ui_flags(s, UI_Flag_TextureContain);
    }
    ui_box(arena, s);

    // Split name
    ui_parent(s, row);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_flags(s, UI_Flag_TextAlignLeft | UI_Flag_TextClipEllipsis);
    ui_font(s, &session->layout.nunito_sans_bold, 27);
    ui_text(s, segment->name);
    ui_box(arena, s);

    // A couple fake times

    {
        UI_Style b3 = {};
        UI_Style *s_time = &b3;

        ui_parent(s_time, row);
        ui_width_px(s_time, SMALL_TIME_WIDTH_PX);
        ui_height_flex(s_time);
        ui_flags(s_time, UI_Flag_TextAlignRight);
        ui_font(s_time, &session->layout.nunito_sans_bold, 27);

        Str duration_str = format_duration(arena, (Duration)(segment_idx * 78273472834), 2, false);

        ui_style(s, s_time);
        ui_text(s, duration_str);
        ui_box(arena, s);

        // Pad
        ui_parent(s, row);
        ui_width_px(s, 10);
        ui_height_flex(s);
        ui_box(arena, s);

        ui_style(s, s_time);
        ui_text(s, duration_str);
        ui_box(arena, s);

        // Pad
        ui_parent(s, row);
        ui_width_px(s, 10);
        ui_height_flex(s);
        ui_box(arena, s);
    }
}

fn void build_bottom_timer_ui(Arena *arena, Session *session, UI_Box *parent) {
    UI_Style style = {};
    UI_Style *s = &style;

    // Big timer
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, (f32)BIG_TIME_FONT_SIZE_PX * 1.05f);
    ui_flags(s, UI_Flag_TextAlignRight);
    ui_font(s, &session->layout.nunito_sans_bold, BIG_TIME_FONT_SIZE_PX);
    ui_text(s, format_duration(arena, 7758289389452, 2, false));
    ui_box(arena, s);

    build_bottom_stat(arena, parent, session, S("Previous Segment"),
                      format_duration(arena, -7238523332, 2, false));
    build_bottom_stat(arena, parent, session, S("Best Possible Time"),
                      format_duration(arena, 7234234238523, 2, false));
}

fn void build_bottom_stat(Arena *arena, UI_Box *parent, Session *session, Str label, Str value) {
    UI_Style style = {};
    UI_Style *s = &style;

    // Row
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, 40);
    ui_flags(s, UI_Flag_ChildLayoutX);
    UI_Box *row = ui_box(arena, s);

    // Left Pad
    ui_parent(s, row);
    ui_width_px(s, 10);
    ui_height_flex(s);
    ui_box(arena, s);

    // Label
    ui_parent(s, row);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_font(s, &session->layout.nunito_sans_bold, 27);
    ui_text(s, label);
    ui_flags(s, UI_Flag_TextClipEllipsis);
    ui_box(arena, s);

    // Value
    ui_parent(s, row);
    ui_width_px(s, SMALL_TIME_WIDTH_PX);
    ui_height_flex(s);
    ui_font(s, &session->layout.nunito_sans_bold, 27);
    ui_flags(s, UI_Flag_TextAlignRight);
    ui_text(s, value);
    ui_box(arena, s);

    // Right Pad
    ui_parent(s, row);
    ui_width_px(s, 10);
    ui_height_flex(s);
    ui_box(arena, s);
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

fn void build_text_test_ui(Arena *arena, UI_Box *base, Session *session, SizePX size) {
    UI_Style b1 = {};
    UI_Style *s = &b1;

    UI_Style b2 = {};
    UI_Style *s_row = &b2;

    ui_parent(s_row, base);
    ui_width_flex(s_row);
    ui_height_px(s_row, 40);
    ui_flags(s_row, UI_Flag_TextAlignLeft | UI_Flag_TextClipEllipsis);

    ui_style(s, s_row);
    ui_font(s, &session->layout.nunito_sans_bold, 30);
    ui_text(s, S("The Legend of Zelda: Tears of the Kingdom"));
    ui_flags(s, UI_Flag_TextAlignCenter);
    ui_box(arena, s);

    ui_style(s, s_row);
    ui_font(s, &session->layout.nunito_sans_bold, 30);
    ui_text(s, S("All Main Quests 1.0.0"));
    ui_flags(s, UI_Flag_TextAlignCenter);
    ui_box(arena, s);

    ui_style(s, s_row);
    ui_font(s, &session->layout.nunito_sans_bold, 20);
    ui_text(s, S("iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii"));
    ui_flags(s, UI_Flag_TextAlignCenter);
    ui_box(arena, s);

    ui_style(s, s_row);
    ui_font(s, &session->layout.kosugi_maru_regular, 25);
    ui_text(s, S("人類社会のすべての構成員の固有の尊厳と平等で"));
    ui_box(arena, s);

    ui_style(s, s_row);
    ui_font(s, &session->layout.departure_mono_regular, 22);
    ui_text(s, S("Flight 0x9428 is departing (NOW)."));
    ui_flags(s, UI_Flag_TextAlignRight);
    ui_box(arena, s);

    ui_style(s, s_row);
    ui_font(s, &session->layout.nunito_sans_bold, 22);
    ui_text(s, S("Emoji test… 🍓"));
    ui_box(arena, s);
}
