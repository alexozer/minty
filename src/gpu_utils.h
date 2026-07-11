//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

SDL_GPUGraphicsPipeline *gpu_make_pipeline(ErrorContext *err,
                                           SDL_GPUDevice *device,
                                           SDL_GPUShader *vert_shader,
                                           SDL_GPUShader *frag_shader,
                                           SDL_GPUTextureFormat target_texture_format,
                                           BlendType blend_type);
SDL_GPUShader *gpu_load_shader(ErrorContext *err,
                               SDL_GPUDevice *device,
                               Arr_u8 source,
                               ShaderStage type);
SDL_GPUBuffer *sdl_create_gpu_buffer(ErrorContext *err,
                                     SDL_GPUDevice *device,
                                     SDL_GPUBufferCreateInfo *info,
                                     Str name);
SDL_GPUTransferBuffer *sdl_create_gpu_transfer_buffer(ErrorContext *err,
                                                      SDL_GPUDevice *device,
                                                      SDL_GPUTransferBufferCreateInfo *info);
SDL_GPUCommandBuffer *sdl_acquire_gpu_command_buffer(ErrorContext *err, SDL_GPUDevice *device);
void sdl_submit_gpu_command_buffer(SDL_GPUCommandBuffer *command_buffer);
u64 quad_vertices(u64 quad_count);
u64 quad_indices(u64 quad_count);
