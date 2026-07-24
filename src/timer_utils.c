#include "timer_utils.h"

fn Opt_Duration dur_add(Opt_Duration d1, Opt_Duration d2) {
    return (Opt_Duration){
        .opt = d1.opt + d2.opt,
        .present = d1.present && d2.present,
    };
}

fn Opt_Duration dur_sub(Opt_Duration d1, Opt_Duration d2) {
    return (Opt_Duration){
        .opt = d1.opt - d2.opt,
        .present = d1.present && d2.present,
    };
}

fn Opt_Duration dur_max(Opt_Duration d1, Opt_Duration d2) {
    return (Opt_Duration){
        .opt = max(d1.opt, d2.opt),
        .present = d1.present && d2.present,
    };
}

fn Arr_SegSummary calc_seg_summary(Arena *arena, Session *session) {
    Timer *timer = &session->timer;
    FileDef *file = &session->file;

    Arr_SegSummary summary = arena_push_arr(arena, SegSummary, session->file.segments.count);

    // Copy PB splits and live splits
    for (u64 i = 0; i < file->segments.count; i++) {
        A(summary, i).pb_split = A(file->segments, i).pb_split;
    }
    for (u64 i = 0; i < timer->live_splits.count; i++) {
        A(summary, i).live_split = A(timer->live_splits, i);
    }

    // Calc PB segments
    for (u64 i = 0; i < summary.count; i++) {
        if (i == 0) {
            A(summary, i).pb_segment = A(summary, i).pb_split;
        } else {
            Opt_Duration curr = A(summary, i).pb_split;
            Opt_Duration prev = A(summary, i - 1).pb_split;
            A(summary, i).pb_segment = dur_sub(curr, prev);
        }
    }

    // Calc live splits
    for (u64 i = 0; i < summary.count; i++) {
        if (i == 0) {
            A(summary, i).live_segment = A(summary, i).live_split;
        } else {
            Opt_Duration curr = A(summary, i).live_split;
            Opt_Duration prev = A(summary, i - 1).live_split;
            A(summary, i).live_segment = dur_sub(curr, prev);
        }
    }

    // Calc live deltas
    for (u64 i = 0; i < summary.count; i++) {
        Opt_Duration live_split = A(summary, i).live_split;
        Opt_Duration pb_split = A(summary, i).pb_split;
        A(summary, i).live_delta = dur_sub(live_split, pb_split);
    }

    // Calc live gained/lost
    for (u64 i = 0; i < summary.count; i++) {
        if (i == 0) {
            A(summary, i).gained = A(summary, i).live_delta;
        } else {
            Opt_Duration curr = A(summary, i).live_delta;
            Opt_Duration prev = A(summary, i - 1).live_delta;
            A(summary, i).gained = dur_sub(curr, prev);
        }
    }

    // Calc golds
    for (u64 i = 0; i < summary.count; i++) {
        Opt_Duration prev_gold = A(file->segments, i).best_segment;
        Opt_Duration live_seg = A(summary, i).live_segment;
        if (prev_gold.present) {
            A(summary, i).is_new_gold =
                prev_gold.present && live_seg.present && live_seg.opt < prev_gold.opt;
        } else {
            A(summary, i).is_new_gold = live_seg.present;
        }

        A(summary, i).best_segment = A(summary, i).is_new_gold ? live_seg : prev_gold;
    }

    return summary;
}

fn Opt_Duration calc_best_possible_time(Session *session, Arr_SegSummary summaries) {
    Opt_Duration bpt = some(0, Duration);

    u64 live_split_count = session->timer.live_splits.count;
    if (live_split_count > 0) {
        Opt_Duration curr_split = A(session->timer.live_splits, live_split_count - 1);
        Opt_Duration best_segment = A(summaries, live_split_count - 1).best_segment;
        bpt = dur_max(curr_split, best_segment);
    }

    for (u64 i = live_split_count; i < summaries.count; i++) {
        bpt = dur_add(bpt, A(summaries, i).best_segment);
    }

    return bpt;
}

fn Opt_Duration calc_sum_of_best_segments(Arr_SegSummary summaries) {
    Opt_Duration sob = some(0, Duration);
    for (u64 i = 0; i < summaries.count; i++) {
        sob = dur_add(sob, A(summaries, i).best_segment);
    }
    return sob;
}

fn Str format_opt_duration(Arena *arena,
                           Opt_Duration duration,
                           u32 ms_digits,
                           bool show_plus_prefix) {
    if (duration.present) {
        return format_duration(arena, duration.opt, ms_digits, show_plus_prefix);
    } else {
        return S("–");
    }
}

fn Str format_duration(Arena *arena, Duration duration, u32 ms_digits, bool show_plus_prefix) {
    log_assert(ms_digits <= 3);

    Str sign_str = show_plus_prefix ? S("+") : S("");
    if (duration < 0) {
        sign_str = S("-");
        duration = -duration;
    }

    constexpr u64 DAY_SECS = 60 * 60 * 24;
    constexpr u64 HOUR_SECS = 60 * 60;
    constexpr u64 MINUTE_SECS = 60;
    constexpr u64 MILLISEC_NSECS = 1'000'000;
    constexpr u64 SECOND_NSECS = 1'000'000'000;

    u64 total_seconds = (u64)duration / SECOND_NSECS;
    u64 subsec_nanos = (u64)duration % SECOND_NSECS;

    u64 days = total_seconds / DAY_SECS;
    u64 hours = total_seconds % DAY_SECS / HOUR_SECS;
    u64 minutes = total_seconds % HOUR_SECS / MINUTE_SECS;
    u64 seconds = total_seconds % MINUTE_SECS;
    u64 milliseconds = subsec_nanos / MILLISEC_NSECS;

    Str result = {};
    if (days == 0 && hours == 0 && minutes == 0) {
        result =
            str_format(arena, "%.*s%" PRIu64 ".%03" PRIu64, SF(sign_str), seconds, milliseconds);
    } else if (days == 0 && hours == 0) {
        result = str_format(arena, "%.*s%" PRIu64 ":%02" PRIu64 ".%03" PRIu64, SF(sign_str),
                            minutes, seconds, milliseconds);
    } else if (days == 0) {
        result = str_format(arena, "%.*s%" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%03" PRIu64,
                            SF(sign_str), hours, minutes, seconds, milliseconds);
    } else {
        result = str_format(arena,
                            "%.*s%" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%03" PRIu64,
                            SF(sign_str), days, hours, minutes, seconds, milliseconds);
    }

    return str_slice(result, 0, result.count - (3 - ms_digits));
}
