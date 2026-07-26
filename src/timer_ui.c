#include "timer_ui.h"
#include "timer_update.h"
#include "timer_utils.h"
#include "ui.h"

// TODO not a lot of principles behind these constants atm
constexpr f32 INFO_HEIGHT_PX = 80.f;
constexpr f32 SEGMENT_HEIGHT_PX = 100.f;
constexpr f32 ICON_PADDING_PX = 8.f;
constexpr f32 SMALL_TIME_WIDTH_PX = 150.f;
constexpr u32 BIG_TIME_FONT_SIZE_PX = 100;
constexpr f32 SMALL_TEXT_OUTLINE_PX = 1.5;
constexpr f32 BIG_TEXT_OUTLINE_PX = 3;
constexpr f32 TEXT_PAD = 10.f;  // ??
const Str SEGMENTS_CONTAINER_ID = S("SegmentsContainer");

fn void build_timer_ui(UI_View *view, Session *session) {
    Arr_SegSummary summaries = calc_seg_summary(view->curr_frame.arena, session);
    UI_Box *root = build_outer_timer_ui(view, session, summaries);
    build_segments_ui(view, session, summaries);
    // build_text_test_ui(view, base, session);

    view->curr_frame.root = root;
}

fn UI_Box *build_outer_timer_ui(UI_View *view, Session *session, Arr_SegSummary summaries) {
    constexpr f32 OUTER_PADDING = 12.f;

    UI_Style style = {};
    UI_Style *s = &style;

    // Outer root
    if (session->layout.background_image.present) {
        ui_texture(s, &session->layout.background_image.opt);
        ui_flags(s, UI_Flag_TextureBlendColor);
        ui_fg_color(s, (Color){.r = 255, .g = 255, .b = 255, .a = 70});
    }
    ui_flags(s, UI_Flag_TextureZoom);
    ui_depth(s, -4);
    UI_Box *root = ui_box(view, s);

    // Inner root
    build_padding(view, s, root, OUTER_PADDING, UI_Flag_IgnoreUserScale);
    ui_flags(s, UI_Flag_ChildLayoutY);
    UI_Box *base = ui_box(view, s);

    build_game_info_ui(view, base, session, summaries);

    // Segments container
    ui_parent(s, base);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_flags(s, UI_Flag_ChildLayoutY);
    ui_box_id(view, s, "%.*s", SF(SEGMENTS_CONTAINER_ID));

    build_big_timer(view, session, summaries, base);
    build_bottom_stats(view, session, summaries, base);

    RectF bbox = {
        .x = 0,
        .y = 0,
        .w = (f32)view->curr_frame.window_size.w,
        .h = (f32)view->curr_frame.window_size.h,
    };
    ui_layout(view, root, bbox);

    return root;
}

fn void build_game_info_ui(UI_View *view,
                           UI_Box *base,
                           Session *session,
                           Arr_SegSummary summaries) {
    UI_Style b1 = {};
    UI_Style *s = &b1;

    UI_Style b2 = {};
    UI_Style *s_header = &b2;

    ui_parent(s_header, base);
    ui_width_flex(s_header);
    ui_height_px(s_header, 40);
    ui_font(s_header, &session->layout.nunito_sans_bold, 32);
    ui_text_outline(s_header, SMALL_TEXT_OUTLINE_PX);
    ui_flags(s_header, UI_Flag_TextAlignXCenter | UI_Flag_TextClipEllipsis);
    ui_fg_color(s_header, session->layout.text_color);

    // Game name
    ui_style(s, s_header);
    ui_text(s, session->file.game_name);
    ui_box(view, s);

    // Category name
    ui_style(s, s_header);
    ui_text(s, session->file.category_name);
    ui_box(view, s);
}

fn void build_segments_ui(UI_View *view, Session *session, Arr_SegSummary summaries) {
    UI_Box *segments_container = ui_find_box(view, SEGMENTS_CONTAINER_ID);
    for (u64 i = 0; i < session->file.segments.count; i++) {
        build_segment_ui(view, segments_container, session, summaries, i);
    }
    RectF container_bbox = ui_get_unscaled_bbox(view, segments_container);
    ui_layout(view, segments_container, container_bbox);
}

fn void build_segment_ui(UI_View *view,
                         UI_Box *parent,
                         Session *session,
                         Arr_SegSummary summaries,
                         u64 segment_idx) {
    UI_Style style = {};
    UI_Style *s = &style;

    SegmentDef *segment = &A(session->file.segments, segment_idx);

    // Row
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, INFO_HEIGHT_PX);
    ui_flags(s, UI_Flag_ChildLayoutX);
    UI_Box *row = ui_box(view, s);

    // Current row background highlight
    bool show_bg =
        session->timer.mode == TimerMode_Running || session->timer.mode == TimerMode_Paused;
    bool is_live_segment = segment_idx == session->timer.live_splits.count;
    if (show_bg && is_live_segment) {
        ui_parent(s, parent);
        ui_width_flex(s);
        ui_height_px(s, INFO_HEIGHT_PX);
        ui_flags(s, UI_Flag_FloatY);
        ui_depth(s, -3);
        ui_color_bg(s, (Color){.r = 23, .g = 40, .b = 200, .a = 127});
        ui_float_y(s, INFO_HEIGHT_PX * (f32)segment_idx);
        ui_box_id(view, s, "CurrSegHL");
    }

    // Icon outer
    ui_parent(s, row);
    ui_width_px(s, SEGMENT_HEIGHT_PX);
    ui_height_flex(s);
    UI_Box *icon_outer = ui_box(view, s);

    // Icon inner
    if (segment->icon_texture.present) {
        build_padding(view, s, icon_outer, ICON_PADDING_PX, UI_Flag_None);
        ui_texture(s, &segment->icon_texture.opt);
        ui_flags(s, UI_Flag_TextureContain);
    }
    ui_box(view, s);

    // Split name
    ui_parent(s, row);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_flags(s, UI_Flag_TextAlignXLeft | UI_Flag_TextClipEllipsis);
    ui_font(s, &session->layout.nunito_sans_bold, 27);
    ui_text_outline(s, SMALL_TEXT_OUTLINE_PX);
    ui_text(s, segment->name);
    ui_fg_color(s, session->layout.text_color);
    ui_box(view, s);

    build_segment_times(view, session, summaries, segment_idx, row);
}

fn void build_segment_times(UI_View *view,
                            Session *session,
                            Arr_SegSummary summaries,
                            u64 segment_idx,
                            UI_Box *row) {
    // Common time style
    UI_Style b3 = {};
    UI_Style *style_template = &b3;
    ui_parent(style_template, row);
    ui_width_px(style_template, SMALL_TIME_WIDTH_PX);
    ui_height_flex(style_template);
    ui_flags(style_template, UI_Flag_TextAlignXRight);
    ui_font(style_template, &session->layout.nunito_sans_bold, 27);
    ui_text_outline(style_template, SMALL_TEXT_OUTLINE_PX);

    build_segment_delta_time(view, session, summaries, segment_idx, row, style_template);
    build_segment_seg_time(view, session, summaries, segment_idx, row, style_template);
    build_segment_split_time(view, session, summaries, segment_idx, row, style_template);
}

fn void build_segment_seg_time(UI_View *view,
                               Session *session,
                               Arr_SegSummary summaries,
                               u64 segment_idx,
                               UI_Box *row,
                               UI_Style *style_template) {
    UI_Style style = {};
    UI_Style *s = &style;

    SegSummary *summary = &A(summaries, segment_idx);
    Opt_Duration t = {};
    if (segment_idx < session->timer.live_splits.count) {
        t = summary->live_segment;
    } else {
        t = summary->pb_segment;
    }
    Str text = format_opt_duration(view->curr_frame.arena, t, 2, false);

    ui_style(s, style_template);
    ui_text(s, text);
    ui_fg_color(s, session->layout.text_color);
    ui_box(view, s);

    pad_box(view, row, TEXT_PAD);
}

fn void build_segment_split_time(UI_View *view,
                                 Session *session,
                                 Arr_SegSummary summaries,
                                 u64 segment_idx,
                                 UI_Box *row,
                                 UI_Style *style_template) {
    UI_Style style = {};
    UI_Style *s = &style;

    SegSummary *summary = &A(summaries, segment_idx);
    Opt_Duration t = {};
    if (segment_idx < session->timer.live_splits.count) {
        t = summary->live_split;
    } else {
        t = summary->pb_split;
    }
    Str text = format_opt_duration(view->curr_frame.arena, t, 2, false);

    ui_style(s, style_template);
    ui_text(s, text);
    ui_fg_color(s, session->layout.text_color);
    ui_box(view, s);

    pad_box(view, row, TEXT_PAD);
}

fn void build_segment_delta_time(UI_View *view,
                                 Session *session,
                                 Arr_SegSummary summaries,
                                 u64 segment_idx,
                                 UI_Box *row,
                                 UI_Style *style_template) {
    UI_Style style = {};
    UI_Style *s = &style;

    SegSummary *summary = &A(summaries, segment_idx);
    Opt_Duration live_split = summary->live_split;
    Opt_Duration live_segment = summary->live_segment;
    Opt_Duration pb_split = summary->pb_split;
    Opt_Duration pb_segment = summary->pb_segment;
    Opt_Duration live_delta = summary->live_delta;

    // TODO this logic seems very stupid and wrong
    bool show = false;
    if (live_segment.present && pb_segment.present) {
        show = live_segment.opt > pb_segment.opt;
    } else if (live_split.present && pb_split.present) {
        show = live_split.opt > pb_split.opt;
    } else {
        show = segment_idx > session->timer.live_splits.count;
    }
    show = true;

    Color text_color = get_delta_color(session, summaries, segment_idx);

    pad_box(view, row, TEXT_PAD);

    ui_style(s, style_template);
    ui_width_text_content(s);  // Leave as much room for split name as possible
    ui_fg_color(s, text_color);
    Str delta = format_opt_duration(view->curr_frame.arena, none(Duration), 1, true);
    if (show && live_delta.present) {
        delta = format_opt_duration(view->curr_frame.arena, live_delta, 1, true);
    }
    ui_text(s, delta);
    ui_box(view, s);

    pad_box(view, row, TEXT_PAD);
}

fn void pad_box(UI_View *view, UI_Box *parent, f32 pad_px) {
    UI_Style style = {};
    UI_Style *s = &style;

    ui_parent(s, parent);
    if (parent->flags & UI_Flag_ChildLayoutX) {
        ui_width_px(s, pad_px);
        ui_height_flex(s);
    } else {
        ui_width_flex(s);
        ui_height_px(s, pad_px);
    }
    ui_box(view, s);
}

fn void build_big_timer(UI_View *view, Session *session, Arr_SegSummary summaries, UI_Box *parent) {
    Instant now = get_current_monotonic_time();
    Duration elapsed = timer_get_elapsed(&session->timer, now);
    Str elapsed_str = format_duration(view->curr_frame.arena, elapsed, 2, false);
    Str elapsed_part1 = str_slice(elapsed_str, 0, elapsed_str.count - 3);
    Str elapsed_part2 = str_slice(elapsed_str, elapsed_str.count - 3, elapsed_str.count);

    UI_Style style = {};
    UI_Style *s = &style;

    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, (f32)BIG_TIME_FONT_SIZE_PX);
    ui_flags(s, UI_Flag_ChildLayoutX);
    UI_Box *big_timer_row = ui_box(view, s);

    // Big timer part 1
    ui_parent(s, big_timer_row);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_flags(s, UI_Flag_TextAlignXRight | UI_Flag_TextAlignYBottom);
    ui_font(s, &session->layout.nunito_sans_bold, BIG_TIME_FONT_SIZE_PX);
    ui_text(s, elapsed_part1);
    ui_text_outline(s, BIG_TEXT_OUTLINE_PX);
    ui_fg_color(s, session->layout.text_color);
    ui_box(view, s);

    // Big timer part 2
    ui_parent(s, big_timer_row);
    ui_width_text_content(s);
    ui_height_flex(s);
    ui_flags(s, UI_Flag_TextAlignXLeft | UI_Flag_TextAlignYBottom);
    ui_font(s, &session->layout.nunito_sans_bold, BIG_TIME_FONT_SIZE_PX * 3 / 4);
    ui_text(s, elapsed_part2);
    ui_text_outline(s, BIG_TEXT_OUTLINE_PX);
    ui_fg_color(s, session->layout.text_color);
    ui_box(view, s);

    // Right Pad
    pad_box(view, big_timer_row, TEXT_PAD);

    // Vertical after big timer
    pad_box(view, parent, TEXT_PAD);
}

fn void build_bottom_stats(UI_View *view,
                           Session *session,
                           Arr_SegSummary summaries,
                           UI_Box *parent) {
    Opt_Duration gained_duration = {};
    if (session->timer.live_splits.count > 0) {
        gained_duration = A(summaries, session->timer.live_splits.count - 1).gained;
    }
    Str gained = format_opt_duration(view->curr_frame.arena, gained_duration, 2, true);
    Color gained_color = session->layout.text_color;
    if (gained_duration.present) {
        gained_color =
            get_gained_color(session, gained_duration.opt <= 0, gained_duration.opt <= 0);
    }

    Str bpt = format_opt_duration(view->curr_frame.arena,
                                  calc_best_possible_time(session, summaries), 2, false);
    Str sob =
        format_opt_duration(view->curr_frame.arena, calc_sum_of_best_segments(summaries), 2, false);

    build_bottom_stat(view, parent, session, S("Previous Segment"), gained, gained_color);
    build_bottom_stat(view, parent, session, S("Best Possible Time"), bpt, COLOR_WHITE);
    build_bottom_stat(view, parent, session, S("Sum of Best Segments"), sob, COLOR_WHITE);
}

fn Color get_delta_color(Session *session, Arr_SegSummary summaries, u64 idx) {
    Color color = session->layout.text_color;

    SegSummary *summary = &A(summaries, idx);
    if (summary->is_new_gold) {
        return session->layout.best_segment_color;

    } else if (summary->gained.present && summary->live_delta.present) {
        bool ahead = summary->live_delta.opt <= 0;
        bool gained = summary->gained.opt <= 0;
        color = get_gained_color(session, ahead, gained);

    } else if (summary->live_delta.present) {
        bool green = summary->live_delta.opt <= 0;
        color = get_gained_color(session, green, green);
    }

    return color;
}

fn Color get_gained_color(Session *session, bool ahead, bool gained) {
    if (ahead && gained) {
        return session->layout.ahead_gaining_time_color;
    }
    if (ahead && !gained) {
        return session->layout.ahead_losing_time_color;
    }
    if (!ahead && gained) {
        return session->layout.behind_gaining_time_color;
    }
    return session->layout.behind_losing_time_color;
}

fn void build_bottom_stat(UI_View *view,
                          UI_Box *parent,
                          Session *session,
                          Str label,
                          Str value,
                          Color value_color) {
    UI_Style style = {};
    UI_Style *s = &style;

    // Row
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, 40);
    ui_flags(s, UI_Flag_ChildLayoutX);
    UI_Box *row = ui_box(view, s);

    // Left Pad
    pad_box(view, row, TEXT_PAD);

    // Label
    ui_parent(s, row);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_font(s, &session->layout.nunito_sans_bold, 27);
    ui_text(s, label);
    ui_flags(s, UI_Flag_TextClipEllipsis);
    ui_text_outline(s, SMALL_TEXT_OUTLINE_PX);
    ui_fg_color(s, session->layout.text_color);
    ui_box(view, s);

    // Value
    ui_parent(s, row);
    ui_width_px(s, SMALL_TIME_WIDTH_PX);
    ui_height_flex(s);
    ui_font(s, &session->layout.nunito_sans_bold, 27);
    ui_flags(s, UI_Flag_TextAlignXRight);
    ui_text(s, value);
    ui_text_outline(s, SMALL_TEXT_OUTLINE_PX);
    ui_fg_color(s, value_color);
    ui_box(view, s);

    // Right Pad
    pad_box(view, row, TEXT_PAD);
}

fn void build_padding(UI_View *view, UI_Style *s, UI_Box *parent, f32 pad_px, UI_Flag flags) {
    // Don't tell anyone we're not using UI_Style
    parent->flags |= UI_Flag_ChildLayoutY;

    // Top pad
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, pad_px);
    ui_flags(s, flags);
    ui_box(view, s);

    // Middle row
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_flex(s);
    ui_flags(s, flags | UI_Flag_ChildLayoutX);
    UI_Box *middle_row = ui_box(view, s);

    // Bottom pad
    ui_parent(s, parent);
    ui_width_flex(s);
    ui_height_px(s, pad_px);
    ui_flags(s, flags);
    ui_box(view, s);

    // Left pad
    ui_parent(s, middle_row);
    ui_width_px(s, pad_px);
    ui_height_flex(s);
    ui_flags(s, flags);
    ui_box(view, s);

    // Right pad
    ui_parent(s, middle_row);
    ui_width_px(s, pad_px);
    ui_height_flex(s);
    ui_flags(s, flags);
    ui_box(view, s);

    // Middle box
    ui_parent(s, middle_row);
    ui_child_idx(s, 1);
    ui_width_flex(s);
    ui_height_flex(s);
    // ... applied to style, so next box will be inserted in the correct position
}

fn void build_text_test_ui(UI_View *view, UI_Box *base, Session *session) {
    UI_Style b1 = {};
    UI_Style *s = &b1;

    UI_Style b2 = {};
    UI_Style *s_row = &b2;

    ui_parent(s_row, base);
    ui_width_flex(s_row);
    ui_height_px(s_row, 40);
    ui_flags(s_row, UI_Flag_TextAlignXLeft | UI_Flag_TextClipEllipsis);

    ui_style(s, s_row);
    ui_font(s, &session->layout.nunito_sans_bold, 30);
    ui_text(s, S("The Legend of Zelda: Tears of the Kingdom"));
    ui_flags(s, UI_Flag_TextAlignXCenter);
    ui_box(view, s);

    ui_style(s, s_row);
    ui_font(s, &session->layout.nunito_sans_bold, 30);
    ui_text(s, S("All Main Quests 1.0.0"));
    ui_flags(s, UI_Flag_TextAlignXCenter);
    ui_box(view, s);

    ui_style(s, s_row);
    ui_font(s, &session->layout.nunito_sans_bold, 20);
    ui_text(s, S("iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii"));
    ui_flags(s, UI_Flag_TextAlignXCenter);
    ui_box(view, s);

    ui_style(s, s_row);
    ui_font(s, &session->layout.kosugi_maru_regular, 25);
    ui_text(s, S("人類社会のすべての構成員の固有の尊厳と平等で"));
    ui_box(view, s);

    ui_style(s, s_row);
    ui_font(s, &session->layout.departure_mono_regular, 22);
    ui_text(s, S("Flight 0x9428 is departing (NOW)."));
    ui_flags(s, UI_Flag_TextAlignXRight);
    ui_box(view, s);

    ui_style(s, s_row);
    ui_font(s, &session->layout.nunito_sans_bold, 22);
    ui_text(s, S("Emoji test… 🍓"));
    ui_box(view, s);
}
