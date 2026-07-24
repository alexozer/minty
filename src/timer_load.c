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
    xao_Value run_tag = {};
    while (xao_iter_tags(&r, root, &run_tag)) {
        if (eq(run_tag, S("Run"))) {
            xao_Value run_child_tag = {};
            while (xao_iter_tags(&r, run_tag, &run_child_tag)) {
                if (eq(run_child_tag, S("GameIcon"))) {
                    file->game_icon = parse_image(err, arena, &r, run_child_tag);

                } else if (eq(run_child_tag, S("GameName"))) {
                    file->game_name = xml_inner(arena, &r, run_child_tag);

                } else if (eq(run_child_tag, S("CategoryName"))) {
                    file->category_name = xml_inner(arena, &r, run_child_tag);

                } else if (eq(run_child_tag, S("Offset"))) {
                    Str offset_str = xml_inner_view(&r, run_child_tag);
                    Opt_Duration offset = parse_opt_duration(err, offset_str);
                    if (!offset.present) {
                        err_report(err, "Failed to parse offset");
                    }
                    file->offset = offset.opt;

                } else if (eq(run_child_tag, S("AttemptCount"))) {
                    Scope attempt_count_scope = scope_open(err);
                    Str attempts_str = xml_inner_view(&r, run_child_tag);
                    file->total_attempts = parse_u64(err, attempts_str);
                    scope_close(attempt_count_scope, "Parse AttemptCount");

                } else if (eq(run_child_tag, S("AttemptHistory"))) {
                    file->attempt_history = parse_attempt_history(err, arena, &r, run_child_tag);

                } else if (eq(run_child_tag, S("Segments"))) {
                    file->segments = parse_livesplit_segments(err, arena, &r, run_child_tag);
                }
            }
        }
    }

    if (r.error != nullptr) {
        err_report(err, "Failed to parse LiveSplit splits: %s", r.error);
    }

    // Basic validation
    if (is_empty(file->segments)) {
        err_report(err, "No segments found");
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

        xao_Value seg_child_tag = {};
        while (xao_iter_tags(r, seg_tag, &seg_child_tag)) {
            if (eq(seg_child_tag, S("Name"))) {
                seg->name = xml_inner(arena, r, seg_child_tag);

            } else if (eq(seg_child_tag, S("Icon"))) {
                seg->icon_texture = parse_image(err, arena, r, seg_child_tag);

            } else if (eq(seg_child_tag, S("SplitTimes"))) {
                seg->pb_split = parse_pb(err, r, seg_child_tag);

            } else if (eq(seg_child_tag, S("BestSegmentTime"))) {
                seg->best_segment = parse_realtime(err, r, seg_child_tag);

            } else if (eq(seg_child_tag, S("SegmentHistory"))) {
                seg->segment_history = parse_segment_history(err, arena, r, seg_child_tag);
            }
        }
    }

    scope_close(scope, "Parse LiveSplit LSS segments");
    return vec_arr(&segments);
}

fn Arr_Duration parse_segment_history(ErrorContext *err,
                                      Arena *arena,
                                      xao_Reader *r,
                                      xao_Value history_tag) {
    Vec_Duration segment_history = {};
    xao_Value time_tag = {};
    while (xao_iter_tags(r, history_tag, &time_tag)) {
        Opt_Duration historical_time = parse_realtime(err, r, time_tag);
        if (historical_time.present) {
            vec_push(arena, &segment_history, historical_time.opt);
        }
    }
    return vec_arr(&segment_history);
}

fn Arr_Attempt parse_attempt_history(ErrorContext *err,
                                     Arena *arena,
                                     xao_Reader *r,
                                     xao_Value attempt_history_tag) {
    Vec_Attempt attempts = {};

    xao_Value attempt_tag = {};
    while (xao_iter_tags(r, attempt_history_tag, &attempt_tag)) {
        if (eq(attempt_tag, S("Attempt"))) {
            Attempt *attempt = vec_push_zero(arena, &attempts);

            xao_Value key = {};
            xao_Value value = {};
            while (xao_iter_attrs(r, attempt_tag, &key, &value)) {
                if (eq(key, S("started"))) {
                    attempt->start_time.time = xml_str(arena, value);
                }
                if (eq(key, S("isStartedSynced"))) {
                    attempt->start_time.ntp_synced = xml_bool(value);
                }
                if (eq(key, S("ended"))) {
                    attempt->end_time.time = xml_str(arena, value);
                }
                if (eq(key, S("isEndedSynced"))) {
                    attempt->end_time.ntp_synced = xml_bool(value);
                }
            }

            attempt->run_duration = parse_realtime(err, r, attempt_tag);
        }
    }

    return vec_arr(&attempts);
}

fn Opt_Duration parse_pb(ErrorContext *err, xao_Reader *r, xao_Value split_times_tag) {
    Scope scope = scope_open(err);

    Opt_Duration duration = {};
    xao_Value split_time_tag = {};
    while (xao_iter_tags(r, split_times_tag, &split_time_tag)) {
        bool is_split_time = eq(split_time_tag, S("SplitTime"));
        bool has_pb = has_attr(r, split_time_tag, S("name"), S("Personal Best"));
        if (is_split_time && has_pb) {
            duration = parse_realtime(err, r, split_time_tag);
        }
    }

    scope_close(scope, "Parse PB time");
    return duration;
}

fn Opt_Duration parse_realtime(ErrorContext *err, xao_Reader *r, xao_Value outer_tag) {
    Opt_Duration duration = {};

    xao_Value real_time_tag = {};
    while (xao_iter_tags(r, outer_tag, &real_time_tag)) {
        if (eq(real_time_tag, S("RealTime"))) {
            Str pb_duration_str = xml_inner_view(r, real_time_tag);
            duration = parse_opt_duration(err, pb_duration_str);
        }
    }

    return duration;
}

fn bool has_attr(xao_Reader *r, xao_Value tag, Str key, Str value) {
    xao_Value curr_key = {};
    xao_Value curr_value = {};
    while (xao_iter_attrs(r, tag, &curr_key, &curr_value)) {
        if (eq(curr_key, key) && eq(curr_value, value)) {
            return true;
        }
    }
    return false;
}

fn Opt_Duration parse_opt_duration(ErrorContext *err, Str s) {
    Scope scope = scope_open(err);

    Opt_Duration duration = {};

    if (s.count > 0) {
        // Hours
        StrPair pair = str_split2_err(err, s, ':');
        Str hours_str = pair.left;
        u64 hours = parse_u64(err, hours_str);

        // Minutes
        pair = str_split2_err(err, pair.right, ':');
        Str minutes_str = pair.left;
        u64 minutes = parse_u64(err, minutes_str);

        // Seconds
        pair = str_split2(pair.right, '.');
        Str seconds_str = pair.left;
        u64 seconds = parse_u64(err, seconds_str);

        // Milliseconds
        u64 milliseconds = 0;
        if (!is_empty(pair.right)) {
            Str milliseconds_str = str_slice_err(err, pair.right, 0, 3);
            milliseconds = parse_u64(err, milliseconds_str);
        }

        Duration total = ((i64)hours * DURATION_HOUR) + ((i64)minutes * DURATION_MINUTE) +
                         ((i64)seconds * DURATION_SECOND) +
                         ((i64)milliseconds * DURATION_MILLISECOND);
        duration = some(total, Duration);
    }

    scope_close(scope, "Parse duration '%.*s'", SF(s));
    return duration;
}

fn bool eq(xao_Value v, Str s) {
    Str xml_str = {.ptr = (u8 *)v.start, .count = (u64)(v.end - v.start)};
    return str_eq(xml_str, s);
}

fn Str xml_str(Arena *arena, xao_Value v) {
    Str s = xml_str_view(v);
    return str_clone(arena, s);
}

fn bool xml_bool(xao_Value v) {
    Str s = xml_str_view(v);
    return (str_eq(s, S("True")));
}

fn Str xml_str_view(xao_Value v) {
    return (Str){.ptr = (u8 *)v.start, .count = (u64)v.end - (u64)v.start};
}

fn Str xml_inner(Arena *arena, xao_Reader *r, xao_Value outer) {
    Str inner = xml_inner_view(r, outer);
    return str_clone(arena, inner);
}

fn Str xml_inner_view(xao_Reader *r, xao_Value outer) {
    xao_Value inner = {};
    xao_iter_content(r, outer, &inner);
    return xml_str_view(inner);
}

fn Opt_Texture parse_image(ErrorContext *err, Arena *arena, xao_Reader *r, xao_Value elem) {
    Arena *scratch = arena_acquire();
    Scope scope = scope_open(err);

    Opt_Texture texture = {};

    Str base64 = xml_inner_view(r, elem);
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

    scope_close(scope, "Parse image: %.*s", SF(xml_str_view(elem)));
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
    if (is_empty(layout->text_font.contents)) {
        layout->text_font = layout->nunito_sans_bold;
    }
    if (is_empty(layout->timer_font.contents)) {
        layout->timer_font = layout->nunito_sans_bold;
    }
    if (is_empty(layout->times_font.contents)) {
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
        if (eq(root_tag, S("Layout"))) {
            xao_Value layout_child = {};
            while (xao_iter_tags(&r, root_tag, &layout_child)) {
                if (eq(layout_child, S("Settings"))) {
                    parse_lsl_settings(err, arena, &r, layout_child, layout);
                }
            }
        }
    }

    if (r.error != nullptr) {
        err_report(err, "Failed to parse LiveSplit layout: %s", r.error);
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
        if (eq(settings_child, S("TextColor"))) {
            layout->text_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("BackgroundColor"))) {
            layout->background_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("PersonalBestColor"))) {
            layout->personal_best_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("AheadGainingTimeColor"))) {
            layout->ahead_gaining_time_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("AheadLosingTimeColor"))) {
            layout->ahead_losing_time_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("BehindGainingTimeColor"))) {
            layout->behind_gaining_time_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("BehindLosingTimeColor"))) {
            layout->behind_losing_time_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("BestSegmentColor"))) {
            layout->best_segment_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("NotRunningColor"))) {
            layout->not_running_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("PausedColor"))) {
            layout->paused_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("TextOutlineColor"))) {
            layout->text_outline_color = parse_livesplit_color(err, r, settings_child);
        } else if (eq(settings_child, S("ShadowsColor"))) {
            layout->shadows_color = parse_livesplit_color(err, r, settings_child);

            // Fonts
        } else if (eq(settings_child, S("TimesFont"))) {
            layout->times_font = parse_livesplit_font(err, arena, r, settings_child);
        } else if (eq(settings_child, S("TimerFont"))) {
            layout->timer_font = parse_livesplit_font(err, arena, r, settings_child);
        } else if (eq(settings_child, S("TextFont"))) {
            layout->text_font = parse_livesplit_font(err, arena, r, settings_child);

            // Background
        } else if (eq(settings_child, S("BackgroundImage"))) {
            layout->background_image = parse_image(err, arena, r, settings_child);
        }
    }
}

fn Color parse_livesplit_color(ErrorContext *err, xao_Reader *r, xao_Value elem) {
    Scope scope = scope_open(err);
    Color color = {};

    Str color_str = xml_inner_view(r, elem);
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

    scope_close(scope, "Parse color: '%.*s'", SF(xml_str_view(elem)));
    return color;
}

fn FontFile parse_livesplit_font(ErrorContext *err, Arena *arena, xao_Reader *r, xao_Value elem) {
    Scope scope = scope_open(err);

    FontFile font_file = {};

    Str base64 = xml_inner_view(r, elem);
    font_file.contents = decode_base64(err, arena, base64);

    scope_close(scope, "Parse font for %.*s", SF(xml_str_view(elem)));
    return font_file;
}
