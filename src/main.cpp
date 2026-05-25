#include "base.hpp"

#include <stdio.h>

// Nanosconds. Signed so that we can use the same type for diffs.
using Duration = i64;

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

struct TimerState {
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

    // Time gained or lost this split relative to PB
    Opt<Duration> gained;

    Opt<Duration> pb_split;
    Opt<Duration> pb_seg;

    bool is_new_gold;
};

Arr<SegSummary> calc_seg_summary(Arena *arena, TimerState *timer) {
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
        summary[i].is_new_gold = prev_gold.present && live_seg.present && live_seg.value < prev_gold.value;
    }

    return summary;
}

int main(int argc, char **argv, char **envp) {
    g_envp = arr_from_null_terminated(envp);
    arena_pool_init();

    return 0;
}
