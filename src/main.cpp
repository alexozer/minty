#include "base.hpp"

#include <stdarg.h>

#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3_ttf/SDL_ttf.h>

extern "C" {
#include <yyjson.h>
#include "xao.h"
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

struct SplitRecord {
    u64 attempt_num;
    Arr<Opt<Duration>> splits;
};

struct SegmentDef {
    Str name;
    Arr<u8> icon; // TODO a proper decode or something
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

struct TimerState {
    TimerMode mode;
    Vec<Opt<Duration>> live_splits;
    Instant start_time;
    Instant paused_time;
    Duration total_paused_duration;
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

Arr<SegSummary> calc_seg_summary(Arena *arena, TimerState *timer, FileDef *file) {
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

void timer_reset(TimerState *timer) {
    timer->mode = TimerMode::Init;
    vec_reset(&timer->live_splits);
}

Duration timer_get_elapsed(TimerState *timer) {
    Instant now = {};
    if (timer->mode == TimerMode::Paused) {
        now = timer->paused_time;
    } else {
        now = get_current_monotonic_time();
    }
    return (now - timer->start_time) - timer->total_paused_duration;
}

void timer_apply_action(Arena *arena, TimerState *timer, FileDef *file, TimerAction action) {
    // Switch statements are annoying... and neovim keeps indenting them wrong :(
    if (timer->mode == TimerMode::Init) {
        if (action == TimerAction::Split) {
            timer->mode = TimerMode::Running;
            timer->start_time = get_current_monotonic_time();
        }

    } else if (timer->mode == TimerMode::Running) {
        if (action == TimerAction::Split) {
            Duration elapsed = timer_get_elapsed(timer);
            vec_push(arena, &timer->live_splits, some(elapsed));

            if (timer->live_splits.count == file->segments.count) {
                timer->mode = TimerMode::Finished;
            }

        } else if (action == TimerAction::UndoSplit) {
            if (timer->live_splits.count == 0) {
                timer_reset(timer);
            } else {
                vec_pop(&timer->live_splits);
            }

        } else if (action == TimerAction::DeleteSplit) {
            if (timer->live_splits.count > 0) {
                timer->live_splits[timer->live_splits.count - 1] = {};
            }

        } else if (action == TimerAction::ResetAndSave) {
            file->total_attempts++;
            // TODO save golds and rest of file (?)
            timer_reset(timer);

        } else if (action == TimerAction::ResetAndDelete) {
            timer_reset(timer);

        } else if (action == TimerAction::Pause) {
            timer->mode = TimerMode::Paused;
            timer->paused_time = get_current_monotonic_time();
        }

    } else if (timer->mode == TimerMode::Paused) {
        if (action == TimerAction::Pause) {
            // Unpause
            Duration pause_duration = get_current_monotonic_time() - timer->paused_time;
            timer->total_paused_duration += pause_duration;
            timer->mode = TimerMode::Running;
        }

        else if (action == TimerAction::ResetAndSave) {
            file->total_attempts++;
            // save golds?
            timer_reset(timer);

        } else if (action == TimerAction::ResetAndDelete) {
            timer_reset(timer);
        }

    } else if (timer->mode == TimerMode::Finished) {
        if (action == TimerAction::UndoSplit) {
            vec_pop(&timer->live_splits);
            timer->mode = TimerMode::Running;

        } else if (action == TimerAction::ResetAndSave) {
            file->total_attempts++;
            file->completed_attempts++;
            // save golds?
            // save personal best?
            timer_reset(timer);

        } else if (action == TimerAction::ResetAndDelete) {
            timer_reset(timer);
        }
    }

    log_assert(false);
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
                seg->icon = decode_icon_base64_to_png(arena, err, base64);
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
        err_scope(err, "Load texture for segment '%.*s'", SF(file->segments[i].name));

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
                    if (str_is_empty(file->game_name)) {
                        err_report(err, "Empty game name");
                        return file;
                    }
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
        return nullptr;
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
    Arena *arena; // Lives for duration of application
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font *font;
    TTF_TextEngine *engine;
    TTF_Text *text;
    FileDef *file;
};

void init_text(ErrorContext *err, App *app) {
    if (!TTF_Init()) {
        log_fatal("Failed to initialize text engine: %s", SDL_GetError());
    }

    const char *font_path = "/Users/alex/Documents/repos/2026/blitter/data/Roboto-Regular.ttf";
    size_t font_file_size = 0;
    // TODO arena allocate
    void *font_buf = SDL_LoadFile(font_path, &font_file_size);
    {
        err_scope(err, "Load font '%s", font_path);
        if (font_buf == nullptr) {
            err_report(err, "Failed to open font: %s", SDL_GetError());
            return;
        }
    }
    Arr<u8> ttf = { .ptr = (u8 *)font_buf, .count = (u64)font_file_size };

    /* Open the font */
    app->font = TTF_OpenFontIO(SDL_IOFromConstMem(ttf.ptr, ttf.count), true, 40.0f);
    if (app->font == nullptr) {
        err_report(err, "Couldn't open font: %s", SDL_GetError());
        return;
    }

    /* Create the text engine */
    app->engine = TTF_CreateRendererTextEngine(app->renderer);
    if (app->engine == nullptr) {
        err_report(err, "Couldn't create text engine: %s", SDL_GetError());
        return;
    }

    /* Create the text */
    app->text = TTF_CreateText(app->engine, app->font, "Hello world!", 0);
    if (app->text == nullptr) {
        err_report(err, "Couldn't create text: %s", SDL_GetError());
        return;
    }
    TTF_SetTextColor(app->text, 255, 255, 255, SDL_ALPHA_OPAQUE);
}

SDL_HitTestResult hittest_callback(SDL_Window* window, const SDL_Point *point, void *data) {
    // Would expand the resize radius if I could, but doesn't appear to work on macOS
    return SDL_HITTEST_DRAGGABLE;
}

void init_window(ErrorContext *err, App *app) {
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

void init_app(ErrorContext *err, App *app) {
    init_window(err, app);
    init_text(err, app);

    if (g_argv.count > 1) {
        Str path = str_from_c(g_argv[1]);
        app->file = load_livesplit_lss(app->arena, err, app->renderer, path);
    } else {
        log_fatal("Usage: blitter <path-to-splits-file>");
    }
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    thread_init(argc, argv);

    ErrorContext err_base = { .arena = arena_acquire() };
    ErrorContext *err = &err_base;
    defer(arena_release(err_base.arena));

    Arena *root_arena = arena_acquire();
    App *app = arena_push<App>(root_arena);
    *appstate = (void *)app;
    app->arena = root_arena;

    init_app(err, app);
    if (err_failed(err)) {
        err_log(err);
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

void draw_sample_text(App *app) {
    int w = 0, h = 0;
    int text_w = 0, text_h = 0;
    float x, y;
    const float scale = 1.0f;

    /* Center the text and scale it up */
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_SetRenderScale(app->renderer, scale, scale);
    TTF_GetTextSize(app->text, &text_w, &text_h);
    x = ((w / scale) - text_w) / 2;
    y = ((h / scale) - text_h) / 2;

    /* Draw the text */
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    TTF_DrawRendererText(app->text, x, y);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App *app = (App *)appstate;

    draw_sample_text(app);

    // Draw split icons
    f32 y = 0;
    for (u64 i = 0; i < app->file->textures.count; i++) {
        SDL_FRect dest = {
            .x = 0,
            .y = y,
            .w = (f32)app->file->textures[i]->w,
            .h = (f32)app->file->textures[i]->h,
        };
        SDL_RenderTexture(app->renderer, app->file->textures[i], nullptr, &dest);
        y += (f32)app->file->textures[i]->h;
    }

    SDL_RenderPresent(app->renderer);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    // App *app = (App *)appstate;

    if (event->common.type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->key.key == SDLK_Q) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    // Just let OS clean up everything
}
