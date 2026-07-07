#include "timer_update.h"

fn void timer_apply_action(Arena *arena, Session *session, TimerAction action, Instant t) {
    switch (session->timer.mode) {
    case TimerMode_Init: {
        timer_apply_action_init(arena, session, action, t);
        break;
    }
    case TimerMode_Running: {
        timer_apply_action_running(arena, session, action, t);
        break;
    }
    case TimerMode_Paused: {
        timer_apply_action_paused(arena, session, action, t);
        break;
    }
    case TimerMode_Finished: {
        timer_apply_action_finished(arena, session, action, t);
        break;
    }
    }
}

fn void timer_apply_action_init(Arena *arena, Session *session, TimerAction action, Instant t) {
    switch (action) {
    case TimerAction_Split: {
        session->timer.mode = TimerMode_Running;
        session->timer.start_time = t;
        break;
    }
    default: {
    }
    }
}

fn void timer_apply_action_running(Arena *arena, Session *session, TimerAction action, Instant t) {
    Timer *timer = &session->timer;
    FileDef *file = &session->file;

    switch (action) {
    case TimerAction_Split: {
        Duration elapsed = timer_get_elapsed(timer, t);
        vec_push(arena, &timer->live_splits, some(elapsed, Duration));

        if (timer->live_splits.count == file->segments.count) {
            timer->mode = TimerMode_Finished;
        }
        break;
    }
    case TimerAction_UndoSplit: {
        if (timer->live_splits.count == 0) {
            timer_reset(timer);
        } else {
            vec_pop(&timer->live_splits);
        }
        break;
    }
    case TimerAction_DeleteSplit: {
        if (timer->live_splits.count > 0) {
            A(timer->live_splits, timer->live_splits.count - 1) = (Opt_Duration){};
        }
        break;
    }
    case TimerAction_ResetAndSave: {
        file->total_attempts++;
        // TODO save golds and rest of file (?)
        timer_reset(timer);
        break;
    }
    case TimerAction_ResetAndDelete: {
        timer_reset(timer);
        break;
    }
    case TimerAction_Pause: {
        timer->mode = TimerMode_Paused;
        timer->paused_time = t;
        break;
    }
    }
}

fn void timer_apply_action_paused(Arena *arena, Session *session, TimerAction action, Instant t) {
    Timer *timer = &session->timer;
    FileDef *file = &session->file;

    switch (action) {
    case TimerAction_Pause: {
        // Unpause
        Duration pause_duration = instant_sub(t, timer->paused_time);
        timer->total_paused_duration += pause_duration;
        timer->mode = TimerMode_Running;
        break;
    }
    case TimerAction_ResetAndSave: {
        file->total_attempts++;
        // TODO: Save golds?
        timer_reset(timer);
        break;
    }
    case TimerAction_ResetAndDelete: {
        timer_reset(timer);
        break;
    }
    default: {
    }
    }
}

fn void timer_apply_action_finished(Arena *arena, Session *session, TimerAction action, Instant t) {
    Timer *timer = &session->timer;
    FileDef *file = &session->file;

    switch (action) {
    case TimerAction_UndoSplit: {
        vec_pop(&timer->live_splits);
        timer->mode = TimerMode_Running;
        break;
    }
    case TimerAction_ResetAndSave: {
        file->total_attempts++;
        file->completed_attempts++;
        // save golds?
        // save personal best?
        timer_reset(timer);
        break;
    }
    case TimerAction_ResetAndDelete: {
        timer_reset(timer);
        break;
    }
    default: {
    }
    }
}

fn void timer_reset(Timer *timer) {
    timer->mode = TimerMode_Init;
    vec_reset(&timer->live_splits);
}

fn Opt_Duration dur_sub(Opt_Duration d1, Opt_Duration d2) {
    return (Opt_Duration){
        .opt = d1.opt - d2.opt,
        .present = d1.present && d2.present,
    };
}

fn Arr_SegSummary calc_seg_summary(Arena *arena, Session *session) {
    Timer *timer = &session->timer;
    FileDef *file = &session->file;

    Arr_SegSummary summary = arena_push_arr(arena, SegSummary, timer->live_splits.count);

    // Calc PB splits
    Arr_Opt_Duration pb_splits = file->personal_best.splits;
    for (u64 i = 0; i < pb_splits.count; i++) {
        A(summary, i).pb_split = A(pb_splits, i);
    }
    for (u64 i = 0; i < pb_splits.count; i++) {
        if (i == 0) {
            A(summary, i).pb_seg = A(summary, i).pb_split;
        } else {
            Opt_Duration curr = A(summary, i).pb_split;
            Opt_Duration prev = A(summary, i - 1).pb_split;
            A(summary, i).pb_seg = dur_sub(curr, prev);
        }
    }

    // Calc live splits
    for (u64 i = 0; i < summary.count; i++) {
        A(summary, i).live_split = A(timer->live_splits, i);
    }
    for (u64 i = 0; i < summary.count; i++) {
        if (i == 0) {
            A(summary, i).live_seg = A(summary, i).live_split;
        } else {
            Opt_Duration curr = A(summary, i).live_split;
            Opt_Duration prev = A(summary, i - 1).live_split;
            A(summary, i).live_seg = dur_sub(curr, prev);
        }
    }

    // Calc live deltas
    for (u64 i = 0; i < summary.count; i++) {
        Opt_Duration curr = A(summary, i).live_split;
        Opt_Duration prev = A(summary, i - 1).live_split;
        A(summary, i).live_delta = dur_sub(curr, prev);
    }
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
        Opt_Duration prev_gold = A(file->golds, i);
        Opt_Duration live_seg = A(summary, i).live_seg;
        A(summary, i).is_new_gold =
            prev_gold.present && live_seg.present && live_seg.opt < prev_gold.opt;
    }

    return summary;
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

fn Duration timer_get_elapsed(Timer *timer, Instant event_time) {
    switch (timer->mode) {
    case TimerMode_Init: {
        return 0;
    }
    case TimerMode_Finished: {
        return A(timer->live_splits, timer->live_splits.count - 1).opt;
    }
    case TimerMode_Running: {
        return instant_sub(event_time, timer->start_time) - timer->total_paused_duration;
    }
    case TimerMode_Paused: {
        Duration pause_duration = instant_sub(timer->paused_time, timer->start_time);
        return pause_duration - timer->total_paused_duration;
    }
    }
}
