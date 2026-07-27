#include "base.h"

#include <stdarg.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include <SDL3/SDL_clipboard.h>

#include "main.h"
#include "render.h"
#include "timer_load.h"
#include "timer_update.h"

fn SDL_HitTestResult hittest_callback(SDL_Window *window, const SDL_Point *point, void *data) {
    // Would expand the resize radius if I could, but doesn't appear to work on
    // macOS
    return SDL_HITTEST_DRAGGABLE;
}

fn SDL_Window *sdl_create_window(ErrorContext *err,
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

fn SDL_Window *init_window(ErrorContext *err) {
    Scope scope = scope_open(err);

    if (!SDL_SetAppMetadata("Minty", "0.0.1", nullptr)) {
        err_report(err, "%s", SDL_GetError());
    }

    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window *window =
        sdl_create_window(err, S("Minty"), DEFAULT_WINDOW_SIZE, MIN_WINDOW_SIZE, window_flags);

    scope_close(scope, "Initialize window");
    return window;
}

fn App *init_app(ErrorContext *err, Str path) {
    Arena *root_arena = arena_acquire();
    App *app = arena_push(root_arena, App);
    app->app_arena = root_arena;

    app->window = init_window(err);

    app->session_arena = arena_acquire();
    app->session = make_session(err, app->session_arena, app, path);
    if (!err_occurred(err)) {
        render_init(err, app);
    }

    return app;
}

fn SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    thread_init();
    if (argc < 2) {
        log_info("Usage: minty <path-to-splits-file>");
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

extern i32 debug_glyph_step;

fn SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
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
        case SDLK_GRAVE: {
            app->debug_draw = !app->debug_draw;
            break;
        }
        case SDLK_EQUALS: {
            app->zoom++;
            break;
        }
        case SDLK_MINUS: {
            app->zoom--;
            break;
        }
        case SDLK_0: {
            app->zoom = 0.f;
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

    // if (event->common.type == SDL_EVENT_MOUSE_WHEEL) {
    // }

    return SDL_APP_CONTINUE;
}

fn SDL_AppResult SDL_AppIterate(void *appstate) {
    App *app = (App *)appstate;
    render(app);
    return SDL_APP_CONTINUE;
}

fn void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    // Just let OS clean up everything
}
