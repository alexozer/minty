//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

TextureSystem *tex_init(ErrorContext *err,
                        Arena *arena,
                        SDL_GPUDevice *device,
                        SDL_GPUShader *vertex_shader,
                        SDL_GPUShader *dummy_fragment_shader);
void init_dummy_white_texture(Arena *arena, Texture *texture);
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
                                       SizePX window_size,
                                       Arr_QuadRequest requests);
void tex_build_clear_insts(Arena *frame_arena,
                           TextureSystem *ctx,
                           RenderState *render_state,
                           FVec_P_RenderInst *render_insts);
void tex_build_upload_insts(Arena *frame_arena,
                            TextureSystem *ctx,
                            RenderState *render_state,
                            Arr_QuadRequest requests,
                            FVec_P_RenderInst *render_insts);
TextureCacheEntry *alloc_cache_entry(TextureSystem *ctx);
void pack_textures_into_existing_atlas(Arena *frame_arena,
                                       TextureSystem *ctx,
                                       Atlas *atlas,
                                       RenderState *render_state,
                                       Arr_P_Texture textures,
                                       FVec_P_RenderInst *render_insts);
void tex_build_draw_insts(Arena *frame_arena,
                          TextureSystem *ctx,
                          RenderState *render_state,
                          SizePX window_size,
                          Arr_QuadRequest requests,
                          FVec_P_RenderInst *render_insts);
void push_atlas_quad(SizePX window_size,
                     Atlas *atlas,
                     MeshBuilder *mesh,
                     RectPX src,
                     QuadRequest *req);
void window_to_ndc(Vertex *vertex, SizePX window_size);
