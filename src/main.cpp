#include "base.hpp"

#include <stdarg.h>

#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3_ttf/SDL_ttf.h>

extern "C" {
#include <xao.h>
}
#include <yyjson.h>
#include <kb_text_shape.h>
#include <stb_rect_pack.h>

//
// MARK:Timer
//

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

struct SplitRecord {
    u64 attempt_num;
    Arr<Opt<Duration>> splits;
};

struct SegmentDef {
    Str name;
    Arr<u8> icon; // Icon in PNG format
};

struct FileDef {
    Str game_name;
    Str category_name;
    u64 total_attempts;
    u64 completed_attempts;
    Arr<SegmentDef> segments;
    SplitRecord personal_best;
    Arr<Opt<Duration>> golds;
    Arr<SDL_Surface *> icons;
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
    FileDef *file;
    Timer *timer;
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

Arr<SegSummary> calc_seg_summary(Arena *arena, Session *session) {
    Timer *timer = session->timer;
    FileDef *file = session->file;

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
        summary[i].is_new_gold = prev_gold.present && live_seg.present && \
                                 live_seg.opt < prev_gold.opt;
    }

    return summary;
}

Str format_duration(Arena *arena, Duration duration, u32 ms_digits, bool show_plus_prefix) {
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
        result = str_format(arena, "%.*s%" PRIu64 ".%03" PRIu64,
                SF(sign_str), seconds, milliseconds);
    } else if (days == 0 && hours == 0) {
        result = str_format(arena, "%.*s%" PRIu64 ":%02" PRIu64 ".%03" PRIu64,
                SF(sign_str), minutes, seconds, milliseconds);
    } else if (days == 0) {
        result = str_format(arena, "%.*s%" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%03" PRIu64,
                SF(sign_str), hours, minutes, seconds, milliseconds);
    } else {
        result = str_format(arena, "%.*s%" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%03" PRIu64,
                SF(sign_str), days, hours, minutes, seconds, milliseconds);
    }

    return arr_slice(result, 0, result.count - (3 - ms_digits));
}

enum class TimerAction {
    Split,
    UndoSplit,
    DeleteSplit,
    ResetAndSave,
    ResetAndDelete,
    Pause,
};

void timer_reset(Timer *timer) {
    timer->mode = TimerMode::Init;
    vec_reset(&timer->live_splits);
}

Duration timer_get_elapsed(Timer *timer, Instant event_time) {
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

void timer_apply_action_init(Arena *arena, Session *session, TimerAction action, Instant t) {
    switch (action) {
    case TimerAction::Split: {
        session->timer->mode = TimerMode::Running;
        session->timer->start_time = t;
        break;
    }
    default: {}
    }
}

void timer_apply_action_running(Arena *arena, Session *session, TimerAction action, Instant t) {
    Timer *timer = session->timer;
    FileDef *file = session->file;

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

void timer_apply_action_paused(Arena *arena, Session *session, TimerAction action, Instant t) {
    Timer *timer = session->timer;
    FileDef *file = session->file;

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
    default: {}
    }
}

void timer_apply_action_finished(Arena *arena, Session *session, TimerAction action, Instant t) {
    Timer *timer = session->timer;
    FileDef *file = session->file;

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
    default: {}
    }
}

void timer_apply_action(Arena *arena, Session *session, TimerAction action, Instant t) {
    switch (session->timer->mode) {
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

bool eq(xao_Value v, const char *s) {
    u64 size = (u64)v.end - (u64)v.start;
    return size == SDL_strlen(s) && SDL_memcmp(v.start, s, size) == 0;
}

Str xml_str(xao_Value v) {
    return { .ptr = (u8 *)v.start, .count = (u64)v.end - (u64)v.start };
}

Str xml_inner(xao_Reader *r, xao_Value outer) {
    xao_Value inner = {};
    xao_iter_content(r, outer, &inner);
    return xml_str(inner);
}

// TODO I've been neglectful of `const`ness in my APIs, but obviously this
// should be const
u8 PNG_HEADER[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };

Arr<u8> decode_icon_base64_to_png(Arena *arena, ErrorContext *err, Str icon_base64) {
    err_scope(err, "Decode icon base64 to PNG");

    Arr<u8> icon_bin = base64_decode(arena, err, icon_base64);
    Opt<u64> png_idx = str_find(icon_bin, A(PNG_HEADER));
    if (!png_idx.present) {
        err_report(err, "PNG image not detected");
        return {};
    }

    return arr_slice(icon_bin, png_idx.opt, icon_bin.count);
}

Arr<SegmentDef> parse_livesplit_segments(Arena *arena, ErrorContext *err, xao_Reader *r, xao_Value segments_tag) {
    err_scope(err, "Parse LiveSplit LSS segments");

    Vec<SegmentDef> segments = {};
    xao_Value seg_tag = {};
    while (xao_iter_tags(r, segments_tag, &seg_tag)) {
        SegmentDef *seg = vec_push_zero(arena, &segments);
        xao_Value attr_tag = {};
        while (xao_iter_tags(r, seg_tag, &attr_tag)) {
            if (eq(attr_tag, "Name")) {
                seg->name = str_clone(arena, xml_inner(r, attr_tag));
                if (str_is_empty(seg->name)) {
                    err_report(err, "Segment %" PRIu64 " has empty name", segments.count + 1);
                    return {};
                }
            } else if (eq(attr_tag, "Icon")) {
                err_scope(err, "Decode icon for segment '%.*s'", SF(seg->name));
                Str base64 = xml_inner(r, attr_tag);
                if (!str_is_empty(base64)) {
                    seg->icon = decode_icon_base64_to_png(arena, err, base64);
                }
            }
        }
    }
    return vec_arr(&segments);
}

// TODO avoid leaking stuff on error
void load_timer_textures(Arena *arena, ErrorContext *err, FileDef *file) {
    err_scope(err, "Load segment icon textures");

    Arr<SDL_Surface *> icons = arena_push_arr<SDL_Surface *>(arena, file->segments.count);
    for (u64 i = 0; i < file->segments.count; i++) {
        if (arr_is_empty(file->segments[i].icon)) {
            continue;
        }

        err_scope(err, "Load icon texture for segment '%.*s'", SF(file->segments[i].name));

        SDL_IOStream *png_stream = try_sdl(err, SDL_IOFromMem(file->segments[i].icon.ptr, file->segments[i].icon.count));
        defer(SDL_CloseIO(png_stream));

        icons[i] = try_sdl(err, SDL_LoadPNG_IO(png_stream, false));
    }
    file->icons = icons;
}

void parse_livesplit_lss(Arena *arena, ErrorContext *err, FileDef *file, Str xml) {
    err_scope(err, "Parse LiveSplit LSS");

    if (!str_is_valid_utf8(xml)) {
        err_report(err, "Invalid UTF-8");
        return;
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
        err_report(err, "Failed to parse LSS XML: %s", r.error);
        return;
    }

    // Basic validation
    if (arr_is_empty(file->segments)) {
        err_report(err, "No segments found");
        return;
    }
    for (u64 i = 0; i < file->segments.count; i++) {
        if (str_is_empty(file->segments[i].name)) {
            err_report(err, "Segment %" PRIu64 " has no name", i + 1);
            return;
        }
    }
    if (str_is_empty(file->game_name)) {
        err_report(err, "Empty game name");
        return;
    }
}

FileDef *load_livesplit_lss(Arena *arena, ErrorContext *err, Str lss_path) {
    err_scope(err, "Load LiveSplit LSS file '%.*s'", SF(lss_path));

    FileDef *file = arena_push<FileDef>(arena);

    Arena *scratch = arena_acquire();
    defer(arena_release(scratch));

    // TODO arena allocate
    size_t size = 0;
    char *lss_path_cstr = str_to_c(scratch, lss_path);
    void *lss_buf = try_sdl(err, file, SDL_LoadFile(lss_path_cstr, &size));
    defer(SDL_free(lss_buf));

    Str xml = { .ptr = (u8 *)lss_buf, .count = (u64)size };

    parse_livesplit_lss(arena, err, file, xml);
    load_timer_textures(arena, err, file);

    return file;
}

//
// Main
//

struct PxSize {
    u16 w, h;
};

struct PxPos {
    u16 x, y;
};

struct PxRect {
    u16 x, y;
    u16 w, h;
};

constexpr PxSize DEFAULT_WINDOW_SIZE = { .w = 360, .h = 600 };
constexpr PxSize MIN_WINDOW_SIZE = { .w = 200, .h = 100 };

struct Atlas {
    PxSize size;
    SDL_GPUTexture *texture;
    SDL_GPUSampler *sampler;
    Arr<PxRect> placements;
};

struct PosTexVertex {
    float x, y, z;
    float u, v;
};

struct App {
    Arena *app_arena; // Lives for duration of application
    SDL_Window* window;
    TTF_Font *font_small;
    TTF_Font *font_medium;
    TTF_Font *font_large;
    TTF_TextEngine *text_engine;
    SDL_Keycode prev_keys;

    Arena *session_arena;
    Session *session; // Nullable

    SDL_GPUDevice *device;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUTransferBuffer *vertex_transfer_buffer;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
    Atlas *atlas;
};

void init_text(ErrorContext *err, App *app) {
    try_sdl(err, TTF_Init());

    const char *font_path = "data/Roboto-Medium.ttf";
    size_t font_file_size = 0;
    void *font_buf = nullptr;
    {
        err_scope(err, "Load font '%s", font_path);
        // TODO arena allocate
        font_buf = try_sdl(err, SDL_LoadFile(font_path, &font_file_size));
    }
    Arr<u8> ttf = { .ptr = (u8 *)font_buf, .count = (u64)font_file_size };

    /* Open the font */
    app->font_small = try_sdl(err, TTF_OpenFontIO(SDL_IOFromConstMem(ttf.ptr, ttf.count), true, 15.0f));
    app->font_medium = try_sdl(err, TTF_OpenFontIO(SDL_IOFromConstMem(ttf.ptr, ttf.count), true, 18.0f));
    app->font_large = try_sdl(err, TTF_OpenFontIO(SDL_IOFromConstMem(ttf.ptr, ttf.count), true, 64.0f));

    /* Create the text engine */
    // app->text_engine = try_sdl(err, TTF_CreateRendererTextEngine(app->renderer));
}

SDL_HitTestResult hittest_callback(SDL_Window* window, const SDL_Point *point, void *data) {
    // Would expand the resize radius if I could, but doesn't appear to work on macOS
    return SDL_HITTEST_DRAGGABLE;
}

void init_window(ErrorContext *err, App *app) {
    err_scope(err, "Initialize window");

    try_sdl(err, SDL_SetAppMetadata("Blitter", "0.0.1", nullptr));

    SDL_PropertiesID props = try_sdl(err, SDL_CreateProperties());
    defer(SDL_DestroyProperties(props));

    try_sdl(err, SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Blitter"));
    try_sdl(err, SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true));
    try_sdl(err, SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true));
    try_sdl(err, SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, DEFAULT_WINDOW_SIZE.w));
    try_sdl(err, SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, DEFAULT_WINDOW_SIZE.h));

    app->window = try_sdl(err, SDL_CreateWindowWithProperties(props));
    try_sdl(err, SDL_SetWindowMinimumSize(app->window, MIN_WINDOW_SIZE.w, MIN_WINDOW_SIZE.h));
    try_sdl(err, SDL_SetWindowHitTest(app->window, hittest_callback, nullptr));
}

Session *create_session(ErrorContext *err, Arena *arena, App *app, Str path) {
    Session *session = arena_push<Session>(arena);

    session->file = load_livesplit_lss(arena, err, path);
    session->timer = arena_push<Timer>(arena);

    return session;
}

void re_init(ErrorContext *err, App *app);
void re_render(App *app);

App *init_app(ErrorContext *err) {
    if (g_argv.count < 2) {
        log_fatal("Usage: blitter <path-to-splits-file>");
    }
    Str path = str_from_c(g_argv[1]);

    Arena *root_arena = arena_acquire();
    App *app = arena_push<App>(root_arena);
    app->app_arena = root_arena;

    init_window(err, app);
    // init_text(err, app);

    app->session_arena = arena_acquire();
    app->session = create_session(err, app->session_arena, app, path);
    if (err_occurred(err)) {
        return app;
    }

    re_init(err, app);

    return app;
}

void shape_test();

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    thread_init(argc, argv);

    ErrorContext err_base = { .arena = arena_acquire() };
    ErrorContext *err = &err_base;
    defer(arena_release(err_base.arena));

    App *app = init_app(err);
    if (err_occurred(err)) {
        err_log(err);
        return SDL_APP_FAILURE;
    }
    *appstate = app;

    // TODO remove
    // shape_test();

    return SDL_APP_CONTINUE;
}

void try_load_new_session(App *app, Str lss_path) {
    ErrorContext err_base = { .arena = arena_acquire() };
    ErrorContext *err = &err_base;
    defer(arena_release(err_base.arena));

    Arena *session_arena = arena_acquire();
    Session *session = create_session(err, session_arena, app, lss_path);
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
            timer_apply_action(app->app_arena, app->session, TimerAction::ResetAndSave, t);
            break;
        }
        }
    }

    if (event->common.type == SDL_EVENT_KEY_UP) {
        app->prev_keys &= ~event->key.key;
    }

    if (event->common.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_RIGHT) {
        if (SDL_HasClipboardText()) {
            char *clipboard_cstr = SDL_GetClipboardText();
            defer(SDL_free(clipboard_cstr));
            Str clipboard = str_trim(str_from_c(clipboard_cstr));
            if (!str_is_empty(clipboard)) { // Empty iff SDL failed to allocate it
                try_load_new_session(app, clipboard);
            }
        }
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    // Just let OS clean up everything
}

//
// MARK:UI
//

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

    // Text stuff
    TTF_Text *text_obj;
    f32 scale;

    // Texture stuff
    u64 texture_idx;

    SDL_FColor color;

    // Hm, padding determined by just making more boxes for now?
    Vec<Box*> children;

    // AABB of self and children - lazily computed
    Opt<PxSize> bbox;
};

Box *make_text_box(Arena *arena, TTF_TextEngine *engine, TTF_Font *font, Str content, SDL_FColor color) {
    Box *box = arena_push<Box>(arena);

    box->type = BoxType::Text;
    // const char *content_cstr = str_is_empty(content) ? "" : (const char *)content.ptr;
    // Zero length actually means "treat string as null terminated"
    // box->text_obj = TTF_CreateText(engine, font, content_cstr, content.count);
    // TTF_SetTextColorFloat(box->text_obj, color.r, color.g, color.b, color.a);

    return box;
}

Box *make_empty_box(Arena *arena, PxSize size) {
    Box *box = arena_push<Box>(arena);
    box->type = BoxType::Empty;
    box->bbox = some(size);
    return box;
}

Box *make_texture_box(Arena *arena, u64 texture_idx, PxSize size) {
    Box *box = arena_push<Box>(arena);
    box->type = BoxType::Texture;
    box->bbox = some(size);
    box->texture_idx = texture_idx;
    return box;
}

Box *make_solid_color_box(Arena *arena, SDL_FColor color, PxSize size) {
    Box *box = arena_push<Box>(arena);
    box->type = BoxType::SolidColor;
    box->bbox = some(size);
    box->color = color;
    return box;
}

PxSize compute_box_bbox(Box *box);

PxSize compute_box_bbox_uncached(Box *box) {
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
        i32 width = 0;
        i32 height = 0;
        TTF_GetTextSize(box->text_obj, &width, &height);
        return { .w = (u16)width, .h = (u16)height };
    }
    case BoxType::Texture: {
        return box->bbox.opt;
    }
    }
}

PxSize compute_box_bbox(Box *box) {
    if (box->bbox.present) {
        return box->bbox.opt;
    }
    PxSize bbox = compute_box_bbox_uncached(box);
    box->bbox = some(bbox);
    return bbox;
}

Box *pad_box_left(Arena *arena, Box *box, u16 pad) {
    PxSize bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, { .w = pad, .h = bbox.h });

    Box *parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::LeftToRightStack;
    vec_push(arena, &parent_box->children, pad_box);
    vec_push(arena, &parent_box->children, box);

    return parent_box;
}

Box *pad_box_right(Arena *arena, Box *box, u16 pad) {
    PxSize bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, { .w = pad, .h = bbox.h });

    Box *parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::LeftToRightStack;
    vec_push(arena, &parent_box->children, box);
    vec_push(arena, &parent_box->children, pad_box);

    return parent_box;
}

Box *pad_box_top(Arena *arena, Box *box, u16 pad) {
    PxSize bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, { .w = bbox.w, .h = pad });

    Box *parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::TopToBottomStack;
    vec_push(arena, &parent_box->children, pad_box);
    vec_push(arena, &parent_box->children, box);

    return parent_box;
}

Box *pad_box_bottom(Arena *arena, Box *box, u16 pad) {
    PxSize bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, { .w = bbox.w, .h = pad });

    Box *parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::TopToBottomStack;
    vec_push(arena, &parent_box->children, box);
    vec_push(arena, &parent_box->children, pad_box);

    return parent_box;
}

Box *align_box_center_horiz(Arena *arena, Box *box, u16 width) {
    PxSize bbox = compute_box_bbox(box);
    width = max(width, bbox.w);
    u16 left_pad = (width - bbox.w) / 2;
    u16 right_pad = width - bbox.w - left_pad;

    Box *left_pad_box = make_empty_box(arena, {left_pad, bbox.h});
    Box *right_pad_box = make_empty_box(arena, {right_pad, bbox.h});

    Box *parent = arena_push<Box>(arena);
    parent->type = BoxType::LeftToRightStack;
    vec_push(arena, &parent->children, left_pad_box);
    vec_push(arena, &parent->children, box);
    vec_push(arena, &parent->children, right_pad_box);

    return parent;
}

Box *align_box_center_vert(Arena *arena, Box *box, u16 height) {
    PxSize bbox = compute_box_bbox(box);
    height = max(height, bbox.h);
    u16 top_pad = (height - bbox.h) / 2;
    u16 bottom_pad = height - bbox.h - top_pad;

    Box *top_pad_box = make_empty_box(arena, {bbox.w, top_pad});
    Box *bottom_pad_box = make_empty_box(arena, {bbox.w, bottom_pad});

    Box *parent = arena_push<Box>(arena);
    parent->type = BoxType::TopToBottomStack;
    vec_push(arena, &parent->children, top_pad_box);
    vec_push(arena, &parent->children, box);
    vec_push(arena, &parent->children, bottom_pad_box);

    return parent;
}

Box *prerender_segment(Arena *arena, App *app, u16 width, u64 idx) {
    constexpr u16 ICON_INNER = 36;
    constexpr u16 ICON_OUTER = 44;
    Box *icon = nullptr;

    // TODO handle empty icons
    icon = make_texture_box(arena, idx, {.w = ICON_INNER, .h = ICON_INNER});
    // } else {
    //     icon = make_empty_box(arena, ICON_INNER, ICON_INNER);
    // }
    icon = align_box_center_horiz(arena, icon, ICON_OUTER);
    icon = align_box_center_vert(arena, icon, ICON_OUTER);

    SDL_FColor text_color = { .r = 1.f, .g = 1.f, .b = 1.f, .a = 1.f };
    Box *pad = make_empty_box(arena, { .w = 10, .h = 0 });
    Box *title = make_text_box(arena, app->text_engine, app->font_medium, app->session->file->segments[idx].name, text_color);
    Box *title_centered = align_box_center_vert(arena, title, ICON_OUTER);

    Box *row_front = arena_push<Box>(arena);
    row_front->type = BoxType::LeftToRightStack;
    vec_push(arena, &row_front->children, icon);
    vec_push(arena, &row_front->children, pad);
    vec_push(arena, &row_front->children, title_centered);

    if (app->session->timer->mode == TimerMode::Running && idx == app->session->timer->live_splits.count) {
        PxSize row_front_bbox = compute_box_bbox(row_front);
        SDL_FColor bg_color = { .r = 0.f, .g = 0.3f, .b = 0.90f, .a = 1.f };
        PxSize row_back_size = { .w = width, .h = row_front_bbox.h };
        Box *row_back = make_solid_color_box(arena, bg_color, row_back_size);

        Box *row = arena_push<Box>(arena);
        row->type = BoxType::BackToFrontStack;
        vec_push(arena, &row->children, row_back);
        vec_push(arena, &row->children, row_front);

        return row;
    }

    return row_front;
}

Box *prerender_contents(Arena *arena, App *app, PxSize size) {
    SDL_FColor color = { .r = 1.f, .g = 1.f, .b = 1.f, .a = 1.f };

    Box *game_name = make_text_box(arena, app->text_engine, app->font_medium,
            app->session->file->game_name, color);
    Box *cat_name = make_text_box(arena, app->text_engine, app->font_medium,
            app->session->file->category_name, color);

    Box *game_name_centered = align_box_center_horiz(arena, game_name, size.w);
    Box *cat_name_centered = align_box_center_horiz(arena, cat_name, size.w);

    Box *top = arena_push<Box>(arena);
    top->type = BoxType::TopToBottomStack;
    vec_push(arena, &top->children, game_name_centered);
    vec_push(arena, &top->children, cat_name_centered);

    for (u64 i = 0; i < app->session->file->segments.count; i++) {
        if (!str_starts_with(app->session->file->segments[i].name, S("-"))) {
            Box *segment = prerender_segment(arena, app, size.w, i);
            vec_push(arena, &top->children, segment);
        }
    }

    Box *bottom = arena_push<Box>(arena);
    bottom->type = BoxType::TopToBottomStack;

    Duration t = timer_get_elapsed(app->session->timer, get_current_monotonic_time());
    Str t_str = format_duration(arena, t, 2, false);
    Box *curr_time = make_text_box(arena, app->text_engine, app->font_large, t_str, color);
    PxSize curr_time_bbox = compute_box_bbox(curr_time);
    Box *curr_time_aligned = pad_box_left(arena, curr_time, size.w - curr_time_bbox.w);

    vec_push(arena, &bottom->children, curr_time_aligned);

    // Put timer at bottom
    PxSize top_bbox = compute_box_bbox(top);
    PxSize bottom_bbox = compute_box_bbox(bottom);
    PxSize vsep_size = { .w = 0, .h = (u16)(size.h - top_bbox.h - bottom_bbox.h) };
    Box *vsep = make_empty_box(arena, vsep_size);

    Box *root = arena_push<Box>(arena);
    root->type = BoxType::TopToBottomStack;
    vec_push(arena, &root->children, top);
    vec_push(arena, &root->children, vsep);
    vec_push(arena, &root->children, bottom);

    return root;
}

Box *prerender(Arena *arena, App *app, PxSize window_size) {
    constexpr u16 PADDING = 10;
    PxSize content_size = {
        .w = (u16)(window_size.w - PADDING * 2),
        .h = (u16)(window_size.h - PADDING * 2),
    };
    Box *timer = prerender_contents(arena, app, content_size);
    timer = pad_box_left(arena, timer, PADDING);
    timer = pad_box_right(arena, timer, PADDING);
    timer = pad_box_top(arena, timer, PADDING);
    timer = pad_box_bottom(arena, timer, PADDING);
    return timer;
}

struct RenderMesh {
    Vec<PosTexVertex> vertices;
    Vec<u16> indices;
};

// TODO for pixel-perfect rendering, need to understand rounding/UV mapping
// w.r.t. pixel center better
void window_to_ndc(PosTexVertex *vertex, PxSize window_size) {
    vertex->x = (vertex->x / (f32)window_size.w) * 2.f - 1.f;
    vertex->y = -((vertex->y / (f32)window_size.h) * 2.f - 1.f);
}

void push_atlas_quad(Arena *arena, PxSize window_size, Atlas *atlas, RenderMesh *mesh, PxRect src, PxRect dst) {
    Arr<u16> indices = vec_extend_zero(arena, &mesh->indices, 6);
    indices[0] = (u16)(mesh->vertices.count + 0);
    indices[1] = (u16)(mesh->vertices.count + 1);
    indices[2] = (u16)(mesh->vertices.count + 2);
    indices[3] = (u16)(mesh->vertices.count + 2);
    indices[4] = (u16)(mesh->vertices.count + 1);
    indices[5] = (u16)(mesh->vertices.count + 3);

    Arr<PosTexVertex> vertices = vec_extend_zero(arena, &mesh->vertices, 4);
    // Top left
    vertices[0] = {
        .x = (f32)dst.x,
        .y = (f32)dst.y,
        .z = 0,
        .u = (f32)src.x / (f32)atlas->size.w,
        .v = (f32)src.y / (f32)atlas->size.h,
    };
    // Top right
    vertices[1] = {
        .x = (f32)(dst.x + dst.w),
        .y = (f32)dst.y,
        .z = 0,
        .u = (f32)(src.x + src.w) / (f32)atlas->size.w,
        .v = (f32)src.y / (f32)atlas->size.h,
    };
    // Bottom left
    vertices[2] = {
        .x = (f32)dst.x,
        .y = (f32)(dst.y + dst.h),
        .z = 0,
        .u = (f32)src.x / (f32)atlas->size.w,
        .v = (f32)(src.y + src.h) / (f32)atlas->size.h,
    };
    // Bottom right
    vertices[3] = {
        .x = (f32)(dst.x + dst.w),
        .y = (f32)(dst.y + dst.h),
        .z = 0,
        .u = (f32)(src.x + src.w) / (f32)atlas->size.w,
        .v = (f32)(src.y + src.h) / (f32)atlas->size.h,
    };

    // TODO less awkward way to do this?
    window_to_ndc(&vertices[0], window_size);
    window_to_ndc(&vertices[1], window_size);
    window_to_ndc(&vertices[2], window_size);
    window_to_ndc(&vertices[3], window_size);
}

void re_build_boxes_mesh(Arena *arena, PxSize window_size, Box *box, PxPos where, Atlas *atlas, RenderMesh *mesh) {
    switch (box->type) {
    case BoxType::Empty: {
        break;
    }
    case BoxType::LeftToRightStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            PxSize child_bbox = compute_box_bbox(box->children[i]);
            re_build_boxes_mesh(arena, window_size, box->children[i], where, atlas, mesh);
            where.x += child_bbox.w;
        }
        break;
    }
    case BoxType::TopToBottomStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            PxSize child_bbox = compute_box_bbox(box->children[i]);
            re_build_boxes_mesh(arena, window_size, box->children[i], where, atlas, mesh);
            where.y += child_bbox.h;
        }
        break;
    }
    case BoxType::BackToFrontStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            re_build_boxes_mesh(arena, window_size, box->children[i], where, atlas, mesh);
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
        push_atlas_quad(arena, window_size, atlas, mesh, src, dest);
        break;
    }
    case BoxType::SolidColor: {
        // SDL_SetRenderDrawColorFloat(app->renderer, box->color.r, box->color.g, box->color.b, box->color.a);
        // SDL_FRect r = { .x = where.x, .y = where.y, .w = box->width, .h = box->height };
        // SDL_RenderFillRect(app->renderer, &r);
        break;
    }
    }
}

void render(App *app) {
    // Frame arena
    Arena *frame_arena = arena_acquire();
    defer(arena_release(frame_arena));

}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App *app = (App *)appstate;
    re_render(app);
    return SDL_APP_CONTINUE;
}

//
// MARK:Font stuff
//

void shape_test() {
    kbts_shape_context *context = kbts_CreateShapeContext(0, 0);
    defer(kbts_DestroyShapeContext(context));
    kbts_ShapePushFontFromFile(context, "data/Roboto-Medium.ttf", 0);

    Str shape_text = S("Let's shape something!");

    kbts_ShapeBegin(context, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(context,
            (char *)shape_text.ptr, (i32)shape_text.count,
            KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
    kbts_ShapeEnd(context);

    // Layout runs naively left to right.
    kbts_run Run = {};
    int CursorX = 0, CursorY = 0;
    u64 run_idx = 0;
    while (kbts_ShapeRun(context, &Run))
    {
        log_info("Run idx = %" PRIu64, run_idx);
        u64 glyph_idx = 0;
        kbts_glyph *Glyph = nullptr;
        while (kbts_GlyphIteratorNext(&Run.Glyphs, &Glyph))
        {
            int GlyphX = CursorX + Glyph->OffsetX;
            int GlyphY = CursorY + Glyph->OffsetY;

            // DisplayGlyph(Glyph->Id, GlyphX, GlyphY);
            log_info("Display glyph: idx = %" PRIu64 ", id = %d, x = %d, y = %d\n", glyph_idx, Glyph->Id, GlyphX, GlyphY);

            CursorX += Glyph->AdvanceX;
            CursorY += Glyph->AdvanceY;
            glyph_idx++;
        }
        run_idx++;
    }
}

//
// MARK:Renderer
//

u64 sdl_surface_size(SDL_Surface *surface) {
    return (u64)surface->h * (u64)surface->pitch;
}

Atlas *re_pack_and_upload_textures(Arena *arena, SDL_GPUDevice *device, SDL_GPUCopyPass *pass, Arr<SDL_Surface *> textures, PxSize atlas_size) {
    log_assert(textures.count > 0);
    SDL_PixelFormat sdl_pixel_format = textures[0]->format;
    for (u64 i = 1; i < textures.count; i++) {
        log_assert(sdl_pixel_format == textures[i]->format);
    }

    Arena *scratch = arena_acquire();
    defer(arena_release(scratch));

    //
    // Compute atlas packing
    //

    stbrp_context packer_ctx = {};
    Arr<stbrp_node> packer_nodes = arena_push_arr<stbrp_node>(scratch, textures.count * 2 /* ?? */);
    stbrp_init_target(&packer_ctx, (i32)atlas_size.w, (i32)atlas_size.h, packer_nodes.ptr, (i32)packer_nodes.count);
    stbrp_setup_allow_out_of_mem(&packer_ctx, true);

    Arr<stbrp_rect> rects = arena_push_arr<stbrp_rect>(scratch, textures.count);
    for (u64 i = 0; i < textures.count; i++) {
        rects[i].id = (i32)i;
        rects[i].w = textures[i]->w;
        rects[i].h = textures[i]->h;
    }

    log_assert(stbrp_pack_rects(&packer_ctx, rects.ptr, (i32)rects.count) == 1);

    //
    // Pack textures into transfer buffer
    //

    // TODO reuse transfer buffer and/or destroy?
    SDL_GPUTransferBufferCreateInfo transfer_buffer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (u32)(atlas_size.w * atlas_size.h * 4),
    };
    SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_buffer_info);

    void *buf = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
    u64 offset = 0;
    for (u64 i = 0; i < textures.count; i++) {
        if (i > 0) {
            u64 prev_size = sdl_surface_size(textures[i - 1]);
            offset = align_to(offset + prev_size, 512);
        }
        u64 size = sdl_surface_size(textures[i]);
        SDL_memcpy((u8 *)buf + offset, textures[i]->pixels, size);
    }
    SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

    //
    // Upload textures
    //

    SDL_GPUTextureCreateInfo gpu_texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GetGPUTextureFormatFromPixelFormat(sdl_pixel_format),
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = (u32)atlas_size.w,
        .height = (u32)atlas_size.h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &gpu_texture_info);
    SDL_SetGPUTextureName(device, texture, "Segment icons");

    offset = 0;
    for (u64 i = 0; i < textures.count; i++) {
        if (i > 0) {
            u64 prev_size = sdl_surface_size(textures[i - 1]);
            offset = align_to(offset + prev_size, 512);
        }

        SDL_GPUTextureTransferInfo src = {
            .transfer_buffer = transfer_buffer,
            .offset = (u32)offset,
        };
        SDL_GPUTextureRegion dest = {
            .texture = texture,
            .mip_level = 0,
            .layer = 0,
            .x = (u32)rects[i].x,
            .y = (u32)rects[i].y,
            .z = 0,
            .w = (u32)textures[i]->w,
            .h = (u32)textures[i]->h,
            .d = 1,
        };
        SDL_UploadToGPUTexture(pass, &src, &dest, false);
    }

    //
    // Make sampler (doesn't super duper need to happen here but w/e)
    //

    SDL_GPUSamplerCreateInfo sampler_info = {
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };
    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &sampler_info);

    //
    // Return atlas descriptor
    //

    Atlas *atlas = arena_push<Atlas>(arena);
    atlas->size = atlas_size;
    atlas->texture = texture;
    atlas->sampler = sampler;
    atlas->placements = arena_push_arr<PxRect>(arena, textures.count);
    for (u64 i = 0; i < textures.count; i++) {
        atlas->placements[i].x = (u16)rects[i].x;
        atlas->placements[i].y = (u16)rects[i].y;
        atlas->placements[i].w = (u16)rects[i].w;
        atlas->placements[i].h = (u16)rects[i].h;
    }

    return atlas;
}

// TODO toggle through build system or something
constexpr bool RENDERER_DEBUG_MODE = true;

// TODO define these in a more principled way
constexpr u64 MAX_QUAD_COUNT = 1024;
constexpr u64 MAX_VERTEX_COUNT = MAX_QUAD_COUNT * 4;
constexpr u64 MAX_INDEX_COUNT = MAX_QUAD_COUNT * 6;

void re_init_pipeline(ErrorContext *err, App *app) {
    err_scope(err, "Init pipeline");

    size_t vert_shader_size = 0;
    size_t frag_shader_size = 0;
    // TODO arena allocate
    void *vert_shader_text = try_sdl(err, SDL_LoadFile("src/shaders/vert.msl", &vert_shader_size));
    void *frag_shader_text = try_sdl(err, SDL_LoadFile("src/shaders/frag.msl", &frag_shader_size));

    SDL_GPUShaderCreateInfo vert_info = {
        .code_size = vert_shader_size,
        .code = (u8 *)vert_shader_text,
        .format = SDL_GPU_SHADERFORMAT_MSL,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 0,
        .props = 0,
    };

    SDL_GPUShaderCreateInfo frag_info = {
        .code_size = frag_shader_size,
        .code = (u8 *)frag_shader_text,
        .format = SDL_GPU_SHADERFORMAT_MSL,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 0,
        .props = 0,
    };

    SDL_GPUShader *vert_shader = try_sdl(err, SDL_CreateGPUShader(app->device, &vert_info));
    defer(SDL_ReleaseGPUShader(app->device, vert_shader));
    SDL_GPUShader *frag_shader = try_sdl(err, SDL_CreateGPUShader(app->device, &frag_info));
    defer(SDL_ReleaseGPUShader(app->device, frag_shader));

    SDL_GPUColorTargetDescription color_target_descs[] = {{
        .format = SDL_GetGPUSwapchainTextureFormat(app->device, app->window),
        .blend_state = {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
        },
    }};

    SDL_GPUVertexBufferDescription vertex_buffer_descs[] = {{
        .slot = 0,
        .pitch = sizeof(PosTexVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    }};

    SDL_GPUVertexAttribute vertex_attrs[] = {
        {
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0,
        }, {
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = sizeof(float) * 3,
        }
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeline_create_info = {
        .vertex_shader = vert_shader,
        .fragment_shader = frag_shader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = vertex_buffer_descs,
            .num_vertex_buffers = c_arr_count(vertex_buffer_descs),
            .vertex_attributes = vertex_attrs,
            .num_vertex_attributes = c_arr_count(vertex_attrs),
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info = {
            .color_target_descriptions = color_target_descs,
            .num_color_targets = c_arr_count(color_target_descs),
        },
    };
    app->pipeline = try_sdl(err, SDL_CreateGPUGraphicsPipeline(app->device, &pipeline_create_info));
}

void re_init_vertex_buffers(ErrorContext *err, App *app) {
    err_scope(err, "Init vertex+index buffers");

    SDL_GPUBufferCreateInfo vert_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(PosTexVertex) * MAX_VERTEX_COUNT,
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
        .size = (sizeof(PosTexVertex) * MAX_VERTEX_COUNT) + (sizeof(u16) * MAX_INDEX_COUNT)
    };
    app->vertex_transfer_buffer = try_sdl(err, SDL_CreateGPUTransferBuffer(app->device, &transfer_buffer_info));
}

void re_upload_vertex_data(App *app, SDL_GPUCommandBuffer *command_buffer, RenderMesh *mesh) {
    log_assert(mesh->vertices.count <= MAX_VERTEX_COUNT);
    log_assert(mesh->indices.count <= MAX_INDEX_COUNT);

    u64 vertex_data_size = mesh->vertices.count * sizeof(mesh->vertices[0]);
    u64 index_data_size = mesh->indices.count * sizeof(mesh->indices[0]);

    void *transfer_data = (PosTexVertex *)SDL_MapGPUTransferBuffer(
        app->device,
        app->vertex_transfer_buffer,
        false
    );

    SDL_memcpy(transfer_data, mesh->vertices.ptr, vertex_data_size);
    SDL_memcpy((u8 *)transfer_data + vertex_data_size, mesh->indices.ptr, index_data_size);

    SDL_UnmapGPUTransferBuffer(app->device, app->vertex_transfer_buffer);

    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(command_buffer);

    // Upload vertex data
    SDL_GPUTransferBufferLocation vert_src = {
        .transfer_buffer = app->vertex_transfer_buffer,
        .offset = 0
    };
    SDL_GPUBufferRegion vert_dest = {
        .buffer = app->vertex_buffer,
        .offset = 0,
        .size = (u32)vertex_data_size,
    };
    SDL_UploadToGPUBuffer(pass, &vert_src, &vert_dest, false);

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
    SDL_UploadToGPUBuffer(pass, &index_src, &index_dest, false);

    SDL_EndGPUCopyPass(pass);
}

void re_init(ErrorContext *err, App *app) {
    err_scope(err, "Initialize renderer");

    app->device = try_sdl(err, SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, RENDERER_DEBUG_MODE, nullptr));
    try_sdl(err, SDL_ClaimWindowForGPUDevice(app->device, app->window));
    re_init_pipeline(err, app);
    re_init_vertex_buffers(err, app);

    SDL_GPUCommandBuffer *command_buffer = try_sdl(err, SDL_AcquireGPUCommandBuffer(app->device));
    SDL_GPUCopyPass *pass = try_sdl(err, SDL_BeginGPUCopyPass(command_buffer));
    app->atlas = re_pack_and_upload_textures(
            app->session_arena, app->device, pass, app->session->file->icons, { .w = 1024, .h = 1024 });
    SDL_EndGPUCopyPass(pass);
    try_sdl(err, SDL_SubmitGPUCommandBuffer(command_buffer));
}

void re_render(App *app) {
    Arena *frame_arena = arena_acquire();
    defer(arena_release(frame_arena));

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(app->device);

    // TODO should we get the window width and height earlier since
    // SDL_WaitAndAcquireGPUSwapchainTexture() blocks?
    SDL_GPUTexture *swapchain = nullptr;
    u32 width = 0;
    u32 height = 0;
    SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, app->window, &swapchain, &width, &height);
    PxSize window_size = { .w = (u16)width, .h = (u16)height };

    Box *box = prerender(frame_arena, app, window_size);
    RenderMesh mesh = {};
    vec_prealloc(frame_arena, &mesh.vertices, MAX_VERTEX_COUNT);
    vec_prealloc(frame_arena, &mesh.indices, MAX_INDEX_COUNT);
    re_build_boxes_mesh(frame_arena, window_size, box, { .x = 0, .y = 0 }, app->atlas, &mesh);
    re_upload_vertex_data(app, command_buffer, &mesh);

    SDL_GPUColorTargetInfo color_target_infos[] = {{
        .texture = swapchain,
        .clear_color = { 0.f, 0.f, 0.f, 1.f },
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    }};
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(
            command_buffer,
            color_target_infos,
            c_arr_count(color_target_infos),
            nullptr);
    SDL_GPUBufferBinding vertex_buffer_bindings[] = {{ .buffer = app->vertex_buffer, .offset = 0 }};
    SDL_GPUBufferBinding index_buffer_binding = { .buffer = app->index_buffer, .offset = 0 };
    SDL_GPUTextureSamplerBinding tex_sampler_bindings[] = {{
        .texture = app->atlas->texture,
        .sampler = app->atlas->sampler,
    }};

    SDL_BindGPUGraphicsPipeline(pass, app->pipeline);

    // SDL_SetGPUViewport(pass);
    SDL_BindGPUVertexBuffers(pass, 0, vertex_buffer_bindings, c_arr_count(vertex_buffer_bindings));
    SDL_BindGPUIndexBuffer(pass, &index_buffer_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_BindGPUFragmentSamplers(pass, 0, tex_sampler_bindings, c_arr_count(tex_sampler_bindings));
    SDL_DrawGPUIndexedPrimitives(pass, (u32)mesh.indices.count, 1, 0, 0, 0);

    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(command_buffer);
}
