//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn Texture load_texture_from_image(ErrorContext *err, Arena *arena, Arr_u8 image_buffer);
fn Texture convert_srgb_surface_to_rgba(Arena *arena, SDL_Surface *surface);
