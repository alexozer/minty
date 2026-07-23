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
            arr_last(timer->live_splits) = (Opt_Duration){};
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

fn Duration timer_get_elapsed(Timer *timer, Instant event_time) {
    switch (timer->mode) {
    case TimerMode_Init: {
        return 0;
    }
    case TimerMode_Finished: {
        return arr_last(timer->live_splits).opt;
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
