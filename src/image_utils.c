#include "image_utils.h"

#include "stb_image.h"

fn Texture load_texture_from_image(ErrorContext *err, Arena *arena, Arr_u8 image_buffer) {
    Scope scope = scope_open(err);

    Texture texture = {};

    int width = 0;
    int height = 0;
    int channel_count = 0;
    u8 *pixels_buf = stbi_load_from_memory(image_buffer.ptr, (i32)image_buffer.count, &width,
                                           &height, &channel_count, 0);

    if (!pixels_buf) {
        err_report(err, "Failed to decode image: %s", stbi_failure_reason());
    } else {
        Arr_u8 pixels = {.ptr = pixels_buf, .count = (u64)(width * height * channel_count)};
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

        switch (channel_count) {
        case 1: {
            // Grayscale, no alpha
            for (u64 i = 0; i < width * height; i++) {
                u64 input_offset = i * 1;
                u64 output_offset = i * 4;
                buffer.ptr[output_offset + 0] = pixels_buf[input_offset + 0];
                buffer.ptr[output_offset + 1] = pixels_buf[input_offset + 0];
                buffer.ptr[output_offset + 2] = pixels_buf[input_offset + 0];
                buffer.ptr[output_offset + 3] = 0xff;
            }
            break;
        }
        case 2: {
            // Grayscale with alpha
            for (u64 i = 0; i < width * height; i++) {
                u64 input_offset = i * 2;
                u64 output_offset = i * 4;
                buffer.ptr[output_offset + 0] = pixels_buf[input_offset + 0];
                buffer.ptr[output_offset + 1] = pixels_buf[input_offset + 0];
                buffer.ptr[output_offset + 2] = pixels_buf[input_offset + 0];
                buffer.ptr[output_offset + 3] = pixels_buf[input_offset + 1];
            }
            break;
        }
        case 3: {
            // RGB, no alpha
            if (!SDL_ConvertPixels(width, height, SDL_PIXELFORMAT_RGB24, pixels_buf, width * 3,
                                   SDL_PIXELFORMAT_RGBA32, texture.buffer.ptr, width * 4)) {
                err_report(err, "%s", SDL_GetError());
            }
            break;
        }
        case 4: {
            arr_copy(texture.buffer, pixels);
            break;
        }
        default: {
            err_report(err, "Unexpected channel count: %d", channel_count);
            break;
        }
        }
    }

    if (pixels_buf) stbi_image_free(pixels_buf);
    scope_close(scope, "Load texture from image");
    return texture;
}
