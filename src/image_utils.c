#include "image_utils.h"

#include "stb_image.h"

fn Texture load_texture_from_image(ErrorContext *err, Arena *arena, Arr_u8 image_buffer) {
    Scope scope = scope_open(err);

    Texture texture = {};

    int width = 0;
    int height = 0;
    int channel_count = 0;
    u8 *pixels = stbi_load_from_memory(image_buffer.ptr, (i32)image_buffer.count, &width, &height,
                                       &channel_count, 0);
    if (!pixels) {
        err_report(err, "Failed to decode image: %s", stbi_failure_reason());
    } else {
        SDL_PixelFormat format = SDL_PIXELFORMAT_UNKNOWN;
        i32 pitch = 0;
        switch (channel_count) {
        case 1:
        case 2: {
            err_report(err, "Grayscale images are currently unsupported");
            break;
        }
        case 3: {
            format = SDL_PIXELFORMAT_RGB24;
            pitch = width * 3;
            break;
        }
        case 4: {
            format = SDL_PIXELFORMAT_RGBA8888;
            pitch = width * 4;
            break;
        }
        default: {
            err_report(err, "Unexpected channel count: %d", channel_count);
            break;
        }
        }

        u64 dest_size = (u64)(width * height * 4);
        Arr_u8 buffer = {
            .ptr = (u8 *)arena_push_bytes(arena, dest_size, 8),
            .count = dest_size,
        };
        texture = (Texture){
            .format = ICON_TEXTURE_FORMAT,
            .buffer = buffer,
            .dims = {.w = (u16)width, .h = (u16)height},
        };
        if (!SDL_ConvertPixels(width, height, format, pixels, pitch, SDL_PIXELFORMAT_RGBA8888,
                               texture.buffer.ptr, width * 4)) {
            err_report(err, "%s", SDL_GetError());
        }
    }

    if (pixels) stbi_image_free(pixels);
    scope_close(scope, "Load texture from image");
    return texture;
}

fn Texture convert_srgb_surface_to_rgba(Arena *arena, SDL_Surface *surface) {
    if (!surface) return (Texture){};

    u64 dest_size = (u64)(surface->w * surface->h * 4);
    Arr_u8 buffer = {
        .ptr = (u8 *)arena_push_bytes(arena, dest_size, 8),
        .count = dest_size,
    };
    Texture texture = {
        .format = ICON_TEXTURE_FORMAT,
        .buffer = buffer,
        .dims = {.w = (u16)surface->w, .h = (u16)surface->h},
    };
    log_assert(SDL_ConvertPixels(surface->w, surface->h, surface->format, surface->pixels,
                                 surface->pitch, SDL_PIXELFORMAT_RGBA8888, texture.buffer.ptr,
                                 surface->w * 4));
    return texture;
}
