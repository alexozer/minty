#include "base.hpp"
#include "platform.hpp"

#include <stdio.h>
#include <assert.h>

// World's crappiest optional type
template <typename T>
struct Opt {
    bool present;
    T value;
};

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

struct DurationState {
    Arr<Opt<Duration>> splits;

    // Current records as they appear in splits file
    Opt<Arr<Opt<Duration>>> personal_best;
    Arr<Opt<Duration>> golds;
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

Arr<SegSummary> calc_seg_summary(Arena *arena, DurationState *timer) {
    Arr<SegSummary> summary = arena_push_arr<SegSummary>(arena, timer->splits.count);

    // Calc PB splits
    if (timer->personal_best.present) {
        Arr<Opt<Duration>> pb_splits = timer->personal_best.value;
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
        summary[i].live_split = timer->splits[i];
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
    for (u64 i = 0; i < timer->splits.count; i++) {
        Opt<Duration> prev_gold = timer->golds[i];
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

int main(int argc, char **argv, char **envp) {
    g_envp = arr_from_null_terminated(envp);
    arena_pool_init();

    Arena *scratch = arena_acquire();
    defer(arena_release(scratch));

    log_info("Hello world! pi = %.2f", 3.14159);
    Str my_str = S("okay then");
    log_warn("pi = %.2f, uh oh! Here's a string for you: '%.*s'", 3.14159f, SF(my_str));

    Instant start = os_get_monotonic_time();
    Instant end = os_get_monotonic_time();
    Duration duration = end - start;
    Str time_str = format_duration(scratch, duration, 2, true);
    log_debug("Raw OS monotonic duration: %.*s", SF(time_str));

    return 0;
}
