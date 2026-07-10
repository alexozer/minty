//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

UI_Box *build_timer_ui(Arena *arena, Session *session, SizePX size);
void build_ui_segments(Arena *arena, UI_Style *s, UI_Box *parent, Session *session);
void build_segment_ui(Arena *arena,
                      UI_Style *s,
                      UI_Box *parent,
                      Session *session,
                      u64 segment_idx);
