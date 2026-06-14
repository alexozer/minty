#include "base.hpp"

#include <stdarg.h>

#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include "SDL3/SDL_keycode.h"
#include <SDL3_ttf/SDL_ttf.h>

extern "C" {
#include <xao.h>
}
#include <yyjson.h>
#include <kb_text_shape.h>

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

    // TODO organize better
    Arr<SDL_Surface *> surfaces;
    Arr<SDL_Texture *> textures;
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
    return size == strlen(s) && memcmp(v.start, s, size) == 0;
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
        SegmentDef *seg = vec_push(arena, &segments, {});
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

void load_timer_textures(Arena *arena, ErrorContext *err, SDL_Renderer *renderer, FileDef *file) {
    err_scope(err, "Load segment textures");

    file->surfaces = arena_push_arr<SDL_Surface *>(arena, file->segments.count);
    file->textures = arena_push_arr<SDL_Texture *>(arena, file->segments.count);

    for (u64 i = 0; i < file->segments.count; i++) {
        if (arr_is_empty(file->segments[i].icon)) {
            continue;
        }

        err_scope(err, "Load icon texture for segment '%.*s'", SF(file->segments[i].name));

        SDL_IOStream *png_stream = SDL_IOFromMem(file->segments[i].icon.ptr, file->segments[i].icon.count);
        defer(SDL_CloseIO(png_stream));

        file->surfaces[i] = SDL_LoadPNG_IO(png_stream, false);
        if (file->surfaces[i] == nullptr) {
            err_report(err, "Failed to load surface from PNG: %s", SDL_GetError());
            return;
        }

        file->textures[i] = SDL_CreateTextureFromSurface(renderer, file->surfaces[i]);
        if (file->textures[i] == nullptr) {
            err_report(err, "Failed to create texture: %s", SDL_GetError());
            return;
        }
    }
}

FileDef *parse_livesplit_lss(Arena *arena, ErrorContext *err, Str xml) {
    err_scope(err, "Parse LiveSplit LSS");

    FileDef *file = arena_push<FileDef>(arena);

    if (!str_is_valid_utf8(xml)) {
        err_report(err, "Invalid UTF-8");
        return file;
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
        return file;
    }

    // Basic validation
    if (arr_is_empty(file->segments)) {
        err_report(err, "No segments found");
        return file;
    }
    for (u64 i = 0; i < file->segments.count; i++) {
        if (str_is_empty(file->segments[i].name)) {
            err_report(err, "Segment %" PRIu64 " has no name", i + 1);
            return file;
        }
    }

    if (str_is_empty(file->game_name)) {
        err_report(err, "Empty game name");
        return file;
    }

    return file;
}

FileDef *load_livesplit_lss(Arena *arena, ErrorContext *err, SDL_Renderer *renderer, Str lss_path) {
    err_scope(err, "Load LiveSplit LSS file '%.*s'", SF(lss_path));

    Arena *scratch = arena_acquire();
    defer(arena_release(scratch));

    // TODO arena allocate
    size_t size = 0;
    char *lss_path_cstr = str_to_c(scratch, lss_path);
    void *lss_buf = SDL_LoadFile(lss_path_cstr, &size);
    if (lss_buf == nullptr) {
        err_report(err, "Failed to load file: %s", SDL_GetError());
        return arena_push<FileDef>(arena);
    }
    defer(SDL_free(lss_buf));

    Str xml = { .ptr = (u8 *)lss_buf, .count = (u64)size };

    FileDef *file_def = parse_livesplit_lss(arena, err, xml);
    load_timer_textures(arena, err, renderer, file_def);

    return file_def;
}

//
// Main
//

constexpr i32 DEFAULT_WINDOW_WIDTH = 360;
constexpr i32 DEFAULT_WINDOW_HEIGHT = 600;
constexpr i32 MIN_WINDOW_WIDTH = 200;
constexpr i32 MIN_WINDOW_HEIGHT = 100;

struct App {
    Arena *app_arena; // Lives for duration of application
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font *font_small;
    TTF_Font *font_medium;
    TTF_Font *font_large;
    TTF_TextEngine *text_engine;
    SDL_Keycode prev_keys;

    Arena *session_arena;
    Session *session; // Nullable
};

void init_text(ErrorContext *err, App *app) {
    if (!TTF_Init()) {
        log_fatal("Failed to initialize text engine: %s", SDL_GetError());
    }

    const char *font_path = "data/Roboto-Medium.ttf";
    size_t font_file_size = 0;
    void *font_buf = nullptr;
    {
        err_scope(err, "Load font '%s", font_path);
        // TODO arena allocate
        font_buf = SDL_LoadFile(font_path, &font_file_size);
        if (font_buf == nullptr) {
            err_report(err, "Failed to open font: %s", SDL_GetError());
            return;
        }
    }
    Arr<u8> ttf = { .ptr = (u8 *)font_buf, .count = (u64)font_file_size };

    /* Open the font */
    app->font_small = TTF_OpenFontIO(SDL_IOFromConstMem(ttf.ptr, ttf.count), true, 15.0f);
    app->font_medium = TTF_OpenFontIO(SDL_IOFromConstMem(ttf.ptr, ttf.count), true, 18.0f);
    app->font_large = TTF_OpenFontIO(SDL_IOFromConstMem(ttf.ptr, ttf.count), true, 64.0f);
    if (app->font_small == nullptr || app->font_medium == nullptr || app->font_large == nullptr) {
        err_report(err, "Couldn't open font: %s", SDL_GetError());
        return;
    }

    /* Create the text engine */
    app->text_engine = TTF_CreateRendererTextEngine(app->renderer);
    if (app->text_engine == nullptr) {
        err_report(err, "Couldn't create text engine: %s", SDL_GetError());
        return;
    }
}

SDL_HitTestResult hittest_callback(SDL_Window* window, const SDL_Point *point, void *data) {
    // Would expand the resize radius if I could, but doesn't appear to work on macOS
    return SDL_HITTEST_DRAGGABLE;
}

void init_window(ErrorContext *err, App *app) {
    err_scope(err, "Initialize window");

    if (!SDL_SetAppMetadata("Blitter", "0.0.1", nullptr)) {
        err_report(err, "Failed to set app metadata: %s", SDL_GetError());
        return;
    }

    SDL_PropertiesID props = SDL_CreateProperties();
    if (props == 0) {
        err_report(err, "Unable to create properties: %s", SDL_GetError());
        return;
    }
    defer(SDL_DestroyProperties(props));

    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Blitter");
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, DEFAULT_WINDOW_WIDTH);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, DEFAULT_WINDOW_HEIGHT);

    app->window = SDL_CreateWindowWithProperties(props);
    if (app->window == nullptr) {
        err_report(err, "Unable to create window: %s", SDL_GetError());
        return;
    }

    if (!SDL_SetWindowMinimumSize(app->window, MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT)) {
        err_report(err, "Unable to set min window dimensions: %s", SDL_GetError());
        return;
    }
    if (!SDL_SetWindowHitTest(app->window, hittest_callback, nullptr)) {
        err_report(err, "Unable to set window hit test callback: %s", SDL_GetError());
        return;
    }

    app->renderer = SDL_CreateRenderer(app->window, nullptr);
    if (app->renderer == nullptr) {
        err_report(err, "Unable to create renderer: %s", SDL_GetError());
        return;
    }
    SDL_SetRenderVSync(app->renderer, 1);
}

Session *create_session(ErrorContext *err, Arena *arena, SDL_Renderer *renderer, Str path) {
    Session *session = arena_push<Session>(arena);

    session->file = load_livesplit_lss(arena, err, renderer, path);
    session->timer = arena_push<Timer>(arena);

    return session;
}

App *init_app(ErrorContext *err) {
    Arena *root_arena = arena_acquire();
    App *app = arena_push<App>(root_arena);
    app->app_arena = root_arena;

    init_window(err, app);
    init_text(err, app);

    if (g_argv.count < 2) {
        log_fatal("Usage: blitter <path-to-splits-file>");
    }
    Str path = str_from_c(g_argv[1]);

    app->session_arena = arena_acquire();
    app->session = create_session(err, app->session_arena, app->renderer, path);

    return app;
}

void shape_test();

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    thread_init(argc, argv);

    ErrorContext err_base = { .arena = arena_acquire() };
    ErrorContext *err = &err_base;
    defer(arena_release(err_base.arena));

    App *app = init_app(err);
    if (err_failed(err)) {
        err_log(err);
        return SDL_APP_FAILURE;
    }
    *appstate = app;

    // TODO remove
    shape_test();

    return SDL_APP_CONTINUE;
}

void try_load_new_session(App *app, Str lss_path) {
    ErrorContext err_base = { .arena = arena_acquire() };
    ErrorContext *err = &err_base;
    defer(arena_release(err_base.arena));

    Arena *session_arena = arena_acquire();
    Session *session = create_session(err, session_arena, app->renderer, lss_path);
    if (err_failed(err)) {
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
        char *clipboard_cstr = SDL_GetClipboardText();
        defer(SDL_free(clipboard_cstr));
        Str clipboard = str_trim(str_from_c(clipboard_cstr));
        try_load_new_session(app, clipboard);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    // Just let OS clean up everything
}

//
// MARK:Rendering
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
    SDL_Texture *texture;

    SDL_FColor color;

    f32 width;
    f32 height;
    // Hm, padding determined by just making more boxes for now?

    Vec<Box*> children;

    // AABB of self and children - lazily computed
    Opt<SDL_FPoint> bbox;
};

Box *make_text_box(Arena *arena, TTF_TextEngine *engine, TTF_Font *font, Str content, SDL_FColor color) {
    Box *box = arena_push<Box>(arena);

    box->type = BoxType::Text;
    const char *content_cstr = str_is_empty(content) ? "" : (const char *)content.ptr;
    // Zero length actually means "treat string as null terminated"
    box->text_obj = TTF_CreateText(engine, font, content_cstr, content.count);
    TTF_SetTextColorFloat(box->text_obj, color.r, color.g, color.b, color.a);

    return box;
}

Box *make_empty_box(Arena *arena, f32 width, f32 height) {
    Box *box = arena_push<Box>(arena);
    box->type = BoxType::Empty;
    box->width = max(width, 0.0f);
    box->height = max(height, 0.0f);
    return box;
}

Box *make_texture_box(Arena *arena, SDL_Texture *texture, f32 width, f32 height) {
    Box *box = arena_push<Box>(arena);
    box->type = BoxType::Texture;
    box->width = max(width, 0.0f);
    box->height = max(height, 0.0f);
    box->texture = texture;
    return box;
}

Box *make_solid_color_box(Arena *arena, SDL_FColor color, f32 width, f32 height) {
    Box *box = arena_push<Box>(arena);
    box->type = BoxType::SolidColor;
    box->width = max(width, 0.0f);
    box->height = max(height, 0.0f);
    box->color = color;
    return box;
}

SDL_FPoint compute_box_bbox(Box *box);

SDL_FPoint compute_box_bbox_uncached(Box *box) {
    switch (box->type) {
    case BoxType::Empty:
    case BoxType::SolidColor: {
        return { .x = box->width, .y = box->height };
    }
    case BoxType::LeftToRightStack: {
        SDL_FPoint total = {};
        for (u64 i = 0; i < box->children.count; i++) {
            SDL_FPoint child_bbox = compute_box_bbox(box->children[i]);
            if (i == 0) {
                total = child_bbox;
            } else {
                total.x += child_bbox.x;
                total.y = max(total.y, child_bbox.y);
            }
        }
        return total;
    }
    case BoxType::TopToBottomStack: {
        SDL_FPoint total = {};
        for (u64 i = 0; i < box->children.count; i++) {
            SDL_FPoint child_bbox = compute_box_bbox(box->children[i]);
            if (i == 0) {
                total = child_bbox;
            } else {
                total.x = max(total.x, child_bbox.x);
                total.y += child_bbox.y;
            }
        }
        return total;
    }
    case BoxType::BackToFrontStack: {
        SDL_FPoint total = {};
        for (u64 i = 0; i < box->children.count; i++) {
            SDL_FPoint child_bbox = compute_box_bbox(box->children[i]);
            if (i == 0) {
                total = child_bbox;
            } else {
                total.x = max(total.x, child_bbox.x);
                total.y = max(total.y, child_bbox.y);
            }
        }
        return total;
    }
    case BoxType::Text: {
        i32 width = 0;
        i32 height = 0;
        TTF_GetTextSize(box->text_obj, &width, &height);
        return { .x = (f32)width, .y = (f32)height };
    }
    case BoxType::Texture: {
        return { .x = box->width, .y = box->height };
    }
    }
}

SDL_FPoint compute_box_bbox(Box *box) {
    if (box->bbox.present) {
        return box->bbox.opt;
    }
    SDL_FPoint bbox = compute_box_bbox_uncached(box);
    box->bbox = some(bbox);
    return bbox;
}

Box *pad_box_left(Arena *arena, Box *box, f32 pad) {
    if (pad < 0) return box;

    SDL_FPoint bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, pad, bbox.y);

    Box *parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::LeftToRightStack;
    vec_push(arena, &parent_box->children, pad_box);
    vec_push(arena, &parent_box->children, box);

    return parent_box;
}

Box *pad_box_right(Arena *arena, Box *box, f32 pad) {
    if (pad < 0) return box;

    SDL_FPoint bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, pad, bbox.y);

    Box *parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::LeftToRightStack;
    vec_push(arena, &parent_box->children, box);
    vec_push(arena, &parent_box->children, pad_box);

    return parent_box;
}

Box *pad_box_top(Arena *arena, Box *box, f32 pad) {
    if (pad < 0) return box;

    SDL_FPoint bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, bbox.x, pad);

    Box *parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::TopToBottomStack;
    vec_push(arena, &parent_box->children, pad_box);
    vec_push(arena, &parent_box->children, box);

    return parent_box;
}

Box *pad_box_bottom(Arena *arena, Box *box, f32 pad) {
    if (pad < 0) return box;

    SDL_FPoint bbox = compute_box_bbox(box);
    Box *pad_box = make_empty_box(arena, bbox.x, pad);

    Box *parent_box = arena_push<Box>(arena);
    parent_box->type = BoxType::TopToBottomStack;
    vec_push(arena, &parent_box->children, box);
    vec_push(arena, &parent_box->children, pad_box);

    return parent_box;
}

Box *align_box_center_horiz(Arena *arena, Box *box, f32 width) {
    SDL_FPoint bbox = compute_box_bbox(box);
    f32 pad = (width - bbox.x) / 2;
    Box *pad_box = make_empty_box(arena, pad, bbox.y);

    Box *parent = arena_push<Box>(arena);
    parent->type = BoxType::LeftToRightStack;
    vec_push(arena, &parent->children, pad_box);
    vec_push(arena, &parent->children, box);
    vec_push(arena, &parent->children, pad_box);

    return parent;
}

Box *align_box_center_vert(Arena *arena, Box *box, f32 height) {
    SDL_FPoint bbox = compute_box_bbox(box);
    f32 pad = (height - bbox.y) / 2;
    Box *pad_box = make_empty_box(arena, bbox.x, pad);

    Box *parent = arena_push<Box>(arena);
    parent->type = BoxType::TopToBottomStack;
    vec_push(arena, &parent->children, pad_box);
    vec_push(arena, &parent->children, box);
    vec_push(arena, &parent->children, pad_box);

    return parent;
}

Box *prerender_segment(Arena *arena, App *app, f32 width, u64 idx) {
    constexpr f32 ICON_INNER = 36.f;
    constexpr f32 ICON_OUTER = 44.f;
    Box *icon = nullptr;
    if (!arr_is_empty(app->session->file->segments[idx].icon)) {
        icon = make_texture_box(arena, app->session->file->textures[idx], ICON_INNER, ICON_INNER);
    } else {
        icon = make_empty_box(arena, ICON_INNER, ICON_INNER);
    }
    icon = align_box_center_horiz(arena, icon, ICON_OUTER);
    icon = align_box_center_vert(arena, icon, ICON_OUTER);

    SDL_FColor text_color = { .r = 1.f, .g = 1.f, .b = 1.f, .a = 1.f };
    Box *pad = make_empty_box(arena, 10, 0);
    Box *title = make_text_box(arena, app->text_engine, app->font_medium, app->session->file->segments[idx].name, text_color);
    Box *title_centered = align_box_center_vert(arena, title, ICON_OUTER);

    Box *row_front = arena_push<Box>(arena);
    row_front->type = BoxType::LeftToRightStack;
    vec_push(arena, &row_front->children, icon);
    vec_push(arena, &row_front->children, pad);
    vec_push(arena, &row_front->children, title_centered);

    if (app->session->timer->mode == TimerMode::Running && idx == app->session->timer->live_splits.count) {
        SDL_FPoint row_front_bbox = compute_box_bbox(row_front);
        SDL_FColor bg_color = { .r = 0.f, .g = 0.3f, .b = 0.90f, .a = 1.f };
        Box *row_back = make_solid_color_box(arena, bg_color, width, row_front_bbox.y);

        Box *row = arena_push<Box>(arena);
        row->type = BoxType::BackToFrontStack;
        vec_push(arena, &row->children, row_back);
        vec_push(arena, &row->children, row_front);

        return row;
    }

    return row_front;
}

Box *prerender_contents(Arena *arena, App *app, f32 width, f32 height) {
    SDL_FColor color = { .r = 1.f, .g = 1.f, .b = 1.f, .a = 1.f };

    Box *game_name = make_text_box(arena, app->text_engine, app->font_medium,
            app->session->file->game_name, color);
    Box *cat_name = make_text_box(arena, app->text_engine, app->font_medium,
            app->session->file->category_name, color);

    Box *game_name_centered = align_box_center_horiz(arena, game_name, (f32)width);
    Box *cat_name_centered = align_box_center_horiz(arena, cat_name, (f32)width);

    Box *top = arena_push<Box>(arena);
    top->type = BoxType::TopToBottomStack;
    vec_push(arena, &top->children, game_name_centered);
    vec_push(arena, &top->children, cat_name_centered);

    for (u64 i = 0; i < app->session->file->segments.count; i++) {
        if (!str_starts_with(app->session->file->segments[i].name, S("-"))) {
            Box *segment = prerender_segment(arena, app, width, i);
            vec_push(arena, &top->children, segment);
        }
    }

    Box *bottom = arena_push<Box>(arena);
    bottom->type = BoxType::TopToBottomStack;

    Duration t = timer_get_elapsed(app->session->timer, get_current_monotonic_time());
    Str t_str = format_duration(arena, t, 2, false);
    Box *curr_time = make_text_box(arena, app->text_engine, app->font_large, t_str, color);
    SDL_FPoint curr_time_bbox = compute_box_bbox(curr_time);
    Box *curr_time_aligned = pad_box_left(arena, curr_time, width - curr_time_bbox.x);

    vec_push(arena, &bottom->children, curr_time_aligned);

    // Put timer at bottom
    SDL_FPoint top_bbox = compute_box_bbox(top);
    SDL_FPoint bottom_bbox = compute_box_bbox(bottom);
    Box *vsep = make_empty_box(arena, 0, height - top_bbox.y - bottom_bbox.y);

    Box *root = arena_push<Box>(arena);
    root->type = BoxType::TopToBottomStack;
    vec_push(arena, &root->children, top);
    vec_push(arena, &root->children, vsep);
    vec_push(arena, &root->children, bottom);

    return root;
}

Box *prerender(Arena *arena, App *app) {
    i32 width = 0;
    i32 height = 0;
    SDL_GetRenderOutputSize(app->renderer, &width, &height);

    constexpr f32 PADDING = 10;
    Box *timer = prerender_contents(arena, app, (f32)width - (PADDING * 2.f), (f32)height - (PADDING * 2.f));
    timer = pad_box_left(arena, timer, PADDING);
    timer = pad_box_right(arena, timer, PADDING);
    timer = pad_box_top(arena, timer, PADDING);
    timer = pad_box_bottom(arena, timer, PADDING);
    return timer;
}

void render_box(App *app, Box *box, SDL_FPoint where) {
    switch (box->type) {
    case BoxType::Empty: {
        break;
    }
    case BoxType::LeftToRightStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            SDL_FPoint child_bbox = compute_box_bbox(box->children[i]);
            render_box(app, box->children[i], where);
            where.x += child_bbox.x;
        }
        break;
    }
    case BoxType::TopToBottomStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            SDL_FPoint child_bbox = compute_box_bbox(box->children[i]);
            render_box(app, box->children[i], where);
            where.y += child_bbox.y;
        }
        break;
    }
    case BoxType::BackToFrontStack: {
        for (u64 i = 0; i < box->children.count; i++) {
            render_box(app, box->children[i], where);
        }
        break;
    }
    case BoxType::Text: {
        TTF_DrawRendererText(box->text_obj, where.x, where.y);
        break;
    }
    case BoxType::Texture: {
        f32 src_ratio = (f32)box->texture->w / (f32)box->texture->h;
        f32 dst_ratio = box->width / box->height;

        // Scale to fit
        SDL_FRect dest = {};
        if (src_ratio > dst_ratio) {
            f32 scale = box->width / (f32)box->texture->w;
            dest.w = box->width;
            dest.h = (f32)box->texture->h * scale;
            dest.x = where.x;
            dest.y = where.y + ((box->height - dest.h) / 2.f);
        } else {
            f32 scale = box->height / (f32)box->texture->h;
            dest.w = (f32)box->texture->w * scale;
            dest.h = box->height;
            dest.x = where.x + ((box->width - dest.w) / 2.f);
            dest.y = where.y;
        }
        SDL_RenderTexture(app->renderer, box->texture, nullptr, &dest);
        break;
    }
    case BoxType::SolidColor: {
        SDL_SetRenderDrawColorFloat(app->renderer, box->color.r, box->color.g, box->color.b, box->color.a);
        SDL_FRect r = { .x = where.x, .y = where.y, .w = box->width, .h = box->height };
        SDL_RenderFillRect(app->renderer, &r);
        break;
    }
    }
}

void render(App *app) {
    // Frame arena
    Arena *arena = arena_acquire();
    defer(arena_release(arena));

    Box *box = prerender(arena, app);

    SDL_SetRenderDrawColor(app->renderer, 5, 0, 8, 255);
    SDL_RenderClear(app->renderer);

    render_box(app, box, {});

    SDL_RenderPresent(app->renderer);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App *app = (App *)appstate;
    render(app);
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
            (char *)shape_text.ptr, shape_text.count,
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
