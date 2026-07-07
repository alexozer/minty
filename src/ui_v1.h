//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

Box *make_text_box(Arena *arena, Str content, Color color);
Box *make_empty_box(Arena *arena, SizePX size);
Box *make_texture_box(Arena *arena, u64 texture_idx, SizePX size);
Box *make_solid_color_box(Arena *arena, Color color, SizePX size);
SizePX compute_box_bbox_uncached(Box *box);
SizePX compute_box_bbox(Box *box);
Box *pad_box_left(Arena *arena, Box *box, u16 pad);
Box *pad_box_right(Arena *arena, Box *box, u16 pad);
Box *pad_box_top(Arena *arena, Box *box, u16 pad);
Box *pad_box_bottom(Arena *arena, Box *box, u16 pad);
Box *align_box_center_horiz(Arena *arena, Box *box, u16 width);
Box *align_box_center_vert(Arena *arena, Box *box, u16 height);
Box *prerender_segment(Arena *arena, Session *session, u16 width, u64 idx);
Box *prerender_contents(Arena *arena, Session *session, SizePX size);
Box *prerender(Arena *arena, Session *session, SizePX window_size);
void window_to_ndc(Vertex *vertex, SizePX window_size);
