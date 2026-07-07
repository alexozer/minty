//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

TextureSystem *tex_init(ErrorContext *err,
                        Arena *arena,
                        SDL_GPUDevice *device,
                        SDL_Window *window,
                        SDL_GPUShader *vertex_shader,
                        SDL_GPUShader *dummy_fragment_shader);
void clear_texture(SDL_GPUCommandBuffer *command_buffer,
                   SDL_GPUGraphicsPipeline *pipeline,
                   SDL_GPUTexture *texture);
Atlas *init_atlas(ErrorContext *err,
                  Arena *arena,
                  SDL_GPUDevice *device,
                  Str name,
                  SizePX atlas_size,
                  SDL_GPUTextureFormat texture_format,
                  FilterType filter);
void upload_texture(SDL_GPUCopyPass *copy_pass,
                    SDL_GPUTexture *texture,
                    SDL_GPUTransferBuffer *transfer_buffer,
                    u32 offset,
                    RectPX placement);
Arr_P_RenderInst tex_prepare_to_render(Arena *frame_arena,
                                       TextureSystem *ctx,
                                       RenderState *render_state,
                                       Arr_TextureRequest requests);
