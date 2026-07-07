//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

void timer_apply_action(Arena *arena, Session *session, TimerAction action, Instant t);
void timer_apply_action_init(Arena *arena, Session *session, TimerAction action, Instant t);
void timer_apply_action_running(Arena *arena, Session *session, TimerAction action, Instant t);
void timer_apply_action_paused(Arena *arena, Session *session, TimerAction action, Instant t);
void timer_apply_action_finished(Arena *arena, Session *session, TimerAction action, Instant t);
void timer_reset(Timer *timer);
Opt_Duration dur_sub(Opt_Duration d1, Opt_Duration d2);
Arr_SegSummary calc_seg_summary(Arena *arena, Session *session);
Str format_duration(Arena *arena, Duration duration, u32 ms_digits, bool show_plus_prefix);
Duration timer_get_elapsed(Timer *timer, Instant event_time);
