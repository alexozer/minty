//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn SDL_HitTestResult hittest_callback(SDL_Window *window, const SDL_Point *point, void *data);
fn SDL_Window *sdl_create_window(ErrorContext *err,
                                 Str name,
                                 SizePX size,
                                 SizePX min_size,
                                 SDL_WindowFlags flags);
fn SDL_Window *init_window(ErrorContext *err);
fn App *init_app(ErrorContext *err, Str path);
fn SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv);
fn SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event);
fn SDL_AppResult SDL_AppIterate(void *appstate);
fn void SDL_AppQuit(void *appstate, SDL_AppResult result);
