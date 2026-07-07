//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

SDL_HitTestResult hittest_callback(SDL_Window *window, const SDL_Point *point, void *data);
SDL_Window *sdl_create_window(ErrorContext *err,
                              Str name,
                              SizePX size,
                              SizePX min_size,
                              SDL_WindowFlags flags);
SDL_Window *init_window(ErrorContext *err);
Session *make_session(ErrorContext *err, Arena *arena, App *app, Str path);
App *init_app(ErrorContext *err, Str path);
SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv);
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event);
SDL_AppResult SDL_AppIterate(void *appstate);
void SDL_AppQuit(void *appstate, SDL_AppResult result);
