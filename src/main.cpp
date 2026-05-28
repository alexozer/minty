#include "base.hpp"
#include "platform.hpp"

#include <stdio.h>
#include <assert.h>

Opt<Duration> operator+(const Opt<Duration>& d1, const Opt<Duration>& d2) {
    return {
        .present = d1.present && d2.present,
        .value = d1.value + d2.value,
    };
}

Opt<Duration> operator-(const Opt<Duration>& d1, const Opt<Duration>& d2) {
    return {
        .present = d1.present && d2.present,
        .value = d1.value - d2.value,
    };
}

struct SplitRecord {
    u64 attempt_num;
    Arr<Opt<Duration>> splits;
};

struct Segment {
    Str name;
    Str icon; // TODO a proper decode or something
};

struct SplitsFile {
    Str game_name;
    Str category_name;
    u64 total_attempts;
    u64 completed_attempts;
    Arr<Segment> segments;
    Opt<SplitRecord> personal_best;
    Arr<Opt<Duration>> golds;
};

enum class TimerMode {
    Init,
    Running,
    Paused,
    Finished,
};

struct TimerState {
    TimerMode mode;
    Vec<Opt<Duration>> live_splits;
    Instant start_time;
    Instant paused_time;
    Duration total_paused_duration;
};

struct SegSummary {
    Opt<Duration> live_split;
    Opt<Duration> live_seg;

    // How far ahead/behind this split is compared to PB
    Opt<Duration> live_delta;

    // Duration gained or lost this split relative to PB
    Opt<Duration> gained;

    Opt<Duration> pb_split;
    Opt<Duration> pb_seg;

    bool is_new_gold;
};

Arr<SegSummary> calc_seg_summary(Arena *arena, TimerState *timer, SplitsFile *file) {
    Arr<SegSummary> summary = arena_push_arr<SegSummary>(arena, timer->live_splits.count);

    // Calc PB splits
    if (file->personal_best.present) {
        Arr<Opt<Duration>> pb_splits = file->personal_best.value.splits;
        for (u64 i = 0; i < pb_splits.count; i++) {
            summary[i].pb_split = pb_splits[i];
        }
        for (u64 i = 0; i < pb_splits.count; i++) {
            if (i == 0) {
                summary[i].pb_seg = summary[i].pb_split;
            } else {
                summary[i].pb_seg = summary[i].pb_split - summary[i - 1].pb_split;
            }
        }
    }

    // Calc live splits
    for (u64 i = 0; i < summary.count; i++) {
        summary[i].live_split = timer->live_splits[i];
    }
    for (u64 i = 0; i < summary.count; i++) {
        if (i == 0) {
            summary[i].live_seg = summary[i].live_split;
        } else {
            summary[i].live_seg = summary[i].live_split - summary[i - 1].live_split;
        }
    }

    // Calc live deltas
    for (u64 i = 0; i < summary.count; i++) {
        summary[i].live_delta = summary[i].live_split - summary[i].pb_split;
    }
    for (u64 i = 0; i < summary.count; i++) {
        if (i == 0) {
            summary[i].gained = summary[i].live_delta;
        } else {
            summary[i].gained = summary[i].live_delta - summary[i - 1].live_delta;
        }
    }

    // Calc golds
    for (u64 i = 0; i < summary.count; i++) {
        Opt<Duration> prev_gold = file->golds[i];
        Opt<Duration> live_seg = summary[i].live_seg;
        summary[i].is_new_gold = prev_gold.present && live_seg.present && \
                                 live_seg.value < prev_gold.value;
    }

    return summary;
}

Str format_duration(Arena *arena, Duration duration, u32 ms_digits, bool show_plus_prefix) {
    assert(ms_digits <= 3);

    Str sign_str = show_plus_prefix ? S("+") : S("");
    if (duration < DURATION_ZERO) {
        sign_str = S("-");
    }

    constexpr u64 DAY_SECS = 60 * 60 * 24;
    constexpr u64 HOUR_SECS = 60 * 60;
    constexpr u64 MINUTE_SECS = 60;
    constexpr u64 MILLISEC_NSECS = 1'000'000;

    u64 total_seconds = duration_seconds(duration);
    u32 subsec_nanos = duration_subsec_nanos(duration);

    u64 days = total_seconds / DAY_SECS;
    u64 hours = total_seconds % DAY_SECS / HOUR_SECS;
    u64 minutes = total_seconds % HOUR_SECS / MINUTE_SECS;
    u64 seconds = total_seconds % MINUTE_SECS;
    u64 milliseconds = subsec_nanos / MILLISEC_NSECS;

    Str result = {};
    if (days == 0 && hours == 0 && minutes == 0) {
        result = str_format(arena, "%.*s%" PRIu64 ".%03" PRIu64,
                SF(sign_str), seconds, milliseconds);
    } else if (days == 0 && hours == 0) {
        result = str_format(arena, "%.*s%" PRIu64 ":%02" PRIu64 ".%03" PRIu64,
                SF(sign_str), minutes, seconds, milliseconds);
    } else if (days == 0) {
        result = str_format(arena, "%.*s%" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%03" PRIu64,
                SF(sign_str), hours, minutes, seconds, milliseconds);
    } else {
        result = str_format(arena, "%.*s%" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%03" PRIu64,
                SF(sign_str), days, hours, minutes, seconds, milliseconds);
    }

    return arr_slice(result, 0, result.count - (3 - ms_digits));
}

enum class TimerAction {
    Split,
    UndoSplit,
    DeleteSplit,
    ResetAndSave,
    ResetAndDelete,
    Pause,
};

void timer_reset(TimerState *timer) {
    timer->mode = TimerMode::Init;
    vec_reset(&timer->live_splits);
}

Duration timer_get_elapsed(TimerState *timer) {
    Instant now = {};
    if (timer->mode == TimerMode::Paused) {
        now = timer->paused_time;
    } else {
        now = os_get_monotonic_time();
    }
    return (now - timer->start_time) - timer->total_paused_duration;
}

void timer_apply_action(Arena *arena, TimerState *timer, SplitsFile *file, TimerAction action) {
    // Switch statements are annoying... and neovim keeps indenting them wrong :(
    if (timer->mode == TimerMode::Init) {
        if (action == TimerAction::Split) {
            timer->mode = TimerMode::Running;
            timer->start_time = os_get_monotonic_time();
        }

    } else if (timer->mode == TimerMode::Running) {
        if (action == TimerAction::Split) {
            Duration elapsed = timer_get_elapsed(timer);
            vec_push(arena, &timer->live_splits, some(elapsed));

            if (timer->live_splits.count == file->segments.count) {
                timer->mode = TimerMode::Finished;
            }

        } else if (action == TimerAction::UndoSplit) {
            if (timer->live_splits.count == 0) {
                timer_reset(timer);
            } else {
                vec_pop(&timer->live_splits);
            }

        } else if (action == TimerAction::DeleteSplit) {
            if (timer->live_splits.count > 0) {
                timer->live_splits[timer->live_splits.count - 1] = {};
            }

        } else if (action == TimerAction::ResetAndSave) {
            file->total_attempts++;
            // TODO save golds and rest of file (?)
            timer_reset(timer);

        } else if (action == TimerAction::ResetAndDelete) {
            timer_reset(timer);

        } else if (action == TimerAction::Pause) {
            timer->mode = TimerMode::Paused;
            timer->paused_time = os_get_monotonic_time();
        }

    } else if (timer->mode == TimerMode::Paused) {
        if (action == TimerAction::Pause) {
            // Unpause
            Duration pause_duration = os_get_monotonic_time() - timer->paused_time;
            timer->total_paused_duration += pause_duration;
            timer->mode = TimerMode::Running;
        }

        else if (action == TimerAction::ResetAndSave) {
            file->total_attempts++;
            // save golds?
            timer_reset(timer);

        } else if (action == TimerAction::ResetAndDelete) {
            timer_reset(timer);
        }

    } else if (timer->mode == TimerMode::Finished) {
        if (action == TimerAction::UndoSplit) {
            vec_pop(&timer->live_splits);
            timer->mode = TimerMode::Running;

        } else if (action == TimerAction::ResetAndSave) {
            file->total_attempts++;
            file->completed_attempts++;
            // save golds?
            // save personal best?
            timer_reset(timer);

        } else if (action == TimerAction::ResetAndDelete) {
            timer_reset(timer);
        }
    }

    assert(false);
}

OSResult parse_livesplit_lss(Arena *arena, Str lss_path, SplitsFile *out) {
    Arena *scratch = arena_acquire();
    defer(arena_release(scratch));

    Str lss = {};
    OSResult result = os_read_file(scratch, lss_path, &lss);
    if (result != OSResult::Ok) return result;

    XmlReader reader = xml_reader(lss);
    u64 i = 0;
    while (i < 20 && str_is_empty(reader.error)) {
        XmlValue value = xml__read(&reader);
        Str content = { .value = value.start, .count = (u64)(value.end - value.start) };
        log_debug("XML value: type = %d, content = '%.*s', depth = %d",
                (i32)value.type, SF(content), value.depth);
        i++;
    }
    log_debug("XML error: %.*s", SF(reader.error));

    return OSResult::Ok;
}

int main(int argc, char **argv, char **envp) {
    g_envp = arr_from_null_terminated(envp);
    arena_pool_init();

    Arena *scratch = arena_acquire();
    defer(arena_release(scratch));

    SplitsFile *splits = arena_push<SplitsFile>(scratch);
    Str path = S("silksong-blank.lss");
    OSResult result = parse_livesplit_lss(scratch, path, splits);
    if (result != OSResult::Ok) {
        log_fatal("Failed to parse LSS '%.*s'", SF(path));
    }

    return 0;
}
