//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn Opt_Duration dur_add(Opt_Duration d1, Opt_Duration d2);
fn Opt_Duration dur_sub(Opt_Duration d1, Opt_Duration d2);
fn Opt_Duration dur_max(Opt_Duration d1, Opt_Duration d2);
fn Arr_SegSummary calc_seg_summary(Arena *arena, Session *session);
fn Opt_Duration calc_best_possible_time(Session *session, Arr_SegSummary summaries);
fn Opt_Duration calc_sum_of_best_segments(Arr_SegSummary summaries);
fn Str format_opt_duration(Arena *arena,
                           Opt_Duration duration,
                           u32 ms_digits,
                           bool show_plus_prefix);
fn Str format_duration(Arena *arena, Duration duration, u32 ms_digits, bool show_plus_prefix);
