//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

UI_Box *build_timer_ui(Arena *arena, Session *session, SizePX size);
void build_segment_ui(Arena *arena, UI_Box *parent, Session *session, u64 segment_idx);
void build_padding(Arena *arena, UI_Style *s, UI_Box *parent, f32 pad_px);
