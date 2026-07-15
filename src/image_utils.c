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
            log_info("Loading image: width = %d, height = %d, format = RGB24", width, height);
            break;
        }
        case 4: {
            format = SDL_PIXELFORMAT_RGBA32;
            pitch = width * 4;
            log_info("Loading image: width = %d, height = %d, format = RGBA8888", width, height);
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
        // if (channel_count == 3) {
        //     // Try manually converting pixels
        //     for (u64 i = 0; i < width * height; i++) {
        //         u64 input_offset = i * 3;
        //         u64 output_offset = i * 4;
        //         buffer.ptr[output_offset + 0] = pixels[input_offset + 0];
        //         buffer.ptr[output_offset + 1] = pixels[input_offset + 1];
        //         buffer.ptr[output_offset + 2] = pixels[input_offset + 2];
        //         buffer.ptr[output_offset + 3] = 0xff;
        //     }
        // } else {
        //     SDL_memcpy(buffer.ptr, pixels, buffer.count);
        // }
        texture = (Texture){
            .format = ICON_TEXTURE_FORMAT,
            .buffer = buffer,
            .dims = {.w = (u16)width, .h = (u16)height},
        };
        if (!SDL_ConvertPixels(width, height, format, pixels, pitch, SDL_PIXELFORMAT_RGBA32,
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
