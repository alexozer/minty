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
Atlas *tex_init_atlas(ErrorContext *err,
                      Arena *arena,
                      SDL_GPUDevice *device,
                      Str name,
                      SizePX atlas_size,
                      SDL_GPUTextureFormat texture_format,
                      FilterType filter);
Arr_P_RenderInst tex_prepare_to_render(Arena *frame_arena,
                                       TextureSystem *ctx,
                                       RenderState *render_state,
                                       Arr_TextureRequest requests);
void tex_prepare_clear_insts(Arena *frame_arena,
                             TextureSystem *ctx,
                             RenderState *render_state,
                             FixedVec_P_RenderInst *render_insts);
void pack_textures_into_existing_atlas(Arena *frame_arena,
                                       Atlas *atlas,
                                       RenderState *render_state,
                                       Arr_P_CPUTexture textures,
                                       FixedVec_P_RenderInst *render_insts);
