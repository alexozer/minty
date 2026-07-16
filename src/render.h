//
// GENERATED FILE - DO NOT MODIFY
//

#pragma once

#include "types.h"

fn void render_init(ErrorContext *err, App *app);
fn void init_vertex_buffers(ErrorContext *err, RenderState *render_state);
fn void init_render_pipelines(ErrorContext *err, SDL_Window *window, RenderState *render_state);
fn void render(App *app);
fn void do_clear_texture_passes(RenderState *render_state,
                                SDL_GPUCommandBuffer *command_buffer,
                                Arr_P_RenderInst clear_texture_insts);
fn void do_upload_texture_passes(RenderState *render_state,
                                 SDL_GPUCommandBuffer *command_buffer,
                                 Arr_P_RenderInst upload_insts);
fn void do_upload_geometry_pass(RenderState *render_state,
                                SDL_GPUCommandBuffer *command_buffer,
                                u32 vertex_count,
                                u32 index_count);
fn void do_draw_pass(SDL_GPUCommandBuffer *command_buffer,
                     SDL_GPUTexture *swapchain_texture,
                     SDL_GPUBuffer *vertex_buffer,
                     SDL_GPUBuffer *index_buffer,
                     Arr_P_RenderInst render_insts,
                     Arr_VertexBufferRegion regions);
fn void do_draw_passes(RenderState *render_state,
                       SDL_GPUCommandBuffer *command_buffer,
                       SDL_GPUTexture *swapchain_texture,
                       Arr_P_RenderInst draw_insts);
