//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn void timer_apply_action(Arena *arena, Session *session, TimerAction action, Instant t);
fn void timer_apply_action_init(Arena *arena, Session *session, TimerAction action, Instant t);
fn void timer_apply_action_running(Arena *arena, Session *session, TimerAction action, Instant t);
fn void timer_apply_action_paused(Arena *arena, Session *session, TimerAction action, Instant t);
fn void timer_apply_action_finished(Arena *arena, Session *session, TimerAction action, Instant t);
fn void timer_reset(Timer *timer);
fn Duration timer_get_elapsed(Timer *timer, Instant event_time);
