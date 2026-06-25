#include "base.hpp"
#include "platform.hpp"

#include <stdarg.h>

#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <ft2build.h>
#include FT_FREETYPE_H

extern "C" {
#include <xao.h>
}
#include <kb_text_shape.h>
#include <stb_rect_pack.h>
#include <yyjson.h>

//
// MARK:Constants
//

// TODO toggle through build system or something
constexpr bool RENDERER_DEBUG_MODE_ENABLED = true;

// TODO define these in a more principled way
constexpr u64 MAX_QUAD_COUNT = 512;
constexpr u64 MAX_VERTEX_COUNT = MAX_QUAD_COUNT * 4;
constexpr u64 MAX_INDEX_COUNT = MAX_QUAD_COUNT * 6;

struct PxSize {
    u16 w, h;
};

constexpr PxSize DEFAULT_WINDOW_SIZE = {.w = 360, .h = 600};
constexpr PxSize MIN_WINDOW_SIZE = {.w = 200, .h = 100};

// TODO thread through program properly
Str FONT_PATH = S("data/Roboto-Medium.ttf");
// Str FONT_PATH = S("data/NotoSans-Regular.ttf");
// Str FONT_PATH = S("data/NotoSans-Bold.ttf");
// Str FONT_PATH = S("data/KosugiMaru-Regular.otf");
constexpr u32 FONT_SIZE_PX = 40;

//
// MARK:Types
//

struct CPUTexture {
    SDL_GPUTextureFormat format;
    Arr<u8> buffer;
    PxSize dims;
};

struct SplitRecord {
    u64 attempt_num;
    Arr<Opt<Duration>> splits;
};

struct SegmentDef {
    Str name;
    Arr<u8> icon_png;  // Icon in PNG format
    CPUTexture icon_texture;
};

struct FileDef {
    Str game_name;
    Str category_name;
    u64 total_attempts;
    u64 completed_attempts;
    Arr<SegmentDef> segments;
    SplitRecord personal_best;
    Arr<Opt<Duration>> golds;
};

enum class TimerMode {
    Init,
    Running,
    Paused,
    Finished,
};

struct Timer {
    TimerMode mode;
    Vec<Opt<Duration>> live_splits;
    Instant start_time;
    Instant paused_time;
    Duration total_paused_duration;
};

struct Session {
    FileDef file;
    Timer timer;
};

struct SegSummary {
    Opt<Duration> live_split;
    Opt<Duration> live_seg;

    // How far ahead/behind this split is compared to PB
    Opt<Duration> live_delta;

    // Duration gained or lost this split relative to PB
    Opt<Duration> gained;

    Opt<Duration> pb_split;
    Opt<Duration> pb_seg;

    bool is_new_gold;
};

enum class TimerAction {
    Split,
    UndoSplit,
    DeleteSplit,
    ResetAndSave,
    ResetAndDelete,
    Pause,
};

enum class ShaderType {
    Vertex,
    Fragment,
};

// Icons don't need color, but it's simpler to just have one format for now
struct Vertex {
    f32 x, y, z;
    f32 u, v;
    u8 r, g, b, a;
};

struct Color {
    u8 r, g, b, a;
};

struct PxPos {
    u16 x, y;
};

struct PxRect {
    u16 x, y;
    u16 w, h;
};

struct Atlas {
    PxSize size;
    SDL_GPUTexture* texture;
    SDL_GPUSampler* sampler;
    Arr<PxRect> placements;
    // Not sure if it's ever worth coalescing transfer buffers
    SDL_GPUTransferBuffer* transfer_buffer;

    // Rect packer state, useful for online repacking
    Arr<stbrp_node> packer_nodes;
    Arr<stbrp_rect> packer_rects;
};

struct GlyphMetrics {
    f32 bearing_px_x;  // Distance from left start to glyph start
    f32 bearing_px_y;  // Distance from baseline to top of glyph
};

struct GlyphAtlas {
    Atlas* atlas;
    u16 px_per_em;  // AKA the face size in pixels
    u16 units_per_em;
    Arr<GlyphMetrics> metrics;
};

struct Mesh {
    Vec<Vertex> vertices;
    Vec<u16> indices;
};

struct ShapedGlyph {
    u32 glyph_id;
    i32 glyph_x_fu;  // fu = font unit
    i32 glyph_y_fu;
};

enum class BlendType {
    None,
    Over,
};

enum class TextureFilterType {
    Nearest,
    Linear,
};

enum class BoxType {
    Empty,
    Text,
    Texture,
    SolidColor,
    TopToBottomStack,
    LeftToRightStack,
    BackToFrontStack,
};

// Try some fat struct stuff?
struct Box {
    BoxType type;
    Opt<PxSize> bbox;
    u64 texture_idx;
    Color color;
    Vec<Box*> children;
};

struct App {
    Arena* app_arena;  // Lives for duration of application
    SDL_Window* window;

    SDL_Keycode prev_keys;
    // TODO: float-based scrolling on NDC could mess with pixel-perfect alignment
    f32 scroll;
    f32 scale;  // `scale + 1.0f` is actual scale
    bool insert_mode_enabled;
    Vec<u8> typed_text;

    Arena* session_arena;
    Session* session;  // Nullable

    SDL_GPUDevice* device;

    // Shaders
    SDL_GPUShader* vert_shader;
    SDL_GPUShader* icon_frag_shader;
    SDL_GPUShader* glyph_frag_shader;

    // Pipelines
    SDL_GPUGraphicsPipeline* icon_pipeline;
    SDL_GPUGraphicsPipeline* glyph_pipeline;
    SDL_GPUGraphicsPipeline* clear_icon_pipeline;
    SDL_GPUGraphicsPipeline* clear_glyph_pipeline;

    // Geometry buffers
    SDL_GPUTransferBuffer* vertex_transfer_buffer;
    SDL_GPUBuffer* vertex_buffer;
    SDL_GPUBuffer* index_buffer;

    // Textures
    Atlas* icon_atlas;
    GlyphAtlas glyph_atlas;

    // Text stuff
    FT_Library freetype;
    Arr<u8> font_file;
};

//
// MARK:Timer
//

CPUTexture convert_srgb_surface_to_rgba(Arena* arena, SDL_Surface* surface) {
    u64 dest_size = (u64)(surface->w * surface->h * 4);
    Arr<u8> buffer = {
        // TODO don't use "private" arena API for alignment
        .ptr = (u8*)arena__push_bytes<8>(arena, dest_size),
        .count = dest_size,
    };
    CPUTexture texture = {
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
        .buffer = buffer,
        .dims = {.w = (u16)surface->w, .h = (u16)surface->h},
    };
    log_assert(SDL_ConvertPixels(surface->w, surface->h, surface->format, surface->pixels,
                                 surface->pitch, SDL_PIXELFORMAT_RGBA32, texture.buffer.ptr,
                                 surface->w * 4));
    return texture;
}

Opt<Duration> operator+(const Opt<Duration>& d1, const Opt<Duration>& d2) {
    return {
        .present = d1.present && d2.present,
        .opt = d1.opt + d2.opt,
    };
}

Opt<Duration> operator-(const Opt<Duration>& d1, const Opt<Duration>& d2) {
    return {
        .present = d1.present && d2.present,
        .opt = d1.opt - d2.opt,
    };
}

Arr<SegSummary> calc_seg_summary(Arena* arena, Session* session) {
    Timer* timer = &session->timer;
    FileDef* file = &session->file;

    Arr<SegSummary> summary = arena_push_arr<SegSummary>(arena, timer->live_splits.count);

    // Calc PB splits
    Arr<Opt<Duration>> pb_splits = file->personal_best.splits;
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

    // Calc live splits
    for (u64 i = 0; i < summary.count; i++) {
        summary[i].live_split = timer->live_splits[i];
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
    for (u64 i = 0; i < summary.count; i++) {
        Opt<Duration> prev_gold = file->golds[i];
        Opt<Duration> live_seg = summary[i].live_seg;
        summary[i].is_new_gold =
            prev_gold.present && live_seg.present && live_seg.opt < prev_gold.opt;
    }

    return summary;
}

Str format_duration(Arena* arena, Duration duration, u32 ms_digits, bool show_plus_prefix) {
    log_assert(ms_digits <= 3);

    Str sign_str = show_plus_prefix ? S("+") : S("");
    if (duration < DURATION_ZERO) {
        sign_str = S("-");
    }

    constexpr u64 DAY_SECS = 60 * 60 * 24;
    constexpr u64 HOUR_SECS = 60 * 60;
    constexpr u64 MINUTE_SECS = 60;
    constexpr u64 MILLISEC_NSECS = 1'000'000;

    u64 total_seconds = duration_seconds(duration);
    u32 subsec_nanos = duration_subsec_nanos(duration);

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

    return arr_slice(result, 0, result.count - (3 - ms_digits));
}

void timer_reset(Timer* timer) {
    timer->mode = TimerMode::Init;
    vec_reset(&timer->live_splits);
}

Duration timer_get_elapsed(Timer* timer, Instant event_time) {
    switch (timer->mode) {
    case TimerMode::Init: {
        return DURATION_ZERO;
    }
    case TimerMode::Finished: {
        return timer->live_splits[timer->live_splits.count - 1].opt;
    }
    case TimerMode::Running: {
        return (event_time - timer->start_time) - timer->total_paused_duration;
    }
    case TimerMode::Paused: {
        return (timer->paused_time - timer->start_time) - timer->total_paused_duration;
    }
    }
}

void timer_apply_action_init(Arena* arena, Session* session, TimerAction action, Instant t) {
    switch (action) {
    case TimerAction::Split: {
        session->timer.mode = TimerMode::Running;
        session->timer.start_time = t;
        break;
    }
    default: {
    }
    }
}

void timer_apply_action_running(Arena* arena, Session* session, TimerAction action, Instant t) {
    Timer* timer = &session->timer;
    FileDef* file = &session->file;

    switch (action) {
    case TimerAction::Split: {
        Duration elapsed = timer_get_elapsed(timer, t);
        vec_push(arena, &timer->live_splits, some(elapsed));

        if (timer->live_splits.count == file->segments.count) {
            timer->mode = TimerMode::Finished;
        }
        break;
    }
    case TimerAction::UndoSplit: {
        if (timer->live_splits.count == 0) {
            timer_reset(timer);
        } else {
            vec_pop(&timer->live_splits);
        }
        break;
    }
    case TimerAction::DeleteSplit: {
        if (timer->live_splits.count > 0) {
            timer->live_splits[timer->live_splits.count - 1] = {};
        }
        break;
    }
    case TimerAction::ResetAndSave: {
        file->total_attempts++;
        // TODO save golds and rest of file (?)
        timer_reset(timer);
        break;
    }
    case TimerAction::ResetAndDelete: {
        timer_reset(timer);
        break;
    }
    case TimerAction::Pause: {
        timer->mode = TimerMode::Paused;
        timer->paused_time = t;
        break;
    }
    }
}

void timer_apply_action_paused(Arena* arena, Session* session, TimerAction action, Instant t) {
    Timer* timer = &session->timer;
    FileDef* file = &session->file;

    switch (action) {
    case TimerAction::Pause: {
        // Unpause
        Duration pause_duration = t - timer->paused_time;
        timer->total_paused_duration += pause_duration;
        timer->mode = TimerMode::Running;
        break;
    }
    case TimerAction::ResetAndSave: {
        file->total_attempts++;
        // TODO: Save golds?
        timer_reset(timer);
        break;
    }
    case TimerAction::ResetAndDelete: {
        timer_reset(timer);
        break;
    }
    default: {
    }
    }
}

void timer_apply_action_finished(Arena* arena, Session* session, TimerAction action, Instant t) {
    Timer* timer = &session->timer;
    FileDef* file = &session->file;

    switch (action) {
    case TimerAction::UndoSplit: {
        vec_pop(&timer->live_splits);
        timer->mode = TimerMode::Running;
        break;
    }
    case TimerAction::ResetAndSave: {
        file->total_attempts++;
        file->completed_attempts++;
        // save golds?
        // save personal best?
        timer_reset(timer);
        break;
    }
    case TimerAction::ResetAndDelete: {
        timer_reset(timer);
        break;
    }
    default: {
    }
    }
}

void timer_apply_action(Arena* arena, Session* session, TimerAction action, Instant t) {
    switch (session->timer.mode) {
    case TimerMode::Init: {
        timer_apply_action_init(arena, session, action, t);
        break;
    }
    case TimerMode::Running: {
        timer_apply_action_running(arena, session, action, t);
        break;
    }
    case TimerMode::Paused: {
        timer_apply_action_paused(arena, session, action, t);
        break;
    }
    case TimerMode::Finished: {
        timer_apply_action_finished(arena, session, action, t);
        break;
    }
    }
}

bool eq(xao_Value v, const char* s) {
    u64 size = (u64)v.end - (u64)v.start;
    return size == SDL_strlen(s) && SDL_memcmp(v.start, s, size) == 0;
}

Str xml_str(xao_Value v) {
    return {.ptr = (u8*)v.start, .count = (u64)v.end - (u64)v.start};
}

Str xml_inner(xao_Reader* r, xao_Value outer) {
    xao_Value inner = {};
    xao_iter_content(r, outer, &inner);
    return xml_str(inner);
}

// TODO I've been neglectful of `const`ness in my APIs, but obviously this
// should be const
u8 PNG_HEADER[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

Arr<u8> decode_icon_base64_to_png(Arena* arena, ErrorContext* err, Str icon_base64) {
    err_scope(err, "Decode icon base64 to PNG");

    Arr<u8> icon_bin = base64_decode(arena, err, icon_base64);
    Opt<u64> png_idx = str_find(icon_bin, A(PNG_HEADER));
    if (!png_idx.present) {
        bail_v(err, {}, "PNG image not detected");
    }

    return arr_slice(icon_bin, png_idx.opt, icon_bin.count);
}

Arr<SegmentDef> parse_livesplit_segments(Arena* arena,
                                         ErrorContext* err,
                                         xao_Reader* r,
                                         xao_Value segments_tag) {
    err_scope(err, "Parse LiveSplit LSS segments");

    Vec<SegmentDef> segments = {};
    xao_Value seg_tag = {};
    while (xao_iter_tags(r, segments_tag, &seg_tag)) {
        SegmentDef* seg = vec_push_zero(arena, &segments);
        xao_Value attr_tag = {};
        while (xao_iter_tags(r, seg_tag, &attr_tag)) {
            if (eq(attr_tag, "Name")) {
                seg->name = str_clone(arena, xml_inner(r, attr_tag));
                if (str_is_empty(seg->name)) {
                    bail_v(err, {}, "Segment %" PRIu64 " has empty name", segments.count + 1);
                }
            } else if (eq(attr_tag, "Icon")) {
                err_scope(err, "Decode icon for segment '%.*s'", SF(seg->name));
                Str base64 = xml_inner(r, attr_tag);
                if (!str_is_empty(base64)) {
                    seg->icon_png = decode_icon_base64_to_png(arena, err, base64);

                    SDL_IOStream* png_stream =
                        try_sdl(err, {}, SDL_IOFromMem(seg->icon_png.ptr, seg->icon_png.count));
                    defer(SDL_CloseIO(png_stream));

                    SDL_Surface* surface = try_sdl(err, {}, SDL_LoadPNG_IO(png_stream, false));
                    defer(SDL_DestroySurface(surface));

                    seg->icon_texture = convert_srgb_surface_to_rgba(arena, surface);
                }
            }
        }
    }
    return vec_arr(&segments);
}

void parse_livesplit_lss(ErrorContext* err, Arena* arena, FileDef* file, Arr<u8> xml) {
    err_scope(err, "Parse LiveSplit LSS");

    if (!str_is_valid_utf8(xml)) {
        bail(err, "Invalid UTF-8");
    }

    xao_Reader r = xao_reader((char*)xml.ptr, xml.count);
    xao_Value root = {};
    xao_Value root_tag = {};
    while (xao_iter_tags(&r, root, &root_tag)) {
        if (eq(root_tag, "Run")) {
            xao_Value run_tag = {};
            while (xao_iter_tags(&r, root_tag, &run_tag)) {
                if (eq(run_tag, "GameName")) {
                    file->game_name = str_clone(arena, xml_inner(&r, run_tag));
                } else if (eq(run_tag, "CategoryName")) {
                    file->category_name = str_clone(arena, xml_inner(&r, run_tag));
                } else if (eq(run_tag, "AttemptCount")) {
                    err_scope(err, "Parse AttemptCount");
                    Str attempts_str = str_clone(arena, xml_inner(&r, run_tag));
                    file->total_attempts = parse_u64(err, attempts_str);
                } else if (eq(run_tag, "Segments")) {
                    file->segments = parse_livesplit_segments(arena, err, &r, run_tag);
                }
            }
        }
    }

    if (r.error != nullptr) {
        bail(err, "Failed to parse LSS XML: %s", r.error);
    }

    // Basic validation
    if (arr_is_empty(file->segments)) {
        bail(err, "No segments found");
    }
    for (u64 i = 0; i < file->segments.count; i++) {
        if (str_is_empty(file->segments[i].name)) {
            bail(err, "Segment %" PRIu64 " has no name", i + 1);
        }
    }
    if (str_is_empty(file->game_name)) {
        bail(err, "Empty game name");
    }
}

void load_livesplit_lss(Arena* arena, ErrorContext* err, Str lss_path, FileDef* file) {
    err_scope(err, "Load LiveSplit LSS file '%.*s'", SF(lss_path));

    Arena* scratch = arena_acquire();
    defer(arena_release(scratch));

    Arr<u8> xml = fs_load_file(err, scratch, lss_path);
    if (err_occurred(err)) return;

    parse_livesplit_lss(err, arena, file, xml);
}

//
// MARK:UI
//

Box* make_text_box(Arena* arena, Str content, Color color) {
    Box* box = arena_push<Box>(arena);

    box->type = BoxType::Text;
    // const char *content_cstr = str_is_empty(content) ? "" : (const char
    // *)content.ptr; Zero length actually means "treat string as null terminated"
    // box->text_obj = TTF_CreateText(engine, font, content_cstr, content.count);
    // TTF_SetTextColorFloat(box->text_obj, color.r, color.g, color.b, color.a);

    return box;
}

Box* make_empty_box(Arena* arena, PxSize size) {
    Box* box = arena_push<Box>(arena);
    box->type = BoxType::Empty;
    box->bbox = some(size);
    return box;
}

Box* make_texture_box(Arena* arena, u64 texture_idx, PxSize size) {
    Box* box = arena_push<Box>(arena);
    box->type = BoxType::Texture;
    box->bbox = some(size);
    box->texture_idx = texture_idx;
    return box;
}

Box* make_solid_color_box(Arena* arena, Color color, PxSize size) {
    Box* box = arena_push<Box>(arena);
    box->type = BoxType::SolidColor;
    box->bbox = some(size);
    box->color = color;
    return box;
}

PxSize compute_box_bbox(Box* box);

PxSize compute_box_bbox_uncached(Box* box) {
    switch (box->type) {
    case BoxType::Empty:
    case BoxType::SolidColor: {
        return box->bbox.opt;
    }
    case BoxType::LeftToRightStack: {
        PxSize total = {};
        for (u64 i = 0; i < box->children.count; i++) {
            PxSize child_bbox = compute_box_bbox(box->children[i]);
            if (i == 0) {
                total = child_bbox;
            } else {
                total.w += child_bbox.w;
                total.h = max(total.h, child_bbox.h);
            }
        }
        return total;
    }
    case BoxType::TopToBottomStack: {
        PxSize total = {};
        for (u64 i = 0; i < box->children.count; i++) {
            PxSize child_bbox = compute_box_bbox(box->children[i]);
            if (i == 0) {
                total = child_bbox;
            } else {
                total.w = max(total.w, child_bbox.w);
                total.h += child_bbox.h;
            }
        }
        return total;
    }
    case BoxType::BackToFrontStack: {
        PxSize total = {};
        for (u64 i = 0; i < box->children.count; i++) {
            PxSize child_bbox = compute_box_bbox(box->children[i]);
            if (i == 0) {
                total = child_bbox;
            } else {
                total.w = max(total.w, child_bbox.w);
                total.h = max(total.h, child_bbox.h);
            }
        }
        return total;
    }
    case BoxType::Text: {
        return {.w = 0, .h = 0};
    }
    case BoxType::Texture: {
        return box->bbox.opt;
    }
    }
}

PxSize compute_box_bbox(Box* box) {
    if (box->bbox.present) {
        return box->bbox.opt;
    }
    PxSize bbox = compute_box_bbox_uncached(box);
    box->bbox = some(bbox);
    return bbox;
}

Box* pad_box_left(Arena* arena, Box* box, u16 pad) {
    PxSize bbox = compute_box_bbox(box);
    Box* pad_box = make_empty_box(arena, {.w = pad, .h = bbox.h});

    Box* parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::LeftToRightStack;
    vec_push(arena, &parent_box->children, pad_box);
    vec_push(arena, &parent_box->children, box);

    return parent_box;
}

Box* pad_box_right(Arena* arena, Box* box, u16 pad) {
    PxSize bbox = compute_box_bbox(box);
    Box* pad_box = make_empty_box(arena, {.w = pad, .h = bbox.h});

    Box* parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::LeftToRightStack;
    vec_push(arena, &parent_box->children, box);
    vec_push(arena, &parent_box->children, pad_box);

    return parent_box;
}

Box* pad_box_top(Arena* arena, Box* box, u16 pad) {
    PxSize bbox = compute_box_bbox(box);
    Box* pad_box = make_empty_box(arena, {.w = bbox.w, .h = pad});

    Box* parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::TopToBottomStack;
    vec_push(arena, &parent_box->children, pad_box);
    vec_push(arena, &parent_box->children, box);

    return parent_box;
}

Box* pad_box_bottom(Arena* arena, Box* box, u16 pad) {
    PxSize bbox = compute_box_bbox(box);
    Box* pad_box = make_empty_box(arena, {.w = bbox.w, .h = pad});

    Box* parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::TopToBottomStack;
    vec_push(arena, &parent_box->children, box);
    vec_push(arena, &parent_box->children, pad_box);

    return parent_box;
}

Box* align_box_center_horiz(Arena* arena, Box* box, u16 width) {
    PxSize bbox = compute_box_bbox(box);
    width = max(width, bbox.w);
    u16 left_pad = (width - bbox.w) / 2;
    u16 right_pad = width - bbox.w - left_pad;

    Box* left_pad_box = make_empty_box(arena, {left_pad, bbox.h});
    Box* right_pad_box = make_empty_box(arena, {right_pad, bbox.h});

    Box* parent = arena_push<Box>(arena);
    parent->type = BoxType::LeftToRightStack;
    vec_push(arena, &parent->children, left_pad_box);
    vec_push(arena, &parent->children, box);
    vec_push(arena, &parent->children, right_pad_box);

    return parent;
}

Box* align_box_center_vert(Arena* arena, Box* box, u16 height) {
    PxSize bbox = compute_box_bbox(box);
    height = max(height, bbox.h);
    u16 top_pad = (height - bbox.h) / 2;
    u16 bottom_pad = height - bbox.h - top_pad;

    Box* top_pad_box = make_empty_box(arena, {bbox.w, top_pad});
    Box* bottom_pad_box = make_empty_box(arena, {bbox.w, bottom_pad});

    Box* parent = arena_push<Box>(arena);
    parent->type = BoxType::TopToBottomStack;
    vec_push(arena, &parent->children, top_pad_box);
    vec_push(arena, &parent->children, box);
    vec_push(arena, &parent->children, bottom_pad_box);

    return parent;
}

Box* prerender_segment(Arena* arena, Session* session, u16 width, u64 idx) {
    constexpr u16 ICON_INNER_PX = 80;
    constexpr u16 ICON_OUTER_PX = 90;
    Box* icon = nullptr;

    // TODO handle empty icons
    icon = make_texture_box(arena, idx, {.w = ICON_INNER_PX, .h = ICON_INNER_PX});
    // } else {
    //     icon = make_empty_box(arena, ICON_INNER, ICON_INNER);
    // }
    icon = align_box_center_horiz(arena, icon, ICON_OUTER_PX);
    icon = align_box_center_vert(arena, icon, ICON_OUTER_PX);

    Color text_color = {.r = 255, .g = 255, .b = 255, .a = 255};
    Box* pad = make_empty_box(arena, {.w = 10, .h = 0});
    Box* title = make_text_box(arena, session->file.segments[idx].name, text_color);
    Box* title_centered = align_box_center_vert(arena, title, ICON_OUTER_PX);

    Box* row_front = arena_push<Box>(arena);
    row_front->type = BoxType::LeftToRightStack;
    vec_push(arena, &row_front->children, icon);
    vec_push(arena, &row_front->children, pad);
    vec_push(arena, &row_front->children, title_centered);

    if (session->timer.mode == TimerMode::Running && idx == session->timer.live_splits.count) {
        PxSize row_front_bbox = compute_box_bbox(row_front);
        Color bg_color = {.r = 0, .g = 0, .b = 0, .a = 255};
        PxSize row_back_size = {.w = width, .h = row_front_bbox.h};
        Box* row_back = make_solid_color_box(arena, bg_color, row_back_size);

        Box* row = arena_push<Box>(arena);
        row->type = BoxType::BackToFrontStack;
        vec_push(arena, &row->children, row_back);
        vec_push(arena, &row->children, row_front);

        return row;
    }

    return row_front;
}

Box* prerender_contents(Arena* arena, Session* session, PxSize size) {
    Color color = {.r = 255, .g = 255, .b = 255, .a = 255};

    Box* game_name = make_text_box(arena, session->file.game_name, color);
    Box* cat_name = make_text_box(arena, session->file.category_name, color);

    Box* game_name_centered = align_box_center_horiz(arena, game_name, size.w);
    Box* cat_name_centered = align_box_center_horiz(arena, cat_name, size.w);

    Box* top = arena_push<Box>(arena);
    top->type = BoxType::TopToBottomStack;
    vec_push(arena, &top->children, game_name_centered);
    vec_push(arena, &top->children, cat_name_centered);

    for (u64 i = 0; i < session->file.segments.count; i++) {
        Box* segment = prerender_segment(arena, session, size.w, i);
        vec_push(arena, &top->children, segment);
    }

    Box* bottom = arena_push<Box>(arena);
    bottom->type = BoxType::TopToBottomStack;

    Duration t = timer_get_elapsed(&session->timer, get_current_monotonic_time());
    Str t_str = format_duration(arena, t, 2, false);
    Box* curr_time = make_text_box(arena, t_str, color);
    PxSize curr_time_bbox = compute_box_bbox(curr_time);
    Box* curr_time_aligned = pad_box_left(arena, curr_time, size.w - curr_time_bbox.w);

    vec_push(arena, &bottom->children, curr_time_aligned);

    // Put timer at bottom
    PxSize top_bbox = compute_box_bbox(top);
    PxSize bottom_bbox = compute_box_bbox(bottom);
    PxSize vsep_size = {.w = 0, .h = (u16)(size.h - top_bbox.h - bottom_bbox.h)};
    Box* vsep = make_empty_box(arena, vsep_size);

    Box* root = arena_push<Box>(arena);
    root->type = BoxType::TopToBottomStack;
    vec_push(arena, &root->children, top);
    vec_push(arena, &root->children, vsep);
    vec_push(arena, &root->children, bottom);

    return root;
}

Box* prerender(Arena* arena, Session* session, PxSize window_size) {
    constexpr u16 PADDING = 10;
    PxSize content_size = {
        .w = (u16)(window_size.w - PADDING * 2),
        .h = (u16)(window_size.h - PADDING * 2),
    };
    Box* timer = prerender_contents(arena, session, content_size);
    timer = pad_box_left(arena, timer, PADDING);
    timer = pad_box_right(arena, timer, PADDING);
    timer = pad_box_top(arena, timer, PADDING);
    timer = pad_box_bottom(arena, timer, PADDING);
    return timer;
}

// TODO for pixel-perfect rendering, need to understand rounding/UV mapping
// w.r.t. pixel center better
void window_to_ndc(Vertex* vertex, PxSize window_size) {
    vertex->x = (vertex->x / (f32)window_size.w) * 2.f - 1.f;
    vertex->y = -((vertex->y / (f32)window_size.h) * 2.f - 1.f);
}

void push_atlas_quad(Arena* arena,
                     PxSize window_size,
                     Atlas* atlas,
                     Mesh* mesh,
                     PxRect src,
                     PxRect dst,
                     Color color) {
    Arr<u16> indices = vec_extend_zero(arena, &mesh->indices, 6);
    indices[0] = (u16)(mesh->vertices.count + 0);
    indices[1] = (u16)(mesh->vertices.count + 1);
    indices[2] = (u16)(mesh->vertices.count + 2);
    indices[3] = (u16)(mesh->vertices.count + 2);
    indices[4] = (u16)(mesh->vertices.count + 1);
    indices[5] = (u16)(mesh->vertices.count + 3);

    Arr<Vertex> vertices = vec_extend_zero(arena, &mesh->vertices, 4);
    // Top left
    vertices[0] = {
        .x = (f32)dst.x,
        .y = (f32)dst.y,
        .z = 0,
        .u = (f32)src.x / (f32)atlas->size.w,
        .v = (f32)src.y / (f32)atlas->size.h,
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
    // Top right
    vertices[1] = {
        .x = (f32)(dst.x + dst.w),
        .y = (f32)dst.y,
        .z = 0,
        .u = (f32)(src.x + src.w) / (f32)atlas->size.w,
        .v = (f32)src.y / (f32)atlas->size.h,
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
    // Bottom left
    vertices[2] = {
        .x = (f32)dst.x,
        .y = (f32)(dst.y + dst.h),
        .z = 0,
        .u = (f32)src.x / (f32)atlas->size.w,
        .v = (f32)(src.y + src.h) / (f32)atlas->size.h,
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
    // Bottom right
    vertices[3] = {
        .x = (f32)(dst.x + dst.w),
        .y = (f32)(dst.y + dst.h),
        .z = 0,
        .u = (f32)(src.x + src.w) / (f32)atlas->size.w,
        .v = (f32)(src.y + src.h) / (f32)atlas->size.h,
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };

    // TODO less awkward way to do this?
    window_to_ndc(&vertices[0], window_size);
    window_to_ndc(&vertices[1], window_size);
    window_to_ndc(&vertices[2], window_size);
    window_to_ndc(&vertices[3], window_size);
}

void make_icon_mesh_inner(Arena* arena,
                          PxSize window_size,
                          Box* box,
                          PxPos where,
                          Atlas* atlas,
                          Mesh* mesh) {
    switch (box->type) {
    case BoxType::Empty: {
        break;
    }
    case BoxType::LeftToRightStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            PxSize child_bbox = compute_box_bbox(box->children[i]);
            make_icon_mesh_inner(arena, window_size, box->children[i], where, atlas, mesh);
            where.x += child_bbox.w;
        }
        break;
    }
    case BoxType::TopToBottomStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            PxSize child_bbox = compute_box_bbox(box->children[i]);
            make_icon_mesh_inner(arena, window_size, box->children[i], where, atlas, mesh);
            where.y += child_bbox.h;
        }
        break;
    }
    case BoxType::BackToFrontStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            make_icon_mesh_inner(arena, window_size, box->children[i], where, atlas, mesh);
        }
        break;
    }
    case BoxType::Text: {
        // TTF_DrawRendererText(box->text_obj, where.x, where.y);
        break;
    }
    case BoxType::Texture: {
        PxRect src = atlas->placements[box->texture_idx];

        f32 src_ratio = (f32)src.w / (f32)src.h;
        f32 dst_ratio = (f32)box->bbox.opt.w / (f32)box->bbox.opt.h;

        // Scale to fit
        PxRect dest = {};
        if (src_ratio > dst_ratio) {
            f32 scale = (f32)box->bbox.opt.w / (f32)src.w;
            dest.w = box->bbox.opt.w;
            dest.h = (u16)SDL_lroundf((f32)src.h * scale);
            dest.x = where.x;
            dest.y = where.y + (u16)SDL_lroundf((f32)(box->bbox.opt.h - dest.h) / 2.f);
        } else {
            f32 scale = (f32)box->bbox.opt.h / (f32)src.h;
            dest.w = (u16)SDL_lroundf((f32)src.w * scale);
            dest.h = box->bbox.opt.h;
            dest.x = where.x + (u16)SDL_lroundf((f32)(box->bbox.opt.w - dest.w) / 2.f);
            dest.y = where.y;
        }
        Color color = {.r = 255, .g = 255, .b = 255, .a = 255};
        push_atlas_quad(arena, window_size, atlas, mesh, src, dest, color);
        break;
    }
    case BoxType::SolidColor: {
        // SDL_SetRenderDrawColorFloat(app->renderer, box->color.r, box->color.g,
        // box->color.b, box->color.a); SDL_FRect r = { .x = where.x, .y =
        // where.y, .w = box->width, .h = box->height };
        // SDL_RenderFillRect(app->renderer, &r);
        break;
    }
    }
}

u64 make_icon_mesh(Arena* arena, PxSize window_size, Session* session, Atlas* atlas, Mesh* mesh) {
    Box* box = prerender(arena, session, window_size);
    u64 start_vertex_count = mesh->vertices.count;
    PxPos where = {.x = 0, .y = 0};
    make_icon_mesh_inner(arena, window_size, box, where, atlas, mesh);
    u64 end_vertex_count = mesh->vertices.count;
    return (end_vertex_count - start_vertex_count) / 4;
}

//
// MARK:Text rendering
//

// TODO cache shaping context
// TODO arena allocate kbts stuff
// TODO handling style/direction/face runs etc.
Arr<ShapedGlyph> shape_text_naive(Arena* arena, Arr<u8> font, Str text) {
    kbts_shape_context* context = kbts_CreateShapeContext(0, 0);
    defer(kbts_DestroyShapeContext(context));
    kbts_ShapePushFontFromMemory(context, font.ptr, (int)font.count, 0);

    kbts_ShapeBegin(context, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(context, (char*)text.ptr, (i32)text.count,
                   KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
    kbts_ShapeEnd(context);

    // Layout runs naively left to right.
    kbts_run Run = {};
    i32 cursor_x = 0, cursor_y = 0;
    // u32 run_idx = 0;
    Vec<ShapedGlyph> output = {};
    vec_prealloc(arena, &output, text.count);
    while (kbts_ShapeRun(context, &Run)) {
        kbts_glyph* glyph = nullptr;
        while (kbts_GlyphIteratorNext(&Run.Glyphs, &glyph)) {
            i32 glyph_x = cursor_x + glyph->OffsetX;
            i32 glyph_y = cursor_y + glyph->OffsetY;

            cursor_x += glyph->AdvanceX;
            cursor_y += glyph->AdvanceY;

            ShapedGlyph* g = vec_push_zero(arena, &output);
            g->glyph_id = glyph->Id;
            g->glyph_x_fu = glyph_x;
            g->glyph_y_fu = glyph_y;
        }
        // run_idx++;
    }

    return vec_arr(&output);
}

Atlas* make_and_upload_atlas(ErrorContext* err,
                             Arena* arena,
                             SDL_GPUDevice* device,
                             SDL_GPUCommandBuffer* command_buffer,
                             SDL_GPUGraphicsPipeline* clear_texture_pipeline,
                             Str name,
                             Arr<CPUTexture> textures,
                             PxSize atlas_size,
                             TextureFilterType filter);

GlyphAtlas make_and_upload_glyph_atlas(ErrorContext* err,
                                       Arena* arena,
                                       SDL_GPUDevice* device,
                                       SDL_GPUCommandBuffer* command_buffer,
                                       SDL_GPUGraphicsPipeline* clear_texture_pipeline,
                                       FT_Library freetype_handle,
                                       Arr<u8> font_file,
                                       u16 face_size_px) {
    err_scope(err, "Initialize text rendering");

    log_assert(face_size_px > 0);

    Arena* scratch = arena_acquire();
    defer(arena_release(scratch));

    FT_Face face = {};
    try_ft(err, {},
           FT_New_Memory_Face(freetype_handle, font_file.ptr, (long)font_file.count, 0, &face));
    defer(FT_Done_Face(face));

    try_ft(err, {}, FT_Set_Pixel_Sizes(face, face_size_px, 0));

    Arr<CPUTexture> textures = arena_push_arr<CPUTexture>(scratch, (u64)face->num_glyphs);
    Arr<GlyphMetrics> metrics = arena_push_arr<GlyphMetrics>(arena, (u64)face->num_glyphs);

    for (u64 glyph_idx = 0; glyph_idx < face->num_glyphs; glyph_idx++) {
        // TODO re-enable hinting once we can account for spacing discrepancies
        // Also maybe disable on macos for more native look?
        try_ft(err, {}, FT_Load_Glyph(face, (u32)glyph_idx, FT_LOAD_NO_HINTING));
        // if (face->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
        //     bail(err, "TODO: handle bitmap glyph");
        // }
        try_ft(err, {}, FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL));

        FT_Bitmap bitmap = face->glyph->bitmap;
        CPUTexture* texture = &textures[glyph_idx];
        Arr<u8> tmp_buffer = {.ptr = bitmap.buffer, .count = bitmap.width * bitmap.rows};
        texture->format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        texture->buffer = arr_clone(scratch, tmp_buffer);
        texture->dims = {.w = (u16)bitmap.width, .h = (u16)bitmap.rows};

        // Convert from 26.6 fixed point pixels to f32 pixels
        metrics[glyph_idx].bearing_px_x = (f32)face->glyph->bitmap_left;
        metrics[glyph_idx].bearing_px_y = (f32)face->glyph->bitmap_top;
    }

    PxSize font_atlas_size = {.w = 2048, .h = 2048};
    Str texture_name = str_format(scratch, "Glyph atlas: family = '%s', style = '%s', size = %dpx",
                                  face->family_name, face->style_name, face_size_px);
    Atlas* atlas =
        make_and_upload_atlas(err, arena, device, command_buffer, clear_texture_pipeline,
                              texture_name, textures, font_atlas_size, TextureFilterType::Nearest);
    return {
        .atlas = atlas,
        .px_per_em = face_size_px,
        .units_per_em = face->units_per_EM,
        .metrics = metrics,
    };
}

u64 make_glyph_mesh(Arena* arena,
                    PxSize window_size,
                    Arr<u8> font_file,
                    GlyphAtlas* atlas,
                    Str text,
                    Mesh* mesh) {
    Arena* scratch = arena_acquire();
    defer(arena_release(scratch));

    if (text.count == 0) {
        return 0;
    }

    u64 start_vertex_count = mesh->vertices.count;

    Arr<ShapedGlyph> shaped_glyphs = shape_text_naive(scratch, font_file, text);
    for (u64 i = 0; i < shaped_glyphs.count; i++) {
        u32 glyph_id = shaped_glyphs[i].glyph_id;

        // Shaping position of glyph
        f32 glyph_px_x = (f32)shaped_glyphs[i].glyph_x_fu * atlas->px_per_em / atlas->units_per_em;
        f32 glyph_px_y = (f32)shaped_glyphs[i].glyph_y_fu * atlas->px_per_em / atlas->units_per_em;

        // Position of glyph bitmap
        f32 bitmap_px_x = glyph_px_x + atlas->metrics[glyph_id].bearing_px_x;
        f32 bitmap_px_y = glyph_px_y + atlas->metrics[glyph_id].bearing_px_y;

        // Convert/round to window space pixel coord
        u16 dest_px_x = (u16)SDL_lroundf(200.f + bitmap_px_x);
        u16 dest_px_y = (u16)SDL_lroundf(200.f - bitmap_px_y);

        PxRect src = atlas->atlas->placements[shaped_glyphs[i].glyph_id];
        PxRect dst = {.x = dest_px_x, .y = dest_px_y, .w = src.w, .h = src.h};
        Color color = {.r = 255, .g = 255, .b = 255, .a = 255};
        push_atlas_quad(arena, window_size, atlas->atlas, mesh, src, dst, color);
    }

    u64 end_vertex_count = mesh->vertices.count;
    return (end_vertex_count - start_vertex_count) / 4;
}

//
// MARK:Rendering
//

SDL_GPUGraphicsPipeline* make_render_pipeline(ErrorContext* err,
                                              SDL_GPUDevice* device,
                                              SDL_Window* window,
                                              SDL_GPUShader* vert_shader,
                                              SDL_GPUShader* frag_shader,
                                              SDL_GPUTextureFormat target_texture_format,
                                              BlendType blend_type) {
    err_scope(err, "Init render pipeline");

    SDL_GPUColorTargetBlendState blend_state = {};
    if (blend_type == BlendType::Over) {
        blend_state = {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
        };
    }

    SDL_GPUColorTargetDescription color_target_descs[] = {{
        .format = target_texture_format,
        .blend_state = blend_state,
    }};

    SDL_GPUVertexBufferDescription vertex_buffer_descs[] = {{
        .slot = 0,
        .pitch = sizeof(Vertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    }};

    SDL_GPUVertexAttribute vertex_attrs[] = {
        {
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0,
        },
        {
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = sizeof(f32) * 3,
        },
        {
            .location = 2,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
            .offset = sizeof(f32) * 5,
        },
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeline_create_info = {
        .vertex_shader = vert_shader,
        .fragment_shader = frag_shader,
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = vertex_buffer_descs,
                .num_vertex_buffers = c_arr_count(vertex_buffer_descs),
                .vertex_attributes = vertex_attrs,
                .num_vertex_attributes = c_arr_count(vertex_attrs),
            },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info =
            {
                .color_target_descriptions = color_target_descs,
                .num_color_targets = c_arr_count(color_target_descs),
            },
    };
    return try_sdl(err, nullptr, SDL_CreateGPUGraphicsPipeline(device, &pipeline_create_info));
}

void clear_texture(ErrorContext* err,
                   SDL_GPUCommandBuffer* command_buffer,
                   SDL_GPUGraphicsPipeline* pipeline,
                   SDL_GPUTexture* texture) {
    SDL_GPUColorTargetInfo color_target_infos[] = {{
        .texture = texture,
        .clear_color = {0.f, 0.f, 0.f, 1.f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    }};
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer, color_target_infos,
                                                     c_arr_count(color_target_infos), nullptr);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_EndGPURenderPass(pass);
}

Atlas* make_and_upload_atlas(ErrorContext* err,
                             Arena* arena,
                             SDL_GPUDevice* device,
                             SDL_GPUCommandBuffer* command_buffer,
                             SDL_GPUGraphicsPipeline* clear_texture_pipeline,
                             Str name,
                             Arr<CPUTexture> textures,
                             PxSize atlas_size,
                             TextureFilterType filter) {
    Arena* scratch = arena_acquire();
    defer(arena_release(scratch));

    // Texture formats must be equal
    SDL_GPUTextureFormat texture_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    for (u64 i = 0; i < textures.count; i++) {
        if (textures[i].dims.w > 0 && textures[i].dims.h > 0) {
            if (texture_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
                texture_format = textures[i].format;
            } else {
                log_assert(texture_format == textures[i].format);
            }
        }
    }
    // There must be at least one non-empty texture
    log_assert(texture_format != SDL_GPU_TEXTUREFORMAT_INVALID);

    //
    // Allocate and clear GPU texture
    //

    SDL_GPUTextureCreateInfo gpu_texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = texture_format,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = (u32)atlas_size.w,
        .height = (u32)atlas_size.h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &gpu_texture_info);
    char* name_cstr = str_to_c(scratch, name);
    SDL_SetGPUTextureName(device, texture, name_cstr);
    clear_texture(err, command_buffer, clear_texture_pipeline, texture);

    //
    // Compute atlas packing
    //

    stbrp_context packer_ctx = {};
    Arr<stbrp_node> packer_nodes = arena_push_arr<stbrp_node>(arena, atlas_size.w);
    stbrp_init_target(&packer_ctx, (i32)atlas_size.w, (i32)atlas_size.h, packer_nodes.ptr,
                      (i32)packer_nodes.count);

    Arr<stbrp_rect> packer_rects = arena_push_arr<stbrp_rect>(arena, textures.count);
    for (u64 i = 0; i < textures.count; i++) {
        packer_rects[i].id = (i32)i;
        if (textures[i].dims.w > 0 && textures[i].dims.h > 0) {
            packer_rects[i].w = textures[i].dims.w + 2;
            packer_rects[i].h = textures[i].dims.h + 2;
        }
    }

    log_assert(stbrp_pack_rects(&packer_ctx, packer_rects.ptr, (i32)packer_rects.count) == 1);

    Arr<PxRect> placements = arena_push_arr<PxRect>(arena, textures.count);
    for (u64 i = 0; i < textures.count; i++) {
        if (textures[i].dims.w > 0 && textures[i].dims.h > 0) {
            placements[i].x = (u16)packer_rects[i].x + 1;
            placements[i].y = (u16)packer_rects[i].y + 1;
            placements[i].w = (u16)textures[i].dims.w;
            placements[i].h = (u16)textures[i].dims.h;
        }
    }

    //
    // Pack textures into transfer buffer
    //

    // Compute transfer buffer size
    Arr<u32> offsets = arena_push_arr<u32>(scratch, textures.count);
    for (u64 i = 0; i < textures.count; i++) {
        if (i > 0) {
            u32 prev_offset = offsets[i - 1];
            u32 prev_size = (u32)textures[i - 1].buffer.count;
            offsets[i] = align_to(prev_offset + prev_size, (u32)512);
        }
    }
    u32 transfer_buffer_size =
        offsets[offsets.count - 1] + (u32)textures[textures.count - 1].buffer.count;

    SDL_GPUTransferBufferCreateInfo transfer_buffer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = transfer_buffer_size,
    };
    SDL_GPUTransferBuffer* transfer_buffer =
        SDL_CreateGPUTransferBuffer(device, &transfer_buffer_info);

    void* buf = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
    for (u64 i = 0; i < textures.count; i++) {
        u64 size = textures[i].buffer.count;
        SDL_memcpy((u8*)buf + offsets[i], textures[i].buffer.ptr, size);
    }
    SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

    //
    // Upload textures
    //

    // TODO coalesce atlas-related copy and render passes
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer);

    for (u64 i = 0; i < textures.count; i++) {
        if (textures[i].dims.w > 0 && textures[i].dims.h > 0) {
            SDL_GPUTextureTransferInfo src = {
                .transfer_buffer = transfer_buffer,
                .offset = offsets[i],
            };
            SDL_GPUTextureRegion dest = {
                .texture = texture,
                .mip_level = 0,
                .layer = 0,
                .x = (u32)placements[i].x,
                .y = (u32)placements[i].y,
                .z = 0,
                .w = (u32)placements[i].w,
                .h = (u32)placements[i].h,
                .d = 1,
            };
            SDL_UploadToGPUTexture(copy_pass, &src, &dest, false);
        }
    }

    SDL_EndGPUCopyPass(copy_pass);

    //
    // Make sampler (doesn't super duper need to happen here but w/e)
    //

    SDL_GPUSamplerCreateInfo sampler_info = {};
    switch (filter) {
    case TextureFilterType::Linear: {
        sampler_info = {
            .min_filter = SDL_GPU_FILTER_LINEAR,
            .mag_filter = SDL_GPU_FILTER_LINEAR,
            .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        };
        break;
    }
    case TextureFilterType::Nearest: {
        sampler_info = {
            .min_filter = SDL_GPU_FILTER_NEAREST,
            .mag_filter = SDL_GPU_FILTER_NEAREST,
            .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        };
        break;
    }
    }

    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device, &sampler_info);

    //
    // Return atlas descriptor
    //

    Atlas* atlas = arena_push<Atlas>(arena);
    atlas->size = atlas_size;
    atlas->texture = texture;
    atlas->sampler = sampler;
    atlas->transfer_buffer = transfer_buffer;
    atlas->placements = placements;
    atlas->packer_nodes = packer_nodes;
    atlas->packer_rects = packer_rects;

    return atlas;
}

SDL_GPUShader* load_shader(ErrorContext* err, SDL_GPUDevice* device, Str name, ShaderType type) {
    err_scope(err, "Load shader '%.*s'", SF(name));

    Arena* scratch = arena_acquire();
    defer(arena_release(scratch));

    Str shader_path =
        str_format(scratch, "src/shaders/%.*s.%.*s", SF(name), SF(OS_SHADER_EXTENSION));

    Arr<u8> source = fs_load_file(err, scratch, shader_path);
    if (err_occurred(err)) return nullptr;

    SDL_GPUShaderCreateInfo info = {};
    switch (type) {
    case ShaderType::Vertex: {
        info = {
            .code_size = source.count,
            .code = (u8*)source.ptr,
            .format = OS_SHADER_FORMAT,
            .stage = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers = 0,
            .num_storage_textures = 0,
            .num_storage_buffers = 0,
            .num_uniform_buffers = 0,
            .props = 0,
        };
        break;
    }
    case ShaderType::Fragment: {
        info = {
            .code_size = source.count,
            .code = (u8*)source.ptr,
            .format = OS_SHADER_FORMAT,
            .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
            .num_samplers = 1,
            .num_storage_textures = 0,
            .num_storage_buffers = 0,
            .num_uniform_buffers = 0,
            .props = 0,
        };
        break;
    }
    }
    return try_sdl(err, nullptr, SDL_CreateGPUShader(device, &info));
}

void init_vertex_buffers(ErrorContext* err, App* app) {
    err_scope(err, "Init vertex+index buffers");

    SDL_GPUBufferCreateInfo vert_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(Vertex) * MAX_VERTEX_COUNT,
    };
    app->vertex_buffer = try_sdl(err, SDL_CreateGPUBuffer(app->device, &vert_info));
    SDL_SetGPUBufferName(app->device, app->vertex_buffer, "THE vertex buffer");

    SDL_GPUBufferCreateInfo index_info = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = sizeof(u16) * MAX_INDEX_COUNT,
    };
    app->index_buffer = try_sdl(err, SDL_CreateGPUBuffer(app->device, &index_info));
    SDL_SetGPUBufferName(app->device, app->index_buffer, "THE index buffer");

    SDL_GPUTransferBufferCreateInfo transfer_buffer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (sizeof(Vertex) * MAX_VERTEX_COUNT) + (sizeof(u16) * MAX_INDEX_COUNT)};
    app->vertex_transfer_buffer =
        try_sdl(err, SDL_CreateGPUTransferBuffer(app->device, &transfer_buffer_info));
}

void make_and_upload_icon_atlas(ErrorContext* err, App* app, SDL_GPUCommandBuffer* command_buffer) {
    Arena* scratch = arena_acquire();
    defer(arena_release(scratch));

    FileDef* file = &app->session->file;
    Arr<CPUTexture> icon_textures = arena_push_arr<CPUTexture>(scratch, file->segments.count);
    for (u64 i = 0; i < file->segments.count; i++) {
        icon_textures[i] = file->segments[i].icon_texture;
    }
    app->icon_atlas = make_and_upload_atlas(
        err, app->session_arena, app->device, command_buffer, app->clear_icon_pipeline,
        S("Icon atlas"), icon_textures, {.w = 1024, .h = 1024}, TextureFilterType::Linear);
}

void init_render_pipelines(ErrorContext* err, App* app) {
    // Load shaders
    app->vert_shader = load_shader(err, app->device, S("vert"), ShaderType::Vertex);
    app->icon_frag_shader = load_shader(err, app->device, S("frag_icon"), ShaderType::Fragment);
    app->glyph_frag_shader = load_shader(err, app->device, S("frag_glyph"), ShaderType::Fragment);
    if (err_occurred(err)) return;

    if (SDL_WindowSupportsGPUPresentMode(app->device, app->window, SDL_GPU_PRESENTMODE_MAILBOX)) {
        try_sdl(err, SDL_SetGPUSwapchainParameters(app->device, app->window,
                                                   SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
                                                   SDL_GPU_PRESENTMODE_MAILBOX));
    } else {
        try_sdl(err, SDL_SetGPUSwapchainParameters(app->device, app->window,
                                                   SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
                                                   SDL_GPU_PRESENTMODE_VSYNC));
    }
    SDL_GPUTextureFormat swapchain_format =
        SDL_GetGPUSwapchainTextureFormat(app->device, app->window);
    app->icon_pipeline =
        make_render_pipeline(err, app->device, app->window, app->vert_shader, app->icon_frag_shader,
                             swapchain_format, BlendType::Over);
    app->glyph_pipeline =
        make_render_pipeline(err, app->device, app->window, app->vert_shader,
                             app->glyph_frag_shader, swapchain_format, BlendType::Over);
    app->clear_icon_pipeline =
        make_render_pipeline(err, app->device, app->window, app->vert_shader, app->icon_frag_shader,
                             SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, BlendType::None);
    app->clear_glyph_pipeline =
        make_render_pipeline(err, app->device, app->window, app->vert_shader, app->icon_frag_shader,
                             SDL_GPU_TEXTUREFORMAT_R8_UNORM, BlendType::None);
    if (err_occurred(err)) return;
}

void init_render_buffers(ErrorContext* err, App* app) {
    SDL_GPUCommandBuffer* command_buffer = try_sdl(err, SDL_AcquireGPUCommandBuffer(app->device));
    defer(SDL_SubmitGPUCommandBuffer(command_buffer));

    init_vertex_buffers(err, app);
    if (err_occurred(err)) return;

    make_and_upload_icon_atlas(err, app, command_buffer);
    if (err_occurred(err)) return;

    app->glyph_atlas = make_and_upload_glyph_atlas(err, app->app_arena, app->device, command_buffer,
                                                   app->clear_glyph_pipeline, app->freetype,
                                                   app->font_file, FONT_SIZE_PX);
}

void init_renderer(ErrorContext* err, App* app) {
    err_scope(err, "Initialize renderer");

    try_ft(err, FT_Init_FreeType(&app->freetype));
    app->font_file = fs_load_file(err, app->app_arena, FONT_PATH);
    if (err_occurred(err)) return;

    app->device =
        try_sdl(err, SDL_CreateGPUDevice(OS_SHADER_FORMAT, RENDERER_DEBUG_MODE_ENABLED, nullptr));
    try_sdl(err, SDL_ClaimWindowForGPUDevice(app->device, app->window));

    init_render_pipelines(err, app);
    init_render_buffers(err, app);
}

void do_geometry_upload_pass(App* app, SDL_GPUCommandBuffer* command_buffer, Mesh* mesh) {
    log_assert(mesh->vertices.count <= MAX_VERTEX_COUNT);
    log_assert(mesh->indices.count <= MAX_INDEX_COUNT);

    u64 vertex_data_size = mesh->vertices.count * sizeof(mesh->vertices[0]);
    u64 index_data_size = mesh->indices.count * sizeof(mesh->indices[0]);

    void* transfer_data =
        (Vertex*)SDL_MapGPUTransferBuffer(app->device, app->vertex_transfer_buffer, true);

    SDL_memcpy(transfer_data, mesh->vertices.ptr, vertex_data_size);
    SDL_memcpy((u8*)transfer_data + vertex_data_size, mesh->indices.ptr, index_data_size);

    SDL_UnmapGPUTransferBuffer(app->device, app->vertex_transfer_buffer);

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(command_buffer);

    // Upload vertex data
    SDL_GPUTransferBufferLocation vert_src = {.transfer_buffer = app->vertex_transfer_buffer,
                                              .offset = 0};
    SDL_GPUBufferRegion vert_dest = {
        .buffer = app->vertex_buffer,
        .offset = 0,
        .size = (u32)vertex_data_size,
    };
    SDL_UploadToGPUBuffer(pass, &vert_src, &vert_dest, true);

    // Upload index data
    SDL_GPUTransferBufferLocation index_src = {
        .transfer_buffer = app->vertex_transfer_buffer,
        .offset = (u32)vertex_data_size,
    };
    SDL_GPUBufferRegion index_dest = {
        .buffer = app->index_buffer,
        .offset = 0,
        .size = (u32)index_data_size,
    };
    SDL_UploadToGPUBuffer(pass, &index_src, &index_dest, true);

    SDL_EndGPUCopyPass(pass);
}

void render_main_color_pass(App* app,
                            SDL_GPUCommandBuffer* command_buffer,
                            SDL_GPUTexture* swapchain_texture,
                            u64 icon_quad_count,
                            u64 glyph_quad_count) {
    SDL_GPUColorTargetInfo color_target_infos[] = {{
        .texture = swapchain_texture,
        .clear_color = {0.f, 0.f, 0.f, 1.f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    }};
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer, color_target_infos,
                                                     c_arr_count(color_target_infos), nullptr);
    SDL_GPUBufferBinding vertex_buffer_bindings[] = {{.buffer = app->vertex_buffer, .offset = 0}};
    SDL_GPUBufferBinding index_buffer_binding = {.buffer = app->index_buffer, .offset = 0};
    SDL_GPUTextureSamplerBinding icon_tex_sampler_bindings[] = {{
        .texture = app->icon_atlas->texture,
        .sampler = app->icon_atlas->sampler,
    }};
    SDL_GPUTextureSamplerBinding glyph_tex_sampler_bindings[] = {{
        .texture = app->glyph_atlas.atlas->texture,
        .sampler = app->glyph_atlas.atlas->sampler,
    }};

    SDL_BindGPUVertexBuffers(pass, 0, vertex_buffer_bindings, c_arr_count(vertex_buffer_bindings));
    SDL_BindGPUIndexBuffer(pass, &index_buffer_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    // Draw icons
    if (icon_quad_count > 0) {
        SDL_BindGPUGraphicsPipeline(pass, app->icon_pipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, icon_tex_sampler_bindings,
                                    c_arr_count(icon_tex_sampler_bindings));
        SDL_DrawGPUIndexedPrimitives(pass,
                                     (u32)(icon_quad_count * 6),  // Index count
                                     1,                           // Instance count
                                     0,                           // First index
                                     0,                           // Vertex offset
                                     0                            // First instance
        );
    }

    // Draw glyphs
    if (glyph_quad_count > 0) {
        SDL_BindGPUGraphicsPipeline(pass, app->glyph_pipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, glyph_tex_sampler_bindings,
                                    c_arr_count(glyph_tex_sampler_bindings));
        SDL_DrawGPUIndexedPrimitives(pass,
                                     (u32)(glyph_quad_count * 6),  // Index count
                                     1,                            // Instance count
                                     (u32)(icon_quad_count * 6),   // First index
                                     0,                            // Vertex offset
                                     0                             // First instance
        );
    }

    SDL_EndGPURenderPass(pass);
}

void render(App* app) {
    Arena* frame_arena = arena_acquire();
    defer(arena_release(frame_arena));

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(app->device);
    SDL_GPUTexture* swapchain_texture = nullptr;
    u32 width = 0;
    u32 height = 0;
    SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, app->window, &swapchain_texture, &width,
                                          &height);
    if (swapchain_texture == nullptr) {  // Apparently can occur if window is minimized
        // Submit nothing in order to get rid of command buffer I guess?
        SDL_SubmitGPUCommandBuffer(command_buffer);
        return;
    }
    PxSize window_size = {.w = (u16)width, .h = (u16)height};

    Mesh mesh = {};
    vec_prealloc(frame_arena, &mesh.vertices, MAX_VERTEX_COUNT);
    vec_prealloc(frame_arena, &mesh.indices, MAX_INDEX_COUNT);

    u64 icon_quad_count =
        make_icon_mesh(frame_arena, window_size, app->session, app->icon_atlas, &mesh);
    u64 glyph_quad_count = make_glyph_mesh(frame_arena, window_size, app->font_file,
                                           &app->glyph_atlas, vec_arr(&app->typed_text), &mesh);
    for (u64 i = 0; i < mesh.vertices.count; i++) {
        mesh.vertices[i].y -= app->scroll * 0.1f;
    }

    do_geometry_upload_pass(app, command_buffer, &mesh);
    render_main_color_pass(app, command_buffer, swapchain_texture, icon_quad_count,
                           glyph_quad_count);

    SDL_SubmitGPUCommandBuffer(command_buffer);
}

//
// MARK:Main
//

SDL_HitTestResult hittest_callback(SDL_Window* window, const SDL_Point* point, void* data) {
    // Would expand the resize radius if I could, but doesn't appear to work on
    // macOS
    return SDL_HITTEST_DRAGGABLE;
}

void init_window(ErrorContext* err, App* app) {
    err_scope(err, "Initialize window");

    try_sdl(err, SDL_SetAppMetadata("Blitter", "0.0.1", nullptr));

    SDL_WindowFlags window_flags =
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    app->window = try_sdl(err, SDL_CreateWindow("Blitter", DEFAULT_WINDOW_SIZE.w,
                                                DEFAULT_WINDOW_SIZE.h, window_flags));
    try_sdl(err, SDL_SetWindowMinimumSize(app->window, MIN_WINDOW_SIZE.w, MIN_WINDOW_SIZE.h));
    try_sdl(err, SDL_SetWindowHitTest(app->window, hittest_callback, nullptr));
}

Session* make_session(ErrorContext* err, Arena* arena, App* app, Str path) {
    Session* session = arena_push<Session>(arena);
    load_livesplit_lss(arena, err, path, &session->file);
    return session;
}

App* init_app(ErrorContext* err) {
    if (g_argv.count < 2) {
        log_fatal("Usage: blitter <path-to-splits-file>");
    }
    Str path = str_from_c(g_argv[1]);

    Arena* root_arena = arena_acquire();
    App* app = arena_push<App>(root_arena);
    app->app_arena = root_arena;

    init_window(err, app);
    // init_text(err, app);

    app->session_arena = arena_acquire();
    app->session = make_session(err, app->session_arena, app, path);
    if (err_occurred(err)) {
        return app;
    }

    init_renderer(err, app);

    return app;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    thread_init(argc, argv);

    ErrorContext err_base = {.arena = arena_acquire()};
    ErrorContext* err = &err_base;
    defer(arena_release(err_base.arena));

    App* app = init_app(err);
    if (err_occurred(err)) {
        err_log(err);
        return SDL_APP_FAILURE;
    }
    *appstate = app;

    return SDL_APP_CONTINUE;
}

void try_load_new_session(App* app, Str lss_path) {
    ErrorContext err_base = {.arena = arena_acquire()};
    ErrorContext* err = &err_base;
    defer(arena_release(err_base.arena));

    Arena* session_arena = arena_acquire();
    Session* session = make_session(err, session_arena, app, lss_path);
    if (err_occurred(err)) {
        err_log(err);
        arena_release(session_arena);
        return;
    }

    if (app->session_arena != nullptr) {
        arena_release(app->session_arena);
    }
    app->session_arena = session_arena;
    app->session = session;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    App* app = (App*)appstate;

    // TODO: this is potentially not the OS timestamp of the keypress, sadly, so
    // not amazingly accurate
    Instant t = instant_from_sdl_nanos(event->common.timestamp);

    if (event->common.type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->common.type == SDL_EVENT_KEY_DOWN && (app->prev_keys & event->key.key) == 0) {
        app->prev_keys |= event->key.key;

        switch (event->key.key) {
        case SDLK_Q: {
            return SDL_APP_SUCCESS;
        }
        case SDLK_SPACE:
        case SDLK_DOWN: {
            timer_apply_action(app->app_arena, app->session, TimerAction::Split, t);
            break;
        }
        case SDLK_UP: {
            timer_apply_action(app->app_arena, app->session, TimerAction::UndoSplit, t);
            break;
        }
        case SDLK_D: {
            timer_apply_action(app->app_arena, app->session, TimerAction::DeleteSplit, t);
            break;
        }
        case SDLK_P: {
            timer_apply_action(app->app_arena, app->session, TimerAction::Pause, t);
            break;
        }
        case SDLK_BACKSPACE: {
            if (app->insert_mode_enabled) {
                if (app->typed_text.count > 0) {
                    vec_pop(&app->typed_text);
                }
            } else {
                timer_apply_action(app->app_arena, app->session, TimerAction::ResetAndSave, t);
            }
            break;
        }
        case SDLK_I: {
            SDL_StartTextInput(app->window);
            app->insert_mode_enabled = true;
            break;
        }
        case SDLK_ESCAPE: {
            SDL_StopTextInput(app->window);
            app->insert_mode_enabled = false;
            vec_reset(&app->typed_text);
            break;
        }
        }
    }

    if (event->common.type == SDL_EVENT_KEY_UP) {
        app->prev_keys &= ~event->key.key;
    }

    if (event->common.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_RIGHT) {
        if (SDL_HasClipboardText()) {
            char* clipboard_cstr = SDL_GetClipboardText();
            defer(SDL_free(clipboard_cstr));
            Str clipboard = str_trim(str_from_c(clipboard_cstr));
            if (!str_is_empty(clipboard)) {  // Empty iff SDL failed to allocate it
                try_load_new_session(app, clipboard);
            }
        }
    }

    if (event->common.type == SDL_EVENT_MOUSE_WHEEL) {
        app->scroll += event->wheel.y;
    }

    if (event->common.type == SDL_EVENT_TEXT_INPUT) {
        Str text = str_from_c(event->text.text);
        vec_extend(app->app_arena, &app->typed_text, text);
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App* app = (App*)appstate;
    render(app);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    // Just let OS clean up everything
}
