#include "render.h"
#include "font.h"
#include "gpu_utils.h"
#include "platform.h"
#include "texture.h"
#include "timer_ui.h"
#include "types.h"
#include "ui.h"

fn void render_init(ErrorContext *err, App *app) {
    Scope scope = scope_open(err);

    font_init(&app->font_system, app->app_arena);

    RenderState *render_state = arena_push(app->app_arena, RenderState);
    app->render_state = render_state;
    render_state->quad_requests = fvec_alloc(app->app_arena, QuadRequest, MAX_QUAD_COUNT);
    render_state->device =
        SDL_CreateGPUDevice(OS_SHADER_FORMAT, RENDERER_DEBUG_MODE_ENABLED, nullptr);
    if (!render_state->device) {
        err_report(err, "%s", SDL_GetError());
    } else {
        // ... or should  I make all renderer initialization functions invariant to nullptr
        // SDL_GPUDevice?
        SDL_ClaimWindowForGPUDevice(render_state->device, app->window);
        init_render_pipelines(err, app->window, render_state);
        init_vertex_buffers(err, render_state);
    }
    app->texture_system = tex_init(err, app->app_arena, render_state->device,
                                   render_state->vertex_shader, render_state->glyph_frag_shader);

    scope_close(scope, "Initialize renderer");
}

fn void init_vertex_buffers(ErrorContext *err, RenderState *render_state) {
    Scope scope = scope_open(err);

    SDL_GPUBufferCreateInfo vert_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = (u32)(sizeof(Vertex) * quad_vertices(MAX_QUAD_COUNT)),
    };
    render_state->vertex_buffer =
        sdl_create_gpu_buffer(err, render_state->device, &vert_info, S("THE vertex buffer"));

    SDL_GPUBufferCreateInfo index_info = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = (u32)(sizeof(u16) * quad_indices(MAX_QUAD_COUNT)),
    };
    render_state->index_buffer =
        sdl_create_gpu_buffer(err, render_state->device, &index_info, S("THE index buffer"));

    SDL_GPUTransferBufferCreateInfo transfer_buffer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = vert_info.size + index_info.size,
    };
    render_state->geom_transfer_buffer =
        sdl_create_gpu_transfer_buffer(err, render_state->device, &transfer_buffer_info);

    scope_close(scope, "Init vertex+index buffers");
}

fn void init_render_pipelines(ErrorContext *err, SDL_Window *window, RenderState *render_state) {
    // Load shaders
    Arr_u8 vert_shader = {.ptr = os_shader_vert, .count = os_shader_vert_len};
    Arr_u8 frag_icon_shader = {.ptr = os_shader_frag_icon, .count = os_shader_frag_icon_len};
    Arr_u8 frag_glyph_shader = {.ptr = os_shader_frag_glyph, .count = os_shader_frag_glyph_len};

    render_state->vertex_shader =
        gpu_load_shader(err, render_state->device, vert_shader, ShaderStage_Vertex);
    render_state->icon_frag_shader =
        gpu_load_shader(err, render_state->device, frag_icon_shader, ShaderStage_Fragment);
    render_state->glyph_frag_shader =
        gpu_load_shader(err, render_state->device, frag_glyph_shader, ShaderStage_Fragment);

    if (err_occurred(err)) return;

    if (SDL_WindowSupportsGPUPresentMode(render_state->device, window,
                                         SDL_GPU_PRESENTMODE_MAILBOX)) {
        SDL_SetGPUSwapchainParameters(render_state->device, window,
                                      SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
                                      SDL_GPU_PRESENTMODE_MAILBOX);
    } else {
        SDL_SetGPUSwapchainParameters(render_state->device, window,
                                      SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
                                      SDL_GPU_PRESENTMODE_VSYNC);
    }
    SDL_GPUTextureFormat swapchain_format =
        SDL_GetGPUSwapchainTextureFormat(render_state->device, window);
    render_state->icon_pipeline =
        gpu_make_pipeline(err, render_state->device, render_state->vertex_shader,
                          render_state->icon_frag_shader, swapchain_format, BlendType_Over);
    render_state->glyph_pipeline =
        gpu_make_pipeline(err, render_state->device, render_state->vertex_shader,
                          render_state->glyph_frag_shader, swapchain_format, BlendType_Over);
}

fn void render(App *app) {
    Arena *frame_arena = arena_acquire();

    RenderState *render_state = app->render_state;
    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(render_state->device);

    SDL_GPUTexture *swapchain_texture = nullptr;
    u32 width = 0;
    u32 height = 0;
    SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, app->window, &swapchain_texture, &width,
                                          &height);
    SizePX window_size = {(u16)width, (u16)height};

    if (swapchain_texture) {  // Apparently can be null if window is minimized
        FVec_QuadRequest *quad_requests = &render_state->quad_requests;
        fvec_reset(quad_requests);

        // 0.5f is just remnant of originally building the UI at 1X scale
        f32 scale = SDL_GetWindowDisplayScale(app->window) * 0.5f * (app->zoom + 1.f);
        UI_Box *root = build_timer_ui(frame_arena, app->session, window_size, scale);

        render_ui(frame_arena, root, &app->font_system, quad_requests);

        if (app->debug_draw) {
            debug_render_ui(frame_arena, root, quad_requests);
        }

        Arr_RenderInst render_insts = tex_prepare_to_render(
            frame_arena, app->texture_system, render_state, window_size, fvec_arr(quad_requests));

        // Group render insts by type
        FVec_RenderInst clear_texture_insts =
            fvec_alloc(frame_arena, RenderInst, render_insts.count);
        FVec_RenderInst upload_insts = fvec_alloc(frame_arena, RenderInst, render_insts.count);
        FVec_RenderInst draw_insts = fvec_alloc(frame_arena, RenderInst, render_insts.count);
        for (u64 i = 0; i < render_insts.count; i++) {
            switch (A(render_insts, i).type) {
            case RenderInstType_ClearTexture: {
                fvec_push(&clear_texture_insts, A(render_insts, i));
                break;
            }
            case RenderInstType_Upload: {
                fvec_push(&upload_insts, A(render_insts, i));
                break;
            }
            case RenderInstType_Draw: {
                fvec_push(&draw_insts, A(render_insts, i));
                break;
            }
            }
        }

        // Hacky scroll
        for (u64 inst_idx = 0; inst_idx < draw_insts.count; inst_idx++) {
            Arr_Vertex vertices = A(draw_insts, inst_idx).mesh.vertices;
            for (u64 i = 0; i < vertices.count; i++) {
                A(vertices, i).y -= app->scroll * 0.1f;
            }
        }

        if (clear_texture_insts.count > 0) {
            do_clear_texture_passes(render_state, command_buffer, fvec_arr(&clear_texture_insts));
        }
        if (upload_insts.count > 0) {
            do_upload_texture_passes(render_state, command_buffer, fvec_arr(&upload_insts));
        }
        if (draw_insts.count > 0) {
            do_draw_passes(render_state, command_buffer, swapchain_texture, fvec_arr(&draw_insts));
        }
    }

    SDL_SubmitGPUCommandBuffer(command_buffer);
    arena_release(frame_arena);
}

fn void do_clear_texture_passes(RenderState *render_state,
                                SDL_GPUCommandBuffer *command_buffer,
                                Arr_RenderInst clear_texture_insts) {
    for (u64 i = 0; i < clear_texture_insts.count; i++) {
        SDL_GPUColorTargetInfo color_target_infos[] = {{
            .texture = A(clear_texture_insts, i).texture,
            .clear_color = {0.f, 0.f, 0.f, 0.f},
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
        }};
        SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(
            command_buffer, color_target_infos, c_arr_count(color_target_infos), nullptr);
        SDL_EndGPURenderPass(render_pass);
    }
}

fn void do_upload_texture_passes(RenderState *render_state,
                                 SDL_GPUCommandBuffer *command_buffer,
                                 Arr_RenderInst upload_insts) {
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    for (u64 inst_idx = 0; inst_idx < upload_insts.count; inst_idx++) {
        RenderInst *inst = &A(upload_insts, inst_idx);

        for (u64 texture_idx = 0; texture_idx < inst->texture_uploads.count; texture_idx++) {
            TextureUpload *upload = &A(inst->texture_uploads, texture_idx);

            SDL_GPUTextureTransferInfo src = {
                .transfer_buffer = upload->transfer_buffer,
                .offset = upload->transfer_buffer_offset,
            };
            SDL_GPUTextureRegion dest = {
                .texture = inst->texture,
                .mip_level = 0,
                .layer = 0,
                .x = (u32)upload->dest.x,
                .y = (u32)upload->dest.y,
                .z = 0,
                .w = (u32)upload->dest.w,
                .h = (u32)upload->dest.h,
                .d = 1,
            };

            SDL_UploadToGPUTexture(copy_pass, &src, &dest, false);
        }
    }
    SDL_EndGPUCopyPass(copy_pass);
}

fn void do_upload_geometry_pass(RenderState *render_state,
                                SDL_GPUCommandBuffer *command_buffer,
                                u32 vertex_count,
                                u32 index_count) {
    u32 vertex_data_size = vertex_count * sizeof(Vertex);
    u32 index_data_size = index_count * sizeof(u16);

    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(command_buffer);

    // Upload vertex data
    SDL_GPUTransferBufferLocation vert_src = {
        .transfer_buffer = render_state->geom_transfer_buffer,
        .offset = 0,
    };
    SDL_GPUBufferRegion vert_dest = {
        .buffer = render_state->vertex_buffer,
        .offset = 0,
        .size = vertex_data_size,
    };
    SDL_UploadToGPUBuffer(pass, &vert_src, &vert_dest, true);

    // Upload index data
    SDL_GPUTransferBufferLocation index_src = {
        .transfer_buffer = render_state->geom_transfer_buffer,
        .offset = (u32)(quad_vertices(MAX_QUAD_COUNT) * sizeof(Vertex)),
    };
    SDL_GPUBufferRegion index_dest = {
        .buffer = render_state->index_buffer,
        .offset = 0,
        .size = index_data_size,
    };
    SDL_UploadToGPUBuffer(pass, &index_src, &index_dest, true);

    SDL_EndGPUCopyPass(pass);
}

fn void do_draw_pass(SDL_GPUCommandBuffer *command_buffer,
                     SDL_GPUTexture *swapchain_texture,
                     SDL_GPUBuffer *vertex_buffer,
                     SDL_GPUBuffer *index_buffer,
                     Arr_RenderInst render_insts,
                     Arr_VertexBufferRegion regions) {
    SDL_GPUColorTargetInfo color_target_infos[] = {{
        .texture = swapchain_texture,
        .clear_color = {0.f, 0.f, 0.f, 1.f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    }};
    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(
        command_buffer, color_target_infos, c_arr_count(color_target_infos), nullptr);

    SDL_GPUBufferBinding vertex_buffer_bindings[] = {{.buffer = vertex_buffer, .offset = 0}};
    SDL_GPUBufferBinding index_buffer_binding = {.buffer = index_buffer, .offset = 0};
    SDL_BindGPUVertexBuffers(render_pass, 0, vertex_buffer_bindings,
                             c_arr_count(vertex_buffer_bindings));
    SDL_BindGPUIndexBuffer(render_pass, &index_buffer_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    for (u64 inst_idx = 0; inst_idx < render_insts.count; inst_idx++) {
        RenderInst *inst = &A(render_insts, inst_idx);
        VertexBufferRegion region = A(regions, inst_idx);

        if (inst->mesh.indices.count > 0) {
            SDL_GPUTextureSamplerBinding tex_sampler_bindings[] = {{
                .texture = inst->texture,
                .sampler = inst->sampler,
            }};
            SDL_BindGPUGraphicsPipeline(render_pass, inst->pipeline);
            SDL_BindGPUFragmentSamplers(render_pass, 0, tex_sampler_bindings,
                                        c_arr_count(tex_sampler_bindings));
            SDL_DrawGPUIndexedPrimitives(render_pass,
                                         region.index_count,        // Index count
                                         1,                         // Instance count
                                         region.first_index,        // First index
                                         (i32)region.first_vertex,  // Vertex offset
                                         0                          // First instance
            );
        }
    }

    SDL_EndGPURenderPass(render_pass);
}

fn void do_draw_passes(RenderState *render_state,
                       SDL_GPUCommandBuffer *command_buffer,
                       SDL_GPUTexture *swapchain_texture,
                       Arr_RenderInst draw_insts) {
    Arena *scratch = arena_acquire();

    void *transfer_data = (Vertex *)SDL_MapGPUTransferBuffer(
        render_state->device, render_state->geom_transfer_buffer, true);
    FVec_Vertex vertices = {
        .ptr = transfer_data,
        .capacity = quad_vertices(MAX_QUAD_COUNT),
    };
    FVec_u16 indices = {
        .ptr = (u16 *)((Vertex *)transfer_data + quad_vertices(MAX_QUAD_COUNT)),
        .capacity = quad_indices(MAX_QUAD_COUNT),
    };

    Arr_VertexBufferRegion regions = arena_push_arr(scratch, VertexBufferRegion, draw_insts.count);
    for (u64 i = 0; i < draw_insts.count; i++) {
        A(regions, i).first_vertex = (u32)vertices.count;
        A(regions, i).first_index = (u32)indices.count;
        A(regions, i).index_count = (u32)A(draw_insts, i).mesh.indices.count;

        fvec_extend(&vertices, A(draw_insts, i).mesh.vertices);
        fvec_extend(&indices, A(draw_insts, i).mesh.indices);
    }
    SDL_UnmapGPUTransferBuffer(render_state->device, render_state->geom_transfer_buffer);
    if (indices.count > 0) {
        do_upload_geometry_pass(render_state, command_buffer, (u32)vertices.count,
                                (u32)indices.count);
    }

    do_draw_pass(command_buffer, swapchain_texture, render_state->vertex_buffer,
                 render_state->index_buffer, draw_insts, regions);

    arena_release(scratch);
}
