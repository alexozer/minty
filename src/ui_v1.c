#include "ui_v1.h"

fn Box *make_text_box(Arena *arena, Str content, Color color) {
    Box *box = arena_push(arena, Box);

    box->type = BoxType_Text;
    // const char *content_cstr = is_empty(content) ? "" : (const char
    // *)content.ptr; Zero length actually means "treat string as null terminated"
    // box->text_obj = TTF_CreateText(engine, font, content_cstr, content.count);
    // TTF_SetTextColorFloat(box->text_obj, color.r, color.g, color.b, color.a);

    return box;
}

fn Box *make_empty_box(Arena *arena, SizePX size) {
    Box *box = arena_push(arena, Box);
    box->type = BoxType_Empty;
    box->bbox = some(size, SizePX);
    return box;
}

fn Box *make_texture_box(Arena *arena, u64 texture_idx, SizePX size) {
    Box *box = arena_push(arena, Box);
    box->type = BoxType_Texture;
    box->bbox = some(size, SizePX);
    box->texture_idx = texture_idx;
    return box;
}

fn Box *make_solid_color_box(Arena *arena, Color color, SizePX size) {
    Box *box = arena_push(arena, Box);
    box->type = BoxType_SolidColor;
    box->bbox = some(size, SizePX);
    box->color = color;
    return box;
}

fn SizePX compute_box_bbox_uncached(Box *box) {
    switch (box->type) {
    case BoxType_Empty:
    case BoxType_SolidColor: {
        return box->bbox.opt;
    }
    case BoxType_LeftToRightStack: {
        SizePX total = {};
        for (u64 i = 0; i < box->children.count; i++) {
            SizePX child_bbox = compute_box_bbox(A(box->children, i));
            if (i == 0) {
                total = child_bbox;
            } else {
                total.w += child_bbox.w;
                total.h = max(total.h, child_bbox.h);
            }
        }
        return total;
    }
    case BoxType_TopToBottomStack: {
        SizePX total = {};
        for (u64 i = 0; i < box->children.count; i++) {
            SizePX child_bbox = compute_box_bbox(A(box->children, i));
            if (i == 0) {
                total = child_bbox;
            } else {
                total.w = max(total.w, child_bbox.w);
                total.h += child_bbox.h;
            }
        }
        return total;
    }
    case BoxType_BackToFrontStack: {
        SizePX total = {};
        for (u64 i = 0; i < box->children.count; i++) {
            SizePX child_bbox = compute_box_bbox(A(box->children, i));
            if (i == 0) {
                total = child_bbox;
            } else {
                total.w = max(total.w, child_bbox.w);
                total.h = max(total.h, child_bbox.h);
            }
        }
        return total;
    }
    case BoxType_Text: {
        return (SizePX){0, 0};
    }
    case BoxType_Texture: {
        return box->bbox.opt;
    }
    }
}

fn SizePX compute_box_bbox(Box *box) {
    if (box->bbox.present) {
        return box->bbox.opt;
    }
    SizePX bbox = compute_box_bbox_uncached(box);
    box->bbox = some(bbox, SizePX);
    return bbox;
}

fn Box *pad_box_left(Arena *arena, Box *box, u16 pad) {
    SizePX bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, (SizePX){pad, bbox.h});

    Box *parent_box = arena_push(arena, Box);
    parent_box->type = BoxType_LeftToRightStack;
    vec_push(arena, &parent_box->children, pad_box);
    vec_push(arena, &parent_box->children, box);

    return parent_box;
}

fn Box *pad_box_right(Arena *arena, Box *box, u16 pad) {
    SizePX bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, (SizePX){pad, bbox.h});

    Box *parent_box = arena_push(arena, Box);
    parent_box->type = BoxType_LeftToRightStack;
    vec_push(arena, &parent_box->children, box);
    vec_push(arena, &parent_box->children, pad_box);

    return parent_box;
}

fn Box *pad_box_top(Arena *arena, Box *box, u16 pad) {
    SizePX bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, (SizePX){bbox.w, pad});

    Box *parent_box = arena_push(arena, Box);
    parent_box->type = BoxType_TopToBottomStack;
    vec_push(arena, &parent_box->children, pad_box);
    vec_push(arena, &parent_box->children, box);

    return parent_box;
}

fn Box *pad_box_bottom(Arena *arena, Box *box, u16 pad) {
    SizePX bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, (SizePX){bbox.w, pad});

    Box *parent_box = arena_push(arena, Box);
    parent_box->type = BoxType_TopToBottomStack;
    vec_push(arena, &parent_box->children, box);
    vec_push(arena, &parent_box->children, pad_box);

    return parent_box;
}

fn Box *align_box_center_horiz(Arena *arena, Box *box, u16 width) {
    SizePX bbox = compute_box_bbox(box);
    width = max(width, bbox.w);
    u16 left_pad = (width - bbox.w) / 2;
    u16 right_pad = width - bbox.w - left_pad;

    Box *left_pad_box = make_empty_box(arena, (SizePX){left_pad, bbox.h});
    Box *right_pad_box = make_empty_box(arena, (SizePX){right_pad, bbox.h});

    Box *parent = arena_push(arena, Box);
    parent->type = BoxType_LeftToRightStack;
    vec_push(arena, &parent->children, left_pad_box);
    vec_push(arena, &parent->children, box);
    vec_push(arena, &parent->children, right_pad_box);

    return parent;
}

fn Box *align_box_center_vert(Arena *arena, Box *box, u16 height) {
    SizePX bbox = compute_box_bbox(box);
    height = max(height, bbox.h);
    u16 top_pad = (height - bbox.h) / 2;
    u16 bottom_pad = height - bbox.h - top_pad;

    Box *top_pad_box = make_empty_box(arena, (SizePX){bbox.w, top_pad});
    Box *bottom_pad_box = make_empty_box(arena, (SizePX){bbox.w, bottom_pad});

    Box *parent = arena_push(arena, Box);
    parent->type = BoxType_TopToBottomStack;
    vec_push(arena, &parent->children, top_pad_box);
    vec_push(arena, &parent->children, box);
    vec_push(arena, &parent->children, bottom_pad_box);

    return parent;
}

fn Box *prerender_segment(Arena *arena, Session *session, u16 width, u64 idx) {
    constexpr u16 ICON_INNER_PX = 80;
    constexpr u16 ICON_OUTER_PX = 90;
    Box *icon = nullptr;

    // TODO handle empty icons
    icon = make_texture_box(arena, idx, (SizePX){ICON_INNER_PX, ICON_INNER_PX});
    // } else {
    //     icon = make_empty_box(arena, ICON_INNER, ICON_INNER);
    // }
    icon = align_box_center_horiz(arena, icon, ICON_OUTER_PX);
    icon = align_box_center_vert(arena, icon, ICON_OUTER_PX);

    Color text_color = {.r = 255, .g = 255, .b = 255, .a = 255};
    Box *pad = make_empty_box(arena, (SizePX){.w = 10, .h = 0});
    Box *title = make_text_box(arena, A(session->file.segments, idx).name, text_color);
    Box *title_centered = align_box_center_vert(arena, title, ICON_OUTER_PX);

    Box *row_front = arena_push(arena, Box);
    row_front->type = BoxType_LeftToRightStack;
    vec_push(arena, &row_front->children, icon);
    vec_push(arena, &row_front->children, pad);
    vec_push(arena, &row_front->children, title_centered);

    if (session->timer.mode == TimerMode_Running && idx == session->timer.live_splits.count) {
        SizePX row_front_bbox = compute_box_bbox(row_front);
        Color bg_color = {.r = 0, .g = 0, .b = 0, .a = 255};
        SizePX row_back_size = {.w = width, .h = row_front_bbox.h};
        Box *row_back = make_solid_color_box(arena, bg_color, row_back_size);

        Box *row = arena_push(arena, Box);
        row->type = BoxType_BackToFrontStack;
        vec_push(arena, &row->children, row_back);
        vec_push(arena, &row->children, row_front);

        return row;
    }

    return row_front;
}

fn Box *prerender_contents(Arena *arena, Session *session, SizePX size) {
    Color color = {.r = 255, .g = 255, .b = 255, .a = 255};

    Box *game_name = make_text_box(arena, session->file.game_name, color);
    Box *cat_name = make_text_box(arena, session->file.category_name, color);

    Box *game_name_centered = align_box_center_horiz(arena, game_name, size.w);
    Box *cat_name_centered = align_box_center_horiz(arena, cat_name, size.w);

    Box *top = arena_push(arena, Box);
    top->type = BoxType_TopToBottomStack;
    vec_push(arena, &top->children, game_name_centered);
    vec_push(arena, &top->children, cat_name_centered);

    for (u64 i = 0; i < session->file.segments.count; i++) {
        Box *segment = prerender_segment(arena, session, size.w, i);
        vec_push(arena, &top->children, segment);
    }

    Box *bottom = arena_push(arena, Box);
    bottom->type = BoxType_TopToBottomStack;

    Duration t = timer_get_elapsed(&session->timer, get_current_monotonic_time());
    Str t_str = format_duration(arena, t, 2, false);
    Box *curr_time = make_text_box(arena, t_str, color);
    SizePX curr_time_bbox = compute_box_bbox(curr_time);
    Box *curr_time_aligned = pad_box_left(arena, curr_time, size.w - curr_time_bbox.w);

    vec_push(arena, &bottom->children, curr_time_aligned);

    // Put timer at bottom
    SizePX top_bbox = compute_box_bbox(top);
    SizePX bottom_bbox = compute_box_bbox(bottom);
    SizePX vsep_size = {.w = 0, .h = (u16)(size.h - top_bbox.h - bottom_bbox.h)};
    Box *vsep = make_empty_box(arena, vsep_size);

    Box *root = arena_push(arena, Box);
    root->type = BoxType_TopToBottomStack;
    vec_push(arena, &root->children, top);
    vec_push(arena, &root->children, vsep);
    vec_push(arena, &root->children, bottom);

    return root;
}

fn Box *prerender(Arena *arena, Session *session, SizePX window_size) {
    constexpr u16 PADDING = 10;
    SizePX content_size = {
        .w = (u16)(window_size.w - PADDING * 2),
        .h = (u16)(window_size.h - PADDING * 2),
    };
    Box *timer = prerender_contents(arena, session, content_size);
    timer = pad_box_left(arena, timer, PADDING);
    timer = pad_box_right(arena, timer, PADDING);
    timer = pad_box_top(arena, timer, PADDING);
    timer = pad_box_bottom(arena, timer, PADDING);
    return timer;
}

fn void window_to_ndc(Vertex *vertex, SizePX window_size) {
    vertex->x = (vertex->x / (f32)window_size.w) * 2.f - 1.f;
    vertex->y = -((vertex->y / (f32)window_size.h) * 2.f - 1.f);
}

// void make_icon_mesh_inner(SizePX window_size,
//                           Box *box,
//                           PosPX where,
//                           Atlas *atlas,
//                           MeshBuilder *mesh) {
//     switch (box->type) {
//     case BoxType_Empty: {
//         break;
//     }
//     case BoxType_LeftToRightStack: {
//         for (u64 i = 0; i < box->children.count; i++) {
//             SizePX child_bbox = compute_box_bbox(A(box->children, i));
//             make_icon_mesh_inner(window_size, A(box->children, i), where, atlas, mesh);
//             where.x += child_bbox.w;
//         }
//         break;
//     }
//     case BoxType_TopToBottomStack: {
//         for (u64 i = 0; i < box->children.count; i++) {
//             SizePX child_bbox = compute_box_bbox(A(box->children, i));
//             make_icon_mesh_inner(window_size, A(box->children, i), where, atlas, mesh);
//             where.y += child_bbox.h;
//         }
//         break;
//     }
//     case BoxType_BackToFrontStack: {
//         for (u64 i = 0; i < box->children.count; i++) {
//             make_icon_mesh_inner(window_size, A(box->children, i), where, atlas, mesh);
//         }
//         break;
//     }
//     case BoxType_Text: {
//         // TTF_DrawRendererText(box->text_obj, where.x, where.y);
//         break;
//     }
//     case BoxType_Texture: {
//         RectPX src = A(atlas->placements, box->texture_idx);
//
//         f32 src_ratio = (f32)src.w / (f32)src.h;
//         f32 dst_ratio = (f32)box->bbox.opt.w / (f32)box->bbox.opt.h;
//
//         // Scale to fit
//         RectPX dest = {};
//         if (src_ratio > dst_ratio) {
//             f32 scale = (f32)box->bbox.opt.w / (f32)src.w;
//             dest.w = box->bbox.opt.w;
//             dest.h = (u16)SDL_lroundf((f32)src.h * scale);
//             dest.x = where.x;
//             dest.y = where.y + (u16)SDL_lroundf((f32)(box->bbox.opt.h - dest.h) / 2.f);
//         } else {
//             f32 scale = (f32)box->bbox.opt.h / (f32)src.h;
//             dest.w = (u16)SDL_lroundf((f32)src.w * scale);
//             dest.h = box->bbox.opt.h;
//             dest.x = where.x + (u16)SDL_lroundf((f32)(box->bbox.opt.w - dest.w) / 2.f);
//             dest.y = where.y;
//         }
//         Color color = {.r = 255, .g = 255, .b = 255, .a = 255};
//         push_atlas_quad(window_size, atlas, mesh, src, dest, color);
//         break;
//     }
//     case BoxType_SolidColor: {
//         // SDL_SetRenderDrawColorFloat(app->renderer, box->color.r, box->color.g,
//         // box->color.b, box->color.a); SDL_FRect r = { .x = where.x, .y =
//         // where.y, .w = box->width, .h = box->height };
//         // SDL_RenderFillRect(app->renderer, &r);
//         break;
//     }
//     }
// }
//
// u64 make_icon_mesh(SizePX window_size, Session *session, Atlas *atlas, MeshBuilder *mesh) {
//     Arena *scratch = arena_acquire();
//
//     Box *box = prerender(scratch, session, window_size);
//     u64 start_vertex_count = mesh->vertices.count;
//     PosPX where = {0, 0};
//     make_icon_mesh_inner(window_size, box, where, atlas, mesh);
//     u64 end_vertex_count = mesh->vertices.count;
//
//     arena_release(scratch);
//     return (end_vertex_count - start_vertex_count) / 4;
// }
