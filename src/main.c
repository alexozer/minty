#include "base.h"
#include "platform.h"

#include <stdarg.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <kb_text_shape.h>
#include <stb_rect_pack.h>
#include <xao.h>
#include <yyjson.h>

//
// ::Constants
//

// TODO toggle through build system or something
constexpr bool RENDERER_DEBUG_MODE_ENABLED = true;

constexpr u64 MAX_QUAD_COUNT = 512;
constexpr u64 MAX_VERTEX_COUNT = MAX_QUAD_COUNT * 4;
constexpr u64 MAX_INDEX_COUNT = MAX_QUAD_COUNT * 6;

struct SizePX {
    u16 w, h;
};
derive_struct(SizePX);

constexpr SizePX DEFAULT_WINDOW_SIZE = {360, 600};
constexpr SizePX MIN_WINDOW_SIZE = {200, 100};

// TODO thread through program properly
Str FONT_PATH = S("data/Roboto-Medium.ttf");
// Str FONT_PATH = S("data/NotoSans-Regular.ttf");
// Str FONT_PATH = S("data/NotoSans-Bold.ttf");
// Str FONT_PATH = S("data/KosugiMaru-Regular.otf");
constexpr u32 FONT_SIZE_PX = 40;

//
// ::Types
//

derive_containers(Opt_Duration);

struct TextureHandle {
    u16 idx;
    u16 generation;
};
derive_struct(TextureHandle);

struct CPUTexture {
    SDL_GPUTextureFormat format;
    Arr_u8 buffer;
    SizePX dims;
    TextureHandle handle;  // Automatically populated/replaced by texture cache
};
derive_struct(CPUTexture);

struct SplitRecord {
    u64 attempt_num;
    Arr_Opt_Duration splits;
};
derive_struct(SplitRecord);

struct SegmentDef {
    Str name;
    Arr_u8 icon_png;  // Icon in PNG format
    CPUTexture icon_texture;
};
derive_struct(SegmentDef);

struct FileDef {
    Str game_name;
    Str category_name;
    u64 total_attempts;
    u64 completed_attempts;
    Arr_SegmentDef segments;
    SplitRecord personal_best;
    Arr_Opt_Duration golds;
};
derive_struct(FileDef);

enum TimerMode {
    TimerMode_Init,
    TimerMode_Running,
    TimerMode_Paused,
    TimerMode_Finished,
};
derive_enum(TimerMode);

struct Timer {
    TimerMode mode;
    Vec_Opt_Duration live_splits;
    CPUTexture CPUTexture;
    Instant start_time;
    Instant paused_time;
    Duration total_paused_duration;
};
derive_struct(Timer);

struct Session {
    FileDef file;
    Timer timer;
};
derive_struct(Session);

struct SegSummary {
    Opt_Duration live_split;
    Opt_Duration live_seg;

    // How far ahead/behind this split is compared to PB
    Opt_Duration live_delta;

    // Duration gained or lost this split relative to PB
    Opt_Duration gained;

    Opt_Duration pb_split;
    Opt_Duration pb_seg;

    bool is_new_gold;
};
derive_struct(SegSummary);

enum TimerAction {
    TimerAction_Split,
    TimerAction_UndoSplit,
    TimerAction_DeleteSplit,
    TimerAction_ResetAndSave,
    TimerAction_ResetAndDelete,
    TimerAction_Pause,
};
derive_enum(TimerAction);

enum ShaderType {
    ShaderType_Vertex,
    ShaderType_Fragment,
};
derive_enum(ShaderType);

// Icons don't need color, but it's simpler to just have one format for now
struct Vertex {
    f32 x, y, z;
    f32 u, v;
    u8 r, g, b, a;
};
derive_struct(Vertex);

struct Color {
    u8 r, g, b, a;
};
derive_struct(Color);

enum Axis {
    Axis_X,
    Axis_Y,
};
derive_enum(Axis);

struct PosPX {
    u16 x, y;
};
derive_struct(PosPX);

struct RectPX {
    u16 x, y;
    u16 w, h;
};
derive_struct(RectPX);

union PosF {
    f32 dims[2];
    struct {
        f32 x, y;
    };
};
derive_union(PosF);

union SizeF {
    f32 dims[2];
    struct {
        f32 w, h;
    };
};
derive_union(SizeF);

union RectF {
    struct {
        PosF pos;
        SizeF size;
    };
    struct {
        f32 x, y;
        f32 w, h;
    };
};
derive_union(RectF);

derive_struct(stbrp_node);
derive_struct(stbrp_rect);

struct Atlas {
    SizePX size;
    SDL_GPUTexture *texture;
    SDL_GPUSampler *sampler;
    Arr_RectPX placements;
    SDL_GPUTransferBuffer *transfer_buffer;
    u64 transfer_buffer_size;

    // Rect packer state for incremental repacking
    stbrp_context packer_ctx;
    Arr_stbrp_node packer_nodes;
    Vec_stbrp_rect packer_rects;
};
derive_struct(Atlas);

struct GlyphMetrics {
    f32 bearing_px_x;  // Distance from left start to glyph start
    f32 bearing_px_y;  // Distance from baseline to top of glyph
};
derive_struct(GlyphMetrics);

struct GlyphAtlas {
    Atlas *atlas;
    u16 px_per_em;  // AKA the face size in pixels
    u16 units_per_em;
    Arr_GlyphMetrics metrics;
};
derive_struct(GlyphAtlas);

struct Mesh {
    FixedVec_Vertex vertices;
    FixedVec_u16 indices;
};
derive_struct(Mesh);

struct ShapedGlyph {
    u32 glyph_id;
    i32 glyph_x_fu;  // fu = font unit
    i32 glyph_y_fu;
};
derive_struct(ShapedGlyph);

struct TextureCacheEntry {
    TextureHandle handle;
    u32 atlas_rect_idx;
    bool is_icon;
};
derive_struct(TextureCacheEntry);

struct TextureSystem {
    Atlas *icon_atlas;
    Atlas *glyph_atlas;
    // TODO use fixed-size array or prealloc
    Vec_P_CPUTexture requests;

    Arr_TextureCacheEntry entries;
    Vec_TextureHandle free_handles;
};
derive_struct(TextureSystem);

struct TextureRequest {
    CPUTexture *texture;
    RectF transform;
};
derive_struct(TextureRequest);

enum BlendType {
    BlendType_None,
    BlendType_Over,
};
derive_enum(BlendType);

enum FilterType {
    FilterType_Nearest,
    FilterType_Linear,
};
derive_enum(FilterType);

enum BoxType {
    BoxType_Empty,
    BoxType_Text,
    BoxType_Texture,
    BoxType_SolidColor,
    BoxType_TopToBottomStack,
    BoxType_LeftToRightStack,
    BoxType_BackToFrontStack,
};
derive_enum(BoxType);

// Try some fat struct stuff?
derive_struct_pre(Box);
struct Box {
    BoxType type;
    Opt_SizePX bbox;
    u64 texture_idx;
    Color color;
    Vec_P_Box children;
};
derive_struct_post(Box);

// New UI stuff

enum UI_DimType : u8 {
    UI_DimType_FixedPX,
    UI_DimType_Flex,
};
derive_enum(UI_DimType);

struct UI_Dim {
    UI_DimType type;
    f32 value;  // Might be absolute size in pixels, flex ratio, etc.
};
derive_struct(UI_Dim);

union UI_Size {
    struct {
        UI_Dim w, h;
    };
    UI_Dim dims[2];
};
derive_union(UI_Size);

enum UI_Flag : u16 {
    UI_Flag_ChildLayoutX = bit(0),
    UI_Flag_ChildLayoutY = bit(1),
    UI_Flag_DrawText = bit(2),
    UI_Flag_DrawTexture = bit(3),
    UI_Flag_TextAlignLeft = bit(4),
    UI_Flag_TextAlignRight = bit(5),
};
derive_enum(UI_Flag);

derive_struct_pre(UI_Box);
struct UI_Box {
    UI_Size input_size;
    Str text_content;
    UI_Flag flags;
    u32 texture_id;

    UI_Box *parent;
    Vec_P_UI_Box childs;

    RectF output_size;

    Str id;
};
derive_struct_post(UI_Box);

struct App {
    Arena *app_arena;  // Lives for duration of application
    SDL_Window *window;

    SDL_Keycode prev_keys;
    // TODO: float-based scrolling on NDC could mess with pixel-perfect alignment
    f32 scroll;
    f32 scale;  // `scale + 1.0f` is actual scale
    bool insert_mode_enabled;
    Vec_u8 typed_text;

    Arena *session_arena;
    Session *session;  // Nullable

    SDL_GPUDevice *device;

    // Shaders
    SDL_GPUShader *vert_shader;
    SDL_GPUShader *icon_frag_shader;
    SDL_GPUShader *glyph_frag_shader;

    // Pipelines
    SDL_GPUGraphicsPipeline *icon_pipeline;
    SDL_GPUGraphicsPipeline *glyph_pipeline;
    SDL_GPUGraphicsPipeline *clear_icon_pipeline;
    SDL_GPUGraphicsPipeline *clear_glyph_pipeline;

    // Geometry buffers
    SDL_GPUTransferBuffer *vertex_transfer_buffer;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;

    // Textures
    Atlas *icon_atlas;
    GlyphAtlas glyph_atlas;

    // Text stuff
    FT_Library freetype;
    Arr_u8 font_file;
};
derive_struct(App);

//
// ::Timer
//

CPUTexture convert_srgb_surface_to_rgba(Arena *arena, SDL_Surface *surface) {
    if (!surface) return (CPUTexture){};

    u64 dest_size = (u64)(surface->w * surface->h * 4);
    Arr_u8 buffer = {
        .ptr = (u8 *)arena_push_bytes(arena, dest_size, 8),
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

Opt_Duration dur_sub(Opt_Duration d1, Opt_Duration d2) {
    return (Opt_Duration){
        .opt = d1.opt - d2.opt,
        .present = d1.present && d2.present,
    };
}

Arr_SegSummary calc_seg_summary(Arena *arena, Session *session) {
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

Str format_duration(Arena *arena, Duration duration, u32 ms_digits, bool show_plus_prefix) {
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

void timer_reset(Timer *timer) {
    timer->mode = TimerMode_Init;
    vec_reset(&timer->live_splits);
}

Duration timer_get_elapsed(Timer *timer, Instant event_time) {
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

void timer_apply_action_init(Arena *arena, Session *session, TimerAction action, Instant t) {
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

void timer_apply_action_running(Arena *arena, Session *session, TimerAction action, Instant t) {
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

void timer_apply_action_paused(Arena *arena, Session *session, TimerAction action, Instant t) {
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

void timer_apply_action_finished(Arena *arena, Session *session, TimerAction action, Instant t) {
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

void timer_apply_action(Arena *arena, Session *session, TimerAction action, Instant t) {
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

bool eq(xao_Value v, const char *s) {
    u64 size = (u64)v.end - (u64)v.start;
    return size == SDL_strlen(s) && SDL_memcmp(v.start, s, size) == 0;
}

Str xml_str(xao_Value v) {
    return (Str){.ptr = (u8 *)v.start, .count = (u64)v.end - (u64)v.start};
}

Str xml_inner(xao_Reader *r, xao_Value outer) {
    xao_Value inner = {};
    xao_iter_content(r, outer, &inner);
    return xml_str(inner);
}

// TODO I've been neglectful of `const`ness in my APIs, but obviously this
// should be const
u8 PNG_HEADER[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

SDL_Surface *sdl_load_png_io(ErrorContext *err, SDL_IOStream *stream) {
    if (!stream) return nullptr;
    Scope scope = scope_open(err);

    SDL_Surface *surface = SDL_LoadPNG_IO(stream, false);
    if (!surface) {
        err_report(err, "%s", SDL_GetError());
    }

    scope_close(scope, "Load PNG from stream");
    return surface;
}

void sdl_destroy_surface(SDL_Surface *surface) {
    if (surface) SDL_DestroySurface(surface);
}

CPUTexture decode_png_to_texture(ErrorContext *err, Arena *arena, Arr_u8 png) {
    Scope scope = scope_open(err);

    SDL_IOStream *stream = sdl_io_from_mem(err, png);
    SDL_Surface *surface = sdl_load_png_io(err, stream);
    CPUTexture texture = convert_srgb_surface_to_rgba(arena, surface);
    sdl_destroy_surface(surface);
    sdl_close_io(stream);

    scope_close(scope, "Decode png to texture");
    return texture;
}

void parse_segment_icon(ErrorContext *err, Arena *arena, SegmentDef *segment, Str base64) {
    Scope scope = scope_open(err);

    segment->icon_png = decode_base64(err, arena, base64);
    Opt_u64 png_header_offset = str_find(segment->icon_png, ARR(PNG_HEADER));
    if (png_header_offset.present) {
        Arr_u8 png_buf =
            arr_slice(segment->icon_png, png_header_offset.opt, segment->icon_png.count);
        segment->icon_texture = decode_png_to_texture(err, arena, png_buf);
    } else {
        err_report(err, "PNG header not found");
    }

    scope_close(scope, "Decode icon for segment '%.*s'", SF(segment->name));
}

Arr_SegmentDef parse_livesplit_segments(ErrorContext *err,
                                        Arena *arena,
                                        xao_Reader *r,
                                        xao_Value segments_tag) {
    Scope scope = scope_open(err);

    Vec_SegmentDef segments = {};
    xao_Value seg_tag = {};
    while (xao_iter_tags(r, segments_tag, &seg_tag)) {
        SegmentDef *seg = vec_push_zero(arena, &segments);
        xao_Value attr_tag = {};
        while (xao_iter_tags(r, seg_tag, &attr_tag)) {
            if (eq(attr_tag, "Name")) {
                seg->name = str_clone(arena, xml_inner(r, attr_tag));

            } else if (eq(attr_tag, "Icon")) {
                Str base64 = xml_inner(r, attr_tag);
                if (!is_empty(base64)) {
                    parse_segment_icon(err, arena, seg, base64);
                }
            }
        }
    }

    scope_close(scope, "Parse LiveSplit LSS segments");
    return vec_arr(&segments);
}

void parse_livesplit_lss(ErrorContext *err, Arena *arena, FileDef *file, Arr_u8 xml) {
    Scope scope = scope_open(err);

    if (!str_is_valid_utf8(xml)) {
        err_report(err, "Invalid UTF-8");
    }

    xao_Reader r = xao_reader((char *)xml.ptr, xml.count);
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
                    Scope attempt_count_scope = scope_open(err);
                    Str attempts_str = str_clone(arena, xml_inner(&r, run_tag));
                    file->total_attempts = parse_u64(err, attempts_str);
                    scope_close(attempt_count_scope, "Parse AttemptCount");

                } else if (eq(run_tag, "Segments")) {
                    file->segments = parse_livesplit_segments(err, arena, &r, run_tag);
                }
            }
        }
    }

    if (r.error != nullptr) {
        err_report(err, "Failed to parse LSS XML: %s", r.error);
    }

    // Basic validation
    if (is_empty(file->segments)) {
        err_report(err, "No segments found");
    }
    for (u64 i = 0; i < file->segments.count; i++) {
        if (is_empty(A(file->segments, i).name)) {
            err_report(err, "Segment %" PRIu64 " has no name", i + 1);
        }
    }
    if (is_empty(file->game_name)) {
        err_report(err, "Empty game name");
    }

    scope_close(scope, "Parse LiveSplit LSS");
}

void load_livesplit_lss(ErrorContext *err, Arena *arena, Str lss_path, FileDef *file) {
    Arena *scratch = arena_acquire();
    Scope scope = scope_open(err);

    Arr_u8 xml = os_read_file(err, scratch, lss_path);
    parse_livesplit_lss(err, arena, file, xml);

    scope_close(scope, "Load LiveSplit LSS file '%.*s'", SF(lss_path));
    arena_release(scratch);
}

//
// ::UI v1
//

Box *make_text_box(Arena *arena, Str content, Color color) {
    Box *box = arena_push(arena, Box);

    box->type = BoxType_Text;
    // const char *content_cstr = is_empty(content) ? "" : (const char
    // *)content.ptr; Zero length actually means "treat string as null terminated"
    // box->text_obj = TTF_CreateText(engine, font, content_cstr, content.count);
    // TTF_SetTextColorFloat(box->text_obj, color.r, color.g, color.b, color.a);

    return box;
}

Box *make_empty_box(Arena *arena, SizePX size) {
    Box *box = arena_push(arena, Box);
    box->type = BoxType_Empty;
    box->bbox = some(size, SizePX);
    return box;
}

Box *make_texture_box(Arena *arena, u64 texture_idx, SizePX size) {
    Box *box = arena_push(arena, Box);
    box->type = BoxType_Texture;
    box->bbox = some(size, SizePX);
    box->texture_idx = texture_idx;
    return box;
}

Box *make_solid_color_box(Arena *arena, Color color, SizePX size) {
    Box *box = arena_push(arena, Box);
    box->type = BoxType_SolidColor;
    box->bbox = some(size, SizePX);
    box->color = color;
    return box;
}

SizePX compute_box_bbox(Box *box);

SizePX compute_box_bbox_uncached(Box *box) {
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

SizePX compute_box_bbox(Box *box) {
    if (box->bbox.present) {
        return box->bbox.opt;
    }
    SizePX bbox = compute_box_bbox_uncached(box);
    box->bbox = some(bbox, SizePX);
    return bbox;
}

Box *pad_box_left(Arena *arena, Box *box, u16 pad) {
    SizePX bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, (SizePX){pad, bbox.h});

    Box *parent_box = arena_push(arena, Box);
    parent_box->type = BoxType_LeftToRightStack;
    vec_push(arena, &parent_box->children, pad_box);
    vec_push(arena, &parent_box->children, box);

    return parent_box;
}

Box *pad_box_right(Arena *arena, Box *box, u16 pad) {
    SizePX bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, (SizePX){pad, bbox.h});

    Box *parent_box = arena_push(arena, Box);
    parent_box->type = BoxType_LeftToRightStack;
    vec_push(arena, &parent_box->children, box);
    vec_push(arena, &parent_box->children, pad_box);

    return parent_box;
}

Box *pad_box_top(Arena *arena, Box *box, u16 pad) {
    SizePX bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, (SizePX){bbox.w, pad});

    Box *parent_box = arena_push(arena, Box);
    parent_box->type = BoxType_TopToBottomStack;
    vec_push(arena, &parent_box->children, pad_box);
    vec_push(arena, &parent_box->children, box);

    return parent_box;
}

Box *pad_box_bottom(Arena *arena, Box *box, u16 pad) {
    SizePX bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, (SizePX){bbox.w, pad});

    Box *parent_box = arena_push(arena, Box);
    parent_box->type = BoxType_TopToBottomStack;
    vec_push(arena, &parent_box->children, box);
    vec_push(arena, &parent_box->children, pad_box);

    return parent_box;
}

Box *align_box_center_horiz(Arena *arena, Box *box, u16 width) {
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

Box *align_box_center_vert(Arena *arena, Box *box, u16 height) {
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

Box *prerender_segment(Arena *arena, Session *session, u16 width, u64 idx) {
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

Box *prerender_contents(Arena *arena, Session *session, SizePX size) {
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

Box *prerender(Arena *arena, Session *session, SizePX window_size) {
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

void window_to_ndc(Vertex *vertex, SizePX window_size) {
    vertex->x = (vertex->x / (f32)window_size.w) * 2.f - 1.f;
    vertex->y = -((vertex->y / (f32)window_size.h) * 2.f - 1.f);
}

void push_atlas_quad(SizePX window_size,
                     Atlas *atlas,
                     Mesh *mesh,
                     RectPX src,
                     RectPX dst,
                     Color color) {
    Arr_u16 indices = fvec_extend_zero(&mesh->indices, 6);
    A(indices, 0) = (u16)(mesh->vertices.count + 0);
    A(indices, 1) = (u16)(mesh->vertices.count + 1);
    A(indices, 2) = (u16)(mesh->vertices.count + 2);
    A(indices, 3) = (u16)(mesh->vertices.count + 2);
    A(indices, 4) = (u16)(mesh->vertices.count + 1);
    A(indices, 5) = (u16)(mesh->vertices.count + 3);

    Arr_Vertex vertices = fvec_extend_zero(&mesh->vertices, 4);
    // Top left
    A(vertices, 0) = (Vertex){
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
    A(vertices, 1) = (Vertex){
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
    A(vertices, 2) = (Vertex){
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
    A(vertices, 3) = (Vertex){
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

    // Kinda awkward but whatever
    window_to_ndc(&A(vertices, 0), window_size);
    window_to_ndc(&A(vertices, 1), window_size);
    window_to_ndc(&A(vertices, 2), window_size);
    window_to_ndc(&A(vertices, 3), window_size);
}

void make_icon_mesh_inner(SizePX window_size, Box *box, PosPX where, Atlas *atlas, Mesh *mesh) {
    switch (box->type) {
    case BoxType_Empty: {
        break;
    }
    case BoxType_LeftToRightStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            SizePX child_bbox = compute_box_bbox(A(box->children, i));
            make_icon_mesh_inner(window_size, A(box->children, i), where, atlas, mesh);
            where.x += child_bbox.w;
        }
        break;
    }
    case BoxType_TopToBottomStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            SizePX child_bbox = compute_box_bbox(A(box->children, i));
            make_icon_mesh_inner(window_size, A(box->children, i), where, atlas, mesh);
            where.y += child_bbox.h;
        }
        break;
    }
    case BoxType_BackToFrontStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            make_icon_mesh_inner(window_size, A(box->children, i), where, atlas, mesh);
        }
        break;
    }
    case BoxType_Text: {
        // TTF_DrawRendererText(box->text_obj, where.x, where.y);
        break;
    }
    case BoxType_Texture: {
        RectPX src = A(atlas->placements, box->texture_idx);

        f32 src_ratio = (f32)src.w / (f32)src.h;
        f32 dst_ratio = (f32)box->bbox.opt.w / (f32)box->bbox.opt.h;

        // Scale to fit
        RectPX dest = {};
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
        push_atlas_quad(window_size, atlas, mesh, src, dest, color);
        break;
    }
    case BoxType_SolidColor: {
        // SDL_SetRenderDrawColorFloat(app->renderer, box->color.r, box->color.g,
        // box->color.b, box->color.a); SDL_FRect r = { .x = where.x, .y =
        // where.y, .w = box->width, .h = box->height };
        // SDL_RenderFillRect(app->renderer, &r);
        break;
    }
    }
}

u64 make_icon_mesh(SizePX window_size, Session *session, Atlas *atlas, Mesh *mesh) {
    Arena *scratch = arena_acquire();

    Box *box = prerender(scratch, session, window_size);
    u64 start_vertex_count = mesh->vertices.count;
    PosPX where = {0, 0};
    make_icon_mesh_inner(window_size, box, where, atlas, mesh);
    u64 end_vertex_count = mesh->vertices.count;

    arena_release(scratch);
    return (end_vertex_count - start_vertex_count) / 4;
}

//
// ::UI v2
//

void ui_flex_x(UI_Box *box, f32 ratio) {
    box->input_size.w = (UI_Dim){
        .type = UI_DimType_Flex,
        .value = ratio,
    };
}

void ui_flex_y(UI_Box *box, f32 ratio) {
    box->input_size.h = (UI_Dim){
        .type = UI_DimType_Flex,
        .value = ratio,
    };
}

void ui_fixed_x(UI_Box *box, f32 size_px) {
    box->input_size.w = (UI_Dim){
        .type = UI_DimType_FixedPX,
        .value = size_px,
    };
}
void ui_fixed_y(UI_Box *box, f32 size_px) {
    box->input_size.h = (UI_Dim){
        .type = UI_DimType_FixedPX,
        .value = size_px,
    };
}

void ui_parent(Arena *arena, UI_Box *child, UI_Box *parent) {
    child->parent = parent;
    vec_push(arena, &parent->childs, child);
}

// TODO don't require ID for every box
UI_Box *ui_box(Arena *arena, Str id) {
    UI_Box *box = arena_push(arena, UI_Box);
    box->id = id;
    return box;
}

UI_Box *ui_template(Arena *arena, UI_Box *template, Str id) {
    // TODO do this in a more principled way
    UI_Box *box = ui_box(arena, id);
    box->input_size = template->input_size;
    box->text_content = template->text_content;
    box->flags = template->flags;
    box->texture_id = template->texture_id;
    if (template->parent) {
        ui_parent(arena, box, template->parent);
    }
    return box;
}

UI_Box *build_ui_segment(Arena *arena, Session *session, u64 idx) {
    UI_Box *row = ui_box(arena, str_format(arena, "row%" PRIu64, idx));
    ui_flex_x(row, 1);
    ui_fixed_y(row, 80);
    row->flags |= UI_Flag_ChildLayoutX;
    row->id = str_format(arena, "icon%" PRIu64, idx);

    UI_Box *icon = ui_box(arena, S("space1"));
    ui_parent(arena, icon, row);
    ui_fixed_x(icon, 80);
    ui_flex_y(icon, 1);
    icon->flags |= UI_Flag_DrawTexture;

    for (u64 i = 0; i < 3; i++) {
        UI_Box *col = ui_box(arena, S("space"));
        ui_parent(arena, col, row);
        ui_flex_x(col, 1);
        ui_flex_y(col, 1);
    }

    return row;
}

UI_Box *build_ui_segments(Arena *arena, Session *session) {
    UI_Box *parent = ui_box(arena, S("segments parent"));
    parent->flags |= UI_Flag_ChildLayoutY;
    ui_flex_x(parent, 1);
    ui_flex_y(parent, 1);

    // for (u64 i = 0; i < session->file.segments.count; i++) {
    UI_Box *row = build_ui_segment(arena, session, 0);
    ui_parent(arena, row, parent);
    // }

    return parent;
}

UI_Box *build_ui(Arena *arena, Session *session, SizePX size) {
    UI_Box *root = ui_box(arena, S("root"));
    ui_fixed_x(root, size.w);
    ui_fixed_y(root, size.h);
    root->flags |= UI_Flag_ChildLayoutY;

    UI_Box *child_template = ui_box(arena, S("child template"));
    ui_flex_x(child_template, 1);
    ui_fixed_y(child_template, 40);
    child_template->parent = root;

    // Game name
    UI_Box *game_name = ui_template(arena, child_template, S("game name"));
    game_name->flags |= UI_Flag_DrawText;
    game_name->text_content = session->file.game_name;

    // Category name
    UI_Box *category_name = ui_template(arena, child_template, S("category name"));
    category_name->flags |= UI_Flag_DrawText;
    category_name->text_content = session->file.game_name;

    UI_Box *segments = build_ui_segments(arena, session);
    ui_parent(arena, segments, root);

    return root;
}

void layout_ui_impl(UI_Box *box);

void layout_ui_main_axis(UI_Box *parent, Axis axis) {
    // Uh oh, unbounded array access?!? Call the safety police
    log_assert(axis < c_arr_count(parent->output_size.size.dims));

    f32 parent_pos = parent->output_size.pos.dims[axis];
    f32 parent_size = parent->output_size.size.dims[axis];
    f32 total_fixed_px = 0;
    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Dim *child_input = &A(parent->childs, i)->input_size.dims[axis];

        if (child_input->type == UI_DimType_FixedPX) {
            total_fixed_px += child_input->value;
        }
    }
    if (total_fixed_px > parent_size) {
        // Out of room! Make 'em flex instead!
        total_fixed_px = 0;
        for (u64 i = 0; i < parent->childs.count; i++) {
            UI_Dim *in_size = &A(parent->childs, i)->input_size.dims[axis];

            switch (in_size->type) {
            case UI_DimType_FixedPX: {
                in_size->type = UI_DimType_Flex;
                in_size->value /= total_fixed_px;
                break;
            }
            case UI_DimType_Flex: {
                in_size->value = 0;
                break;
            }
            }
        }
    }

    // Compute total flex units
    f32 total_flex_units = 0;
    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Dim *in_size = &A(parent->childs, i)->input_size.dims[axis];

        if (in_size->type == UI_DimType_Flex) {
            total_flex_units += in_size->value;
        }
    }

    // Compute all children pos/size
    f32 total_flex_px = parent_size - total_fixed_px;
    f32 current_pos_px = 0;
    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Dim *in_size = &A(parent->childs, i)->input_size.dims[axis];
        f32 *out_size = &A(parent->childs, i)->output_size.size.dims[axis];
        f32 *out_pos = &A(parent->childs, i)->output_size.pos.dims[axis];

        *out_pos = current_pos_px + parent_pos;

        switch (in_size->type) {
        case UI_DimType_FixedPX: {
            *out_size = in_size->value;
            break;
        }
        case UI_DimType_Flex: {
            *out_size = in_size->value / total_flex_units * total_flex_px;
            break;
        }
        }

        current_pos_px += *out_size;
    }
}

void layout_ui_cross_axis(UI_Box *parent, Axis axis) {
    // Uh oh, unbounded array access?!? Call the safety police
    log_assert(axis < c_arr_count(parent->output_size.size.dims));

    f32 parent_size = parent->output_size.size.dims[axis];

    for (u64 i = 0; i < parent->childs.count; i++) {
        UI_Dim *in_size = &A(parent->childs, i)->input_size.dims[axis];
        f32 *out_size = &A(parent->childs, i)->output_size.size.dims[axis];
        f32 *out_pos = &A(parent->childs, i)->output_size.pos.dims[axis];

        *out_pos = parent->output_size.pos.dims[axis];

        switch (in_size->type) {
        case UI_DimType_FixedPX: {
            *out_size = in_size->value;
            break;
        }
        case UI_DimType_Flex: {
            *out_size = parent_size;
            break;
        }
        }
    }
}

// Compute layout of children, assuming root pos/size is computed
void layout_ui_impl(UI_Box *box) {
    log_assert(box->input_size.dims[Axis_X].value > 0);
    log_assert(box->input_size.dims[Axis_Y].value > 0);

    Axis main_axis = Axis_X;
    if (box->flags & UI_Flag_ChildLayoutX) {
        main_axis = Axis_X;
    } else if (box->flags & UI_Flag_ChildLayoutY) {
        main_axis = Axis_Y;
    } else {
        log_assert(is_empty(box->childs));
    }
    layout_ui_main_axis(box, main_axis);
    layout_ui_cross_axis(box, main_axis == Axis_X ? Axis_Y : Axis_X);

    // Recursively compute child layouts
    for (u64 i = 0; i < box->childs.count; i++) {
        UI_Box *child = A(box->childs, i);
        layout_ui_impl(child);
    }
}

void layout_ui(UI_Box *root) {
    log_assert(root->input_size.w.type == UI_DimType_FixedPX);
    log_assert(root->input_size.h.type == UI_DimType_FixedPX);
    root->output_size.x = 0;
    root->output_size.y = 0;
    root->output_size.w = root->input_size.w.value;
    root->output_size.h = root->input_size.h.value;
    layout_ui_impl(root);
}

u64 make_ui_mesh(Arena *arena, SizePX window_size, UI_Box *box, Mesh *mesh) {
    u64 start_quad_count = mesh->vertices.count / 4;

    // Fake atlas for now
    Atlas atlas = {.size = window_size};
    RectPX rect_px = {
        .x = (u16)SDL_lroundf(box->output_size.x),
        .y = (u16)SDL_lroundf(box->output_size.y),
        .w = (u16)SDL_lroundf(box->output_size.w),
        .h = (u16)SDL_lroundf(box->output_size.h),
    };
    push_atlas_quad(window_size, &atlas, mesh, rect_px, rect_px, (Color){});

    for (u64 i = 0; i < box->childs.count; i++) {
        UI_Box *child = A(box->childs, i);
        make_ui_mesh(arena, window_size, child, mesh);
    }

    u64 end_quad_count = mesh->vertices.count / 4;
    return end_quad_count - start_quad_count;
}

//
// ::Glyph system
//

// TODO check font for errors on load, but afterwards assume it's good
// TODO cache shaping context
// TODO arena allocate kbts stuff
// TODO handling style/direction/face runs etc.
Arr_ShapedGlyph shape_text_naive(Arena *arena, Arr_u8 font, Str text) {
    if (text.count == 0) return (Arr_ShapedGlyph){};

    Vec_ShapedGlyph output = {};
    kbts_shape_context *context = NULL;

    context = kbts_CreateShapeContext(0, 0);
    kbts_ShapePushFontFromMemory(context, font.ptr, (int)font.count, 0);

    kbts_ShapeBegin(context, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(context, (char *)text.ptr, (i32)text.count,
                   KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
    kbts_ShapeEnd(context);

    // Layout runs naively left to right.
    kbts_run Run = {};
    i32 cursor_x = 0, cursor_y = 0;
    // u32 run_idx = 0;
    vec_prealloc(arena, &output, text.count);
    while (kbts_ShapeRun(context, &Run)) {
        kbts_glyph *glyph = nullptr;
        while (kbts_GlyphIteratorNext(&Run.Glyphs, &glyph)) {
            i32 glyph_x = cursor_x + glyph->OffsetX;
            i32 glyph_y = cursor_y + glyph->OffsetY;

            cursor_x += glyph->AdvanceX;
            cursor_y += glyph->AdvanceY;

            ShapedGlyph *g = vec_push_zero(arena, &output);
            g->glyph_id = glyph->Id;
            g->glyph_x_fu = glyph_x;
            g->glyph_y_fu = glyph_y;
        }
        // run_idx++;
    }

    kbts_DestroyShapeContext(context);
    return vec_arr(&output);
}

// TODO split fallible font sanity check vs. infallible atlas creation
GlyphAtlas make_and_upload_glyph_atlas(ErrorContext *err,
                                       Arena *arena,
                                       SDL_GPUDevice *device,
                                       SDL_GPUCommandBuffer *command_buffer,
                                       SDL_GPUGraphicsPipeline *clear_texture_pipeline,
                                       FT_Library freetype_handle,
                                       Arr_u8 font_file,
                                       u16 face_size_px) {
    if (!command_buffer) return (GlyphAtlas){};
    if (!clear_texture_pipeline) return (GlyphAtlas){};
    if (!freetype_handle) return (GlyphAtlas){};

    log_assert(face_size_px > 0);
    Arena *scratch = arena_acquire();
    Scope scope = scope_open(err);

    FT_Face face = {};
    if (FT_New_Memory_Face(freetype_handle, font_file.ptr, (long)font_file.count, 0, &face) !=
        FT_Err_Ok) {
        // TODO
        // break;
    }

    if (FT_Set_Pixel_Sizes(face, face_size_px, 0) != FT_Err_Ok) {
        // TODO
        // break;
    }

    Arr_CPUTexture textures = arena_push_arr(scratch, CPUTexture, (u64)face->num_glyphs);
    Arr_GlyphMetrics metrics = arena_push_arr(arena, GlyphMetrics, (u64)face->num_glyphs);

    for (u64 glyph_idx = 0; glyph_idx < face->num_glyphs; glyph_idx++) {
        // TODO re-enable hinting once we can account for spacing discrepancies
        // Also maybe disable on macos for more native look?
        FT_Load_Glyph(face, (u32)glyph_idx, FT_LOAD_NO_HINTING);
        // if (face->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
        //     bail(err, "TODO: handle bitmap glyph");
        // }
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);

        FT_Bitmap bitmap = face->glyph->bitmap;
        CPUTexture *texture = &A(textures, glyph_idx);
        Arr_u8 tmp_buffer = {.ptr = bitmap.buffer, .count = bitmap.width * bitmap.rows};
        texture->format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        texture->buffer = arr_clone(scratch, tmp_buffer);
        texture->dims = (SizePX){(u16)bitmap.width, (u16)bitmap.rows};

        // Convert from 26.6 fixed point pixels to f32 pixels
        A(metrics, glyph_idx).bearing_px_x = (f32)face->glyph->bitmap_left;
        A(metrics, glyph_idx).bearing_px_y = (f32)face->glyph->bitmap_top;
    }

    SizePX font_atlas_size = {.w = 2048, .h = 2048};
    Str texture_name = str_format(scratch, "Glyph atlas: family = '%s', style = '%s', size = %dpx",
                                  face->family_name, face->style_name, face_size_px);
    Atlas *atlas =
        make_and_upload_atlas(err, arena, device, command_buffer, clear_texture_pipeline,
                              texture_name, textures, font_atlas_size, FilterType_Nearest);
    GlyphAtlas ret = {
        .atlas = atlas,
        .px_per_em = face_size_px,
        .units_per_em = face->units_per_EM,
        .metrics = metrics,
    };

    FT_Done_Face(face);
    scope_close(scope, "Create and upload glyph atlas");
    arena_release(scratch);
    return ret;
}

u64 make_glyph_mesh(SizePX window_size, Arr_u8 font_file, GlyphAtlas *atlas, Str text, Mesh *mesh) {
    Arena *scratch = arena_acquire();

    u64 start_vertex_count = mesh->vertices.count;

    Arr_ShapedGlyph shaped_glyphs = shape_text_naive(scratch, font_file, text);
    for (u64 i = 0; i < shaped_glyphs.count; i++) {
        u32 glyph_id = A(shaped_glyphs, i).glyph_id;

        // Shaping position of glyph
        f32 glyph_px_x =
            (f32)A(shaped_glyphs, i).glyph_x_fu * atlas->px_per_em / atlas->units_per_em;
        f32 glyph_px_y =
            (f32)A(shaped_glyphs, i).glyph_y_fu * atlas->px_per_em / atlas->units_per_em;

        // Position of glyph bitmap
        f32 bitmap_px_x = glyph_px_x + A(atlas->metrics, glyph_id).bearing_px_x;
        f32 bitmap_px_y = glyph_px_y + A(atlas->metrics, glyph_id).bearing_px_y;

        // Convert/round to window space pixel coord
        u16 dest_px_x = (u16)SDL_lroundf(200.f + bitmap_px_x);
        u16 dest_px_y = (u16)SDL_lroundf(200.f - bitmap_px_y);

        u32 shaped_id = A(shaped_glyphs, i).glyph_id;
        RectPX src = A(atlas->atlas->placements, shaped_id);
        RectPX dst = {.x = dest_px_x, .y = dest_px_y, .w = src.w, .h = src.h};
        Color color = {.r = 255, .g = 255, .b = 255, .a = 255};
        push_atlas_quad(window_size, atlas->atlas, mesh, src, dst, color);
    }

    u64 end_vertex_count = mesh->vertices.count;
    u64 quad_count = (end_vertex_count - start_vertex_count) / 4;

    arena_release(scratch);
    return quad_count;
}

//
// ::Rendering
//

SDL_GPUGraphicsPipeline *make_render_pipeline(ErrorContext *err,
                                              SDL_GPUDevice *device,
                                              SDL_Window *window,
                                              SDL_GPUShader *vert_shader,
                                              SDL_GPUShader *frag_shader,
                                              SDL_GPUTextureFormat target_texture_format,
                                              BlendType blend_type) {
    if (!device) return nullptr;
    if (!window) return nullptr;
    if (!vert_shader) return nullptr;
    if (!frag_shader) return nullptr;
    Scope scope = scope_open(err);

    SDL_GPUColorTargetBlendState blend_state = {};
    if (blend_type == BlendType_Over) {
        blend_state = (SDL_GPUColorTargetBlendState){
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

    SDL_GPUGraphicsPipeline *pipeline =
        SDL_CreateGPUGraphicsPipeline(device, &pipeline_create_info);
    if (!pipeline) {
        err_report(err, "%s", SDL_GetError());
    }

    scope_close(scope, "Create render pipeline");
    return pipeline;
}

SDL_GPUShader *load_shader(ErrorContext *err,
                           SDL_GPUDevice *device,
                           Arr_u8 source,
                           ShaderType type) {
    Scope scope = scope_open(err);
    Arena *scratch = arena_acquire();

    SDL_GPUShaderCreateInfo info = {};
    switch (type) {
    case ShaderType_Vertex: {
        info = (SDL_GPUShaderCreateInfo){
            .code_size = source.count,
            .code = (u8 *)source.ptr,
            .format = OS_SHADERS.format,
            .stage = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers = 0,
            .num_storage_textures = 0,
            .num_storage_buffers = 0,
            .num_uniform_buffers = 0,
            .props = 0,
        };
        break;
    }
    case ShaderType_Fragment: {
        info = (SDL_GPUShaderCreateInfo){
            .code_size = source.count,
            .code = (u8 *)source.ptr,
            .format = OS_SHADERS.format,
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
    SDL_GPUShader *shader = SDL_CreateGPUShader(device, &info);
    if (!shader) {
        err_report(err, "%s", SDL_GetError());
    }

    arena_release(scratch);
    scope_close(scope, "Load shader");
    return shader;
}

SDL_GPUBuffer *sdl_create_gpu_buffer(ErrorContext *err,
                                     SDL_GPUDevice *device,
                                     SDL_GPUBufferCreateInfo *info,
                                     Str name) {
    if (!device) return nullptr;
    Scope scope = scope_open(err);
    Arena *scratch = arena_acquire();

    SDL_GPUBuffer *gpu_buffer = SDL_CreateGPUBuffer(device, info);
    if (!gpu_buffer) {
        err_report(err, "%s", SDL_GetError());
    } else {
        SDL_SetGPUBufferName(device, gpu_buffer, str_to_c(scratch, name));
    }

    arena_release(scratch);
    scope_close(scope, "Create buffer '%.*s'", SF(name));
    return gpu_buffer;
}

SDL_GPUTransferBuffer *sdl_create_gpu_transfer_buffer(ErrorContext *err,
                                                      SDL_GPUDevice *device,
                                                      SDL_GPUTransferBufferCreateInfo *info) {
    if (!device) return nullptr;
    Scope scope = scope_open(err);

    SDL_GPUTransferBuffer *buffer = SDL_CreateGPUTransferBuffer(device, info);
    if (!buffer) {
        err_report(err, "%s", SDL_GetError());
    }

    scope_close(scope, "Create GPU transfer buffer");
    return buffer;
}

void init_vertex_buffers(ErrorContext *err, App *app) {
    Scope scope = scope_open(err);

    SDL_GPUBufferCreateInfo vert_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(Vertex) * MAX_VERTEX_COUNT,
    };
    app->vertex_buffer =
        sdl_create_gpu_buffer(err, app->device, &vert_info, S("THE vertex buffer"));

    SDL_GPUBufferCreateInfo index_info = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = sizeof(u16) * MAX_INDEX_COUNT,
    };
    app->index_buffer = sdl_create_gpu_buffer(err, app->device, &index_info, S("THE index buffer"));

    SDL_GPUTransferBufferCreateInfo transfer_buffer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (sizeof(Vertex) * MAX_VERTEX_COUNT) + (sizeof(u16) * MAX_INDEX_COUNT),
    };
    app->vertex_transfer_buffer =
        sdl_create_gpu_transfer_buffer(err, app->device, &transfer_buffer_info);

    scope_close(scope, "Init vertex+index buffers");
}

void make_and_upload_icon_atlas(ErrorContext *err, App *app, SDL_GPUCommandBuffer *command_buffer) {
    if (!command_buffer) return;
    Scope scope = scope_open(err);
    Arena *scratch = arena_acquire();

    FileDef *file = &app->session->file;
    Arr_CPUTexture icon_textures = arena_push_arr(scratch, CPUTexture, file->segments.count);
    for (u64 i = 0; i < file->segments.count; i++) {
        A(icon_textures, i) = A(file->segments, i).icon_texture;
    }
    app->icon_atlas = make_and_upload_atlas(err, app->session_arena, app->device, command_buffer,
                                            app->clear_icon_pipeline, S("Icon atlas"),
                                            icon_textures, (SizePX){2048, 2048}, FilterType_Linear);

    arena_release(scratch);
    scope_close(scope, "Make and upload icon atlas");
}

void init_render_pipelines(ErrorContext *err, App *app) {
    // Load shaders
    app->vert_shader = load_shader(err, app->device, OS_SHADERS.vert_shader, ShaderType_Vertex);
    app->icon_frag_shader =
        load_shader(err, app->device, OS_SHADERS.frag_icon_shader, ShaderType_Fragment);
    app->glyph_frag_shader =
        load_shader(err, app->device, OS_SHADERS.frag_glyph_shader, ShaderType_Fragment);
    if (err_occurred(err)) return;

    if (SDL_WindowSupportsGPUPresentMode(app->device, app->window, SDL_GPU_PRESENTMODE_MAILBOX)) {
        SDL_SetGPUSwapchainParameters(app->device, app->window,
                                      SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
                                      SDL_GPU_PRESENTMODE_MAILBOX);
    } else {
        SDL_SetGPUSwapchainParameters(app->device, app->window,
                                      SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
                                      SDL_GPU_PRESENTMODE_VSYNC);
    }
    SDL_GPUTextureFormat swapchain_format =
        SDL_GetGPUSwapchainTextureFormat(app->device, app->window);
    app->icon_pipeline =
        make_render_pipeline(err, app->device, app->window, app->vert_shader, app->icon_frag_shader,
                             swapchain_format, BlendType_Over);
    app->glyph_pipeline =
        make_render_pipeline(err, app->device, app->window, app->vert_shader,
                             app->glyph_frag_shader, swapchain_format, BlendType_Over);
    app->clear_icon_pipeline =
        make_render_pipeline(err, app->device, app->window, app->vert_shader, app->icon_frag_shader,
                             SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, BlendType_None);
    app->clear_glyph_pipeline =
        make_render_pipeline(err, app->device, app->window, app->vert_shader, app->icon_frag_shader,
                             SDL_GPU_TEXTUREFORMAT_R8_UNORM, BlendType_None);
}

SDL_GPUCommandBuffer *sdl_acquire_gpu_command_buffer(ErrorContext *err, SDL_GPUDevice *device) {
    Scope scope = scope_open(err);

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
    if (!command_buffer) {
        err_report(err, "%s", SDL_GetError());
    }

    scope_close(scope, "Acquire GPU command buffer");
    return command_buffer;
}

void sdl_submit_gpu_command_buffer(SDL_GPUCommandBuffer *command_buffer) {
    if (command_buffer) SDL_SubmitGPUCommandBuffer(command_buffer);
}

void init_render_buffers(ErrorContext *err, App *app) {
    Scope scope = scope_open(err);

    SDL_GPUCommandBuffer *command_buffer = sdl_acquire_gpu_command_buffer(err, app->device);
    init_vertex_buffers(err, app);
    make_and_upload_icon_atlas(err, app, command_buffer);
    app->glyph_atlas = make_and_upload_glyph_atlas(err, app->app_arena, app->device, command_buffer,
                                                   app->clear_glyph_pipeline, app->freetype,
                                                   app->font_file, FONT_SIZE_PX);

    sdl_submit_gpu_command_buffer(command_buffer);
    scope_close(scope, "Initialize render buffers");
}

void init_renderer(ErrorContext *err, App *app) {
    Scope scope = scope_open(err);

    if (FT_Init_FreeType(&app->freetype) != FT_Err_Ok) {
        err_report(err, "Failed to initialize freetype");
    }
    app->font_file = os_read_file(err, app->app_arena, FONT_PATH);

    app->device = SDL_CreateGPUDevice(OS_SHADERS.format, RENDERER_DEBUG_MODE_ENABLED, nullptr);
    if (!app->device) {
        err_report(err, "%s", SDL_GetError());
    } else {
        // ... or should  I make all renderer initialization functions invariant to nullptr
        // SDL_GPUDevice?
        SDL_ClaimWindowForGPUDevice(app->device, app->window);
        init_render_pipelines(err, app);
        init_render_buffers(err, app);
    }

    scope_close(scope, "Initialize renderer");
}

void do_geometry_upload_pass(App *app, SDL_GPUCommandBuffer *command_buffer, Mesh *mesh) {
    u64 vertex_data_size = mesh->vertices.count * sizeof(A(mesh->vertices, 0));
    u64 index_data_size = mesh->indices.count * sizeof(A(mesh->indices, 0));

    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(command_buffer);

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
        .offset = (u32)((u64)mesh->indices.ptr - (u64)mesh->vertices.ptr),
        .size = (u32)index_data_size,
    };
    SDL_UploadToGPUBuffer(pass, &index_src, &index_dest, true);

    SDL_EndGPUCopyPass(pass);
}

void render_main_color_pass(App *app,
                            SDL_GPUCommandBuffer *command_buffer,
                            SDL_GPUTexture *swapchain_texture,
                            u64 icon_quad_count,
                            u64 glyph_quad_count) {
    SDL_GPUColorTargetInfo color_target_infos[] = {{
        .texture = swapchain_texture,
        .clear_color = {0.f, 0.f, 0.f, 1.f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    }};
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command_buffer, color_target_infos,
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

void render(App *app) {
    Arena *frame_arena = arena_acquire();
    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(app->device);

    SDL_GPUTexture *texture = nullptr;
    u32 width = 0;
    u32 height = 0;
    SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, app->window, &texture, &width, &height);
    if (texture) {  // Apparently can be null if window is minimized
        SizePX window_size = {.w = (u16)width, .h = (u16)height};

        // TODO: switch to new layout
        UI_Box *box = build_ui(frame_arena, app->session, window_size);
        layout_ui(box);

        void *transfer_data =
            (Vertex *)SDL_MapGPUTransferBuffer(app->device, app->vertex_transfer_buffer, true);
        Mesh mesh = {};
        mesh.vertices = (FixedVec_Vertex){
            .ptr = transfer_data,
            .capacity = MAX_VERTEX_COUNT,
        };
        mesh.indices = (FixedVec_u16){
            .ptr = (u16 *)((Vertex *)transfer_data + MAX_VERTEX_COUNT),
            .capacity = MAX_INDEX_COUNT,
        };

        u64 ui_quad_count = make_ui_mesh(frame_arena, window_size, box, &mesh);
        u64 icon_quad_count = ui_quad_count;
        u64 glyph_quad_count = 0;

        // TODO get rid of ui_quad_count
        // u64 icon_quad_count =
        //     make_icon_mesh(frame_arena, window_size, app->session, app->icon_atlas, &mesh);
        // u64 glyph_quad_count = make_glyph_mesh(frame_arena, window_size, app->font_file,
        //                                        &app->glyph_atlas, vec_arr(&app->typed_text),
        //                                        &mesh);

        for (u64 i = 0; i < mesh.vertices.count; i++) {
            A(mesh.vertices, i).y -= app->scroll * 0.1f;
        }

        SDL_UnmapGPUTransferBuffer(app->device, app->vertex_transfer_buffer);
        do_geometry_upload_pass(app, command_buffer, &mesh);
        render_main_color_pass(app, command_buffer, texture, icon_quad_count, glyph_quad_count);
    }

    SDL_SubmitGPUCommandBuffer(command_buffer);
    arena_release(frame_arena);
}

//
// ::Texture system
//

void clear_texture(ErrorContext *err,
                   SDL_GPUCommandBuffer *command_buffer,
                   SDL_GPUGraphicsPipeline *pipeline,
                   SDL_GPUTexture *texture) {
    SDL_GPUColorTargetInfo color_target_infos[] = {{
        .texture = texture,
        .clear_color = {0.f, 0.f, 0.f, 1.f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    }};
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command_buffer, color_target_infos,
                                                     c_arr_count(color_target_infos), nullptr);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_EndGPURenderPass(pass);
}

Atlas *init_atlas(ErrorContext *err,
                  Arena *arena,
                  SDL_GPUDevice *device,
                  Str name,
                  SizePX atlas_size,
                  SDL_GPUTextureFormat texture_format,
                  FilterType filter) {
    Atlas *atlas = arena_push(arena, Atlas);
    Arena *scratch = arena_acquire();

    // Allocate and clear GPU texture

    SDL_GPUTextureCreateInfo gpu_texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = texture_format,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = (u32)atlas_size.w,
        .height = (u32)atlas_size.h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &gpu_texture_info);
    if (!texture) {
        err_report(err, "%s", SDL_GetError());
    } else {
        char *name_cstr = str_to_c(scratch, name);
        SDL_SetGPUTextureName(device, texture, name_cstr);
    }

    // Make transfer buffer.
    // Note that we can't just size it to the size of the atlas due rect alignment bloating the
    // size.
    SDL_GPUTransferBufferCreateInfo transfer_buffer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = atlas_size.w * atlas_size.h / 2,
    };
    SDL_GPUTransferBuffer *transfer_buffer =
        SDL_CreateGPUTransferBuffer(device, &transfer_buffer_info);

    // Make sampler

    SDL_GPUSamplerCreateInfo sampler_info = {};
    switch (filter) {
    case FilterType_Linear: {
        sampler_info = (SDL_GPUSamplerCreateInfo){
            .min_filter = SDL_GPU_FILTER_LINEAR,
            .mag_filter = SDL_GPU_FILTER_LINEAR,
            .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        };
        break;
    }
    case FilterType_Nearest: {
        sampler_info = (SDL_GPUSamplerCreateInfo){
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

    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &sampler_info);

    // Init rect packer state
    Arr_stbrp_node packer_nodes = arena_push_arr(arena, stbrp_node, atlas_size.w);
    stbrp_init_target(&atlas->packer_ctx, (i32)atlas_size.w, (i32)atlas_size.h, packer_nodes.ptr,
                      (i32)packer_nodes.count);

    // Fill atlas descriptor
    atlas->size = atlas_size;
    atlas->texture = texture;
    atlas->sampler = sampler;
    atlas->transfer_buffer = transfer_buffer;
    atlas->transfer_buffer_size = transfer_buffer_info.size;
    atlas->packer_nodes = packer_nodes;

    arena_release(scratch);
    return atlas;
}

void pack_textures_into_existing_atlas(Atlas *atlas,
                                       SDL_GPUDevice *device,
                                       SDL_GPUCommandBuffer *command_buffer,
                                       Arr_CPUTexture textures) {
    Arena *scratch = arena_acquire();

    //
    // Compute packing (on top of existing packing skyline)
    //

    Arr_stbrp_rect packer_rects = arena_push_arr(scratch, stbrp_rect, textures.count);
    for (u64 i = 0; i < textures.count; i++) {
        A(packer_rects, i).id = (i32)i;
        A(packer_rects, i).w = A(textures, i).dims.w + 2;
        A(packer_rects, i).h = A(textures, i).dims.h + 2;
    }

    log_assert(stbrp_pack_rects(&atlas->packer_ctx, packer_rects.ptr, (i32)packer_rects.count) ==
               1);

    // TODO use only 1px of padding, not 2px
    Arr_RectPX placements = arena_push_arr(scratch, RectPX, textures.count);
    for (u64 i = 0; i < textures.count; i++) {
        if (A(textures, i).dims.w > 0 && A(textures, i).dims.h > 0) {
            A(placements, i).x = (u16)A(packer_rects, i).x + 1;
            A(placements, i).y = (u16)A(packer_rects, i).y + 1;
            A(placements, i).w = A(textures, i).dims.w;
            A(placements, i).h = A(textures, i).dims.h;
        }
    }

    //
    // Pack textures into transfer buffer
    //

    void *ptr = SDL_MapGPUTransferBuffer(device, atlas->transfer_buffer, true);
    Arr_u8 arr = {.ptr = ptr, .count = atlas->transfer_buffer_size};
    Packer packer = packer_from_arr(arr);

    Vec_u32 offsets = {};
    vec_prealloc(scratch, &offsets, textures.count);

    for (u64 i = 0; i < textures.count; i++) {
        Opt_u64 offset = packer_try_push(&packer, A(textures, i).buffer, 512);
        log_assert(offset.present);  // TODO
        vec_push(scratch, &offsets, (u32)offset.opt);
    }
    SDL_UnmapGPUTransferBuffer(device, atlas->transfer_buffer);

    //
    // Upload textures
    //

    // TODO coalesce atlas-related copy and render passes
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);

    for (u64 i = 0; i < offsets.count; i++) {
        if (A(textures, i).dims.w > 0 && A(textures, i).dims.h > 0) {
            SDL_GPUTextureTransferInfo src = {
                .transfer_buffer = atlas->transfer_buffer,
                .offset = A(offsets, i),
            };
            SDL_GPUTextureRegion dest = {
                .texture = atlas->texture,
                .mip_level = 0,
                .layer = 0,
                .x = (u32)A(placements, i).x,
                .y = (u32)A(placements, i).y,
                .z = 0,
                .w = (u32)A(placements, i).w,
                .h = (u32)A(placements, i).h,
                .d = 1,
            };
            SDL_UploadToGPUTexture(copy_pass, &src, &dest, false);
        }
    }

    SDL_EndGPUCopyPass(copy_pass);
    arena_release(scratch);
}

TextureSystem *tex_init(ErrorContext *err, Arena *arena, SDL_GPUDevice *device) {
    TextureSystem *ctx = arena_push(arena, TextureSystem);
    ctx->icon_atlas = init_atlas(err, arena, device, S("Icon Atlas"), (SizePX){2048, 2048},
                                 SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, FilterType_Linear);
    ctx->glyph_atlas = init_atlas(err, arena, device, S("Glyph Atlas"), (SizePX){1024, 1024},
                                  SDL_GPU_TEXTUREFORMAT_R8_UNORM, FilterType_Nearest);
    return ctx;
}

void tex_request(TextureSystem *ctx, CPUTexture *texture, RectF where) {}

//
// ::Main
//

SDL_HitTestResult hittest_callback(SDL_Window *window, const SDL_Point *point, void *data) {
    // Would expand the resize radius if I could, but doesn't appear to work on
    // macOS
    return SDL_HITTEST_DRAGGABLE;
}

SDL_Window *sdl_create_window(ErrorContext *err,
                              Str name,
                              SizePX size,
                              SizePX min_size,
                              SDL_WindowFlags flags) {
    Scope scope = scope_open(err);
    Arena *scratch = arena_acquire();

    char *name_cstr = str_to_c(scratch, name);
    SDL_Window *window = SDL_CreateWindow(name_cstr, size.w, size.h, flags);
    if (!window) {
        err_report(err, "%s", SDL_GetError());
    } else {
        if (!SDL_SetWindowMinimumSize(window, min_size.w, min_size.h)) {
            err_report(err, "%s", SDL_GetError());
        }
        if (!SDL_SetWindowHitTest(window, hittest_callback, nullptr)) {
            err_report(err, "%s", SDL_GetError());
        }
    }

    arena_release(scratch);
    scope_close(scope, "Create window");
    return window;
}

SDL_Window *init_window(ErrorContext *err) {
    Scope scope = scope_open(err);

    if (!SDL_SetAppMetadata("Blitter", "0.0.1", nullptr)) {
        err_report(err, "%s", SDL_GetError());
    }

    SDL_WindowFlags window_flags =
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window *window =
        sdl_create_window(err, S("Blitter"), DEFAULT_WINDOW_SIZE, MIN_WINDOW_SIZE, window_flags);

    scope_close(scope, "Initialize window");
    return window;
}

Session *make_session(ErrorContext *err, Arena *arena, App *app, Str path) {
    Session *session = arena_push(arena, Session);
    load_livesplit_lss(err, arena, path, &session->file);
    return session;
}

App *init_app(ErrorContext *err, Str path) {
    Arena *root_arena = arena_acquire();
    App *app = arena_push(root_arena, App);
    app->app_arena = root_arena;

    app->window = init_window(err);

    app->session_arena = arena_acquire();
    app->session = make_session(err, app->session_arena, app, path);
    if (!err_occurred(err)) {
        init_renderer(err, app);
    }

    return app;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    thread_init();
    if (argc < 2) {
        log_info("Usage: blitter <path-to-splits-file>");
        return SDL_APP_FAILURE;
    }

    Arena *err_arena = arena_acquire();
    ErrorContext err_base = {.arena = err_arena};
    ErrorContext *err = &err_base;

    Scope scope = scope_open(err);
    *appstate = init_app(err, str_from_c(argv[1]));
    scope_close(scope, "Init app");

    bool did_error = err_occurred(err);
    if (did_error) {
        err_log(err);
    }

    arena_release(err_arena);
    return did_error ? SDL_APP_FAILURE : SDL_APP_CONTINUE;
}

// TODO audit session arena/resource/memory usage
// void try_load_new_session(App *app, Str lss_path) {
//     ErrorContext err_base = {.arena = arena_acquire()};
//     ErrorContext *err = &err_base;
//     defer(arena_release(err_base.arena));
//
//     Arena *session_arena = arena_acquire();
//     Session *session = make_session(err, session_arena, app, lss_path);
//     if (err_occurred(err)) {
//         err_log(err);
//         arena_release(session_arena);
//         return;
//     }
//
//     if (app->session_arena != nullptr) {
//         arena_release(app->session_arena);
//     }
//     app->session_arena = session_arena;
//     app->session = session;
// }

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    App *app = (App *)appstate;

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
            timer_apply_action(app->app_arena, app->session, TimerAction_Split, t);
            break;
        }
        case SDLK_UP: {
            timer_apply_action(app->app_arena, app->session, TimerAction_UndoSplit, t);
            break;
        }
        case SDLK_D: {
            timer_apply_action(app->app_arena, app->session, TimerAction_DeleteSplit, t);
            break;
        }
        case SDLK_P: {
            timer_apply_action(app->app_arena, app->session, TimerAction_Pause, t);
            break;
        }
        case SDLK_BACKSPACE: {
            if (app->insert_mode_enabled) {
                if (app->typed_text.count > 0) {
                    vec_pop(&app->typed_text);
                }
            } else {
                timer_apply_action(app->app_arena, app->session, TimerAction_ResetAndSave, t);
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
            char *clipboard_cstr = SDL_GetClipboardText();
            Str clipboard = str_trim(str_from_c(clipboard_cstr));
            if (!is_empty(clipboard)) {  // Empty iff SDL failed to allocate it
                // try_load_new_session(app, clipboard);
            }
            SDL_free(clipboard_cstr);
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

SDL_AppResult SDL_AppIterate(void *appstate) {
    App *app = (App *)appstate;
    render(app);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    // Just let OS clean up everything
}
