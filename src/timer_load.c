#include "timer_load.h"
#include "image_utils.h"
#include "platform.h"

fn Session *make_session(ErrorContext *err, Arena *arena, App *app, Str lss_path) {
    Session *session = arena_push(arena, Session);

    load_livesplit_lss(err, arena, lss_path, &session->file);
    Str lsl_path = S("data/layout-botw.lsl");
    load_livesplit_layout(err, arena, lsl_path, &session->layout);

    return session;
}

fn void load_livesplit_lss(ErrorContext *err, Arena *arena, Str lss_path, FileDef *file) {
    Arena *scratch = arena_acquire();
    Scope scope = scope_open(err);

    Arr_u8 xml = os_read_file(err, scratch, lss_path);
    parse_livesplit_lss(err, arena, xml, file);

    scope_close(scope, "Load LiveSplit LSS file '%.*s'", SF(lss_path));
    arena_release(scratch);
}

fn void parse_livesplit_lss(ErrorContext *err, Arena *arena, Arr_u8 xml, FileDef *file) {
    Scope scope = scope_open(err);

    if (!str_is_valid_utf8(xml)) {
        err_report(err, "Invalid UTF-8");
    }

    xao_Reader r = xao_reader((char *)xml.ptr, xml.count);
    xao_Value root = {};
    xao_Value root_tag = {};
    while (xao_iter_tags(&r, root, &root_tag)) {
        if (eq(root_tag, "Run")) {
            xao_Value run_tag = {};
            while (xao_iter_tags(&r, root_tag, &run_tag)) {
                if (eq(run_tag, "GameName")) {
                    file->game_name = str_clone(arena, xml_inner(&r, run_tag));

                } else if (eq(run_tag, "CategoryName")) {
                    file->category_name = str_clone(arena, xml_inner(&r, run_tag));

                } else if (eq(run_tag, "AttemptCount")) {
                    Scope attempt_count_scope = scope_open(err);
                    Str attempts_str = str_clone(arena, xml_inner(&r, run_tag));
                    file->total_attempts = parse_u64(err, attempts_str);
                    scope_close(attempt_count_scope, "Parse AttemptCount");

                } else if (eq(run_tag, "Segments")) {
                    file->segments = parse_livesplit_segments(err, arena, &r, run_tag);
                }
            }
        }
    }

    if (r.error != nullptr) {
        err_report(err, "Failed to parse LSS XML: %s", r.error);
    }

    // Basic validation
    if (is_empty(file->segments)) {
        err_report(err, "No segments found");
    }
    for (u64 i = 0; i < file->segments.count; i++) {
        if (is_empty(A(file->segments, i).name)) {
            err_report(err, "Segment %" PRIu64 " has no name", i + 1);
        }
    }
    if (is_empty(file->game_name)) {
        err_report(err, "Empty game name");
    }

    scope_close(scope, "Parse LiveSplit LSS");
}

fn Arr_SegmentDef parse_livesplit_segments(ErrorContext *err,
                                           Arena *arena,
                                           xao_Reader *r,
                                           xao_Value segments_tag) {
    Scope scope = scope_open(err);

    Vec_SegmentDef segments = {};
    xao_Value seg_tag = {};
    while (xao_iter_tags(r, segments_tag, &seg_tag)) {
        SegmentDef *seg = vec_push_zero(arena, &segments);
        xao_Value attr_tag = {};
        while (xao_iter_tags(r, seg_tag, &attr_tag)) {
            if (eq(attr_tag, "Name")) {
                seg->name = str_clone(arena, xml_inner(r, attr_tag));

            } else if (eq(attr_tag, "Icon")) {
                seg->icon_texture = parse_image(err, arena, r, attr_tag);
            }
        }
    }

    scope_close(scope, "Parse LiveSplit LSS segments");
    return vec_arr(&segments);
}

fn bool eq(xao_Value v, const char *s) {
    u64 size = (u64)v.end - (u64)v.start;
    return size == SDL_strlen(s) && SDL_memcmp(v.start, s, size) == 0;
}

fn Str xml_str(xao_Value v) {
    return (Str){.ptr = (u8 *)v.start, .count = (u64)v.end - (u64)v.start};
}

fn Str xml_inner(xao_Reader *r, xao_Value outer) {
    xao_Value inner = {};
    xao_iter_content(r, outer, &inner);
    return xml_str(inner);
}

fn Opt_Texture parse_image(ErrorContext *err, Arena *arena, xao_Reader *r, xao_Value elem) {
    Arena *scratch = arena_acquire();
    Scope scope = scope_open(err);

    Opt_Texture texture = {};

    Str base64 = xml_inner(r, elem);
    if (base64.count > 0) {
        Arr_u8 full_buf = decode_base64(err, scratch, base64);

        // Just based on my empirical observations... I don't want
        // to parse the crusty Microsoft object serializer wire format
        constexpr u64 IMAGE_OFFSET = 161;
        if (full_buf.count < IMAGE_OFFSET) {
            err_report(err, "Image buffer too short");
        } else {
            Arr_u8 png_buf = arr_slice(full_buf, IMAGE_OFFSET, full_buf.count);
            texture = some(load_texture_from_image(err, arena, png_buf), Texture);
        }
    }

    scope_close(scope, "Parse image: %.*s", SF(xml_str(elem)));
    arena_release(scratch);
    return texture;
}

fn void load_livesplit_layout(ErrorContext *err, Arena *arena, Str lsl_path, Layout *layout) {
    Arena *scratch = arena_acquire();

    layout->nunito_sans_bold.contents = os_read_file(err, arena, S("data/NunitoSans-Bold.ttf"));
    layout->kosugi_maru_regular.contents =
        os_read_file(err, arena, S("data/KosugiMaru-Regular.otf"));
    layout->departure_mono_regular.contents =
        os_read_file(err, arena, S("data/DepartureMono-Regular.otf"));

    Arr_u8 xml = os_read_file(err, scratch, lsl_path);
    parse_livesplit_lsl(err, arena, xml, layout);

    // Fallback fonts
    // (TODO bundle fallback font)
    if (layout->text_font.contents.count == 0) {
        layout->text_font = layout->nunito_sans_bold;
    }
    if (layout->timer_font.contents.count == 0) {
        layout->timer_font = layout->nunito_sans_bold;
    }
    if (layout->times_font.contents.count == 0) {
        layout->times_font = layout->nunito_sans_bold;
    }

    arena_release(scratch);
}

fn void parse_livesplit_lsl(ErrorContext *err, Arena *arena, Arr_u8 xml, Layout *layout) {
    Scope scope = scope_open(err);

    if (!str_is_valid_utf8(xml)) {
        err_report(err, "Invalid UTF-8");
    }

    xao_Reader r = xao_reader((char *)xml.ptr, xml.count);
    xao_Value root = {};
    xao_Value root_tag = {};
    while (xao_iter_tags(&r, root, &root_tag)) {
        if (eq(root_tag, "Layout")) {
            xao_Value layout_child = {};
            while (xao_iter_tags(&r, root_tag, &layout_child)) {
                if (eq(layout_child, "Settings")) {
                    parse_lsl_settings(err, arena, &r, layout_child, layout);
                }
            }
        }
    }

    if (r.error != nullptr) {
        err_report(err, "Failed to parse LSS XML: %s", r.error);
    }

    scope_close(scope, "Parse LiveSplit LSL");
}

fn void parse_lsl_settings(ErrorContext *err,
                           Arena *arena,
                           xao_Reader *r,
                           xao_Value settings_tag,
                           Layout *layout) {
    xao_Value settings_child = {};
    while (xao_iter_tags(r, settings_tag, &settings_child)) {
        // Colors
        if (eq(settings_child, "TextColor")) {
            layout->text_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "BackgroundColor")) {
            layout->background_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "PersonalBestColor")) {
            layout->personal_best_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "AheadGainingTimeColor")) {
            layout->ahead_gaining_time_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "AheadLosingTimeColor")) {
            layout->ahead_losing_time_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "BehindGainingTimeColor")) {
            layout->behind_gaining_time_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "BehindLosingTimeColor")) {
            layout->behind_losing_time_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "BestSegmentColor")) {
            layout->best_segment_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "NotRunningColor")) {
            layout->not_running_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "PausedColor")) {
            layout->paused_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "TextOutlineColor")) {
            layout->text_outline_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, "ShadowsColor")) {
            layout->shadows_color = parse_livesplit_color(err, r, settings_child);

            // Fonts
        } else if (eq(settings_child, "TimesFont")) {
            layout->times_font = parse_livesplit_font(err, arena, r, settings_child);
        } else if (eq(settings_child, "TimerFont")) {
            layout->timer_font = parse_livesplit_font(err, arena, r, settings_child);
        } else if (eq(settings_child, "TextFont")) {
            layout->text_font = parse_livesplit_font(err, arena, r, settings_child);

            // Background
        } else if (eq(settings_child, "BackgroundImage")) {
            layout->background_image = parse_image(err, arena, r, settings_child);
        }
    }
}

fn Color parse_livesplit_color(ErrorContext *err, xao_Reader *r, xao_Value elem) {
    Scope scope = scope_open(err);
    Color color = {};

    Str color_str = xml_inner(r, elem);
    if (color_str.count != 8) {
        err_report(err, "Invalid color length");

    } else if (!str_eq(color_str, S("00000000"))) {
        char *end = (char *)color_str.ptr + color_str.count;
        u64 v = SDL_strtoull((char *)color_str.ptr, &end, 16);
        if (v == 0) {
            err_report(err, "Invalid color");
        }

        color.r = (v >> 16) & 0xff;
        color.g = (v >> 8) & 0xff;
        color.b = (v >> 0) & 0xff;
        color.a = (v >> 24) & 0xff;
    }

    scope_close(scope, "Parse color: '%.*s'", SF(xml_str(elem)));
    return color;
}

fn FontFile parse_livesplit_font(ErrorContext *err, Arena *arena, xao_Reader *r, xao_Value elem) {
    Scope scope = scope_open(err);

    FontFile font_file = {};

    Str base64 = xml_inner(r, elem);
    font_file.contents = decode_base64(err, arena, base64);

    scope_close(scope, "Parse font for %.*s", SF(xml_str(elem)));
    return font_file;
}
