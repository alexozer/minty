#include "gpu_utils.h"
#include "platform.h"

fn SDL_GPUGraphicsPipeline *gpu_make_pipeline(ErrorContext *err,
                                              SDL_GPUDevice *device,
                                              SDL_GPUShader *vert_shader,
                                              SDL_GPUShader *frag_shader,
                                              SDL_GPUTextureFormat target_texture_format,
                                              BlendType blend_type) {
    if (!device) return nullptr;
    if (!vert_shader) return nullptr;
    if (!frag_shader) return nullptr;
    Scope scope = scope_open(err);

    SDL_GPUColorTargetBlendState blend_state = {};
    if (blend_type == BlendType_Over) {
        blend_state = (SDL_GPUColorTargetBlendState){
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
        };
    }

    SDL_GPUColorTargetDescription color_target_descs[] = {{
        .format = target_texture_format,
        .blend_state = blend_state,
    }};

    SDL_GPUVertexBufferDescription vertex_buffer_descs[] = {{
        .slot = 0,
        .pitch = sizeof(Vertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    }};

    SDL_GPUVertexAttribute vertex_attrs[] = {
        {
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0,
        },
        {
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = sizeof(f32) * 3,
        },
        {
            .location = 2,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
            .offset = sizeof(f32) * 5,
        },
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeline_create_info = {
        .vertex_shader = vert_shader,
        .fragment_shader = frag_shader,
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = vertex_buffer_descs,
                .num_vertex_buffers = c_arr_count(vertex_buffer_descs),
                .vertex_attributes = vertex_attrs,
                .num_vertex_attributes = c_arr_count(vertex_attrs),
            },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info =
            {
                .color_target_descriptions = color_target_descs,
                .num_color_targets = c_arr_count(color_target_descs),
            },
    };

    SDL_GPUGraphicsPipeline *pipeline =
        SDL_CreateGPUGraphicsPipeline(device, &pipeline_create_info);
    if (!pipeline) {
        err_report(err, "%s", SDL_GetError());
    }

    scope_close(scope, "Create render pipeline");
    return pipeline;
}

fn SDL_GPUShader *gpu_load_shader(ErrorContext *err,
                                  SDL_GPUDevice *device,
                                  Arr_u8 source,
                                  ShaderStage type) {
    Scope scope = scope_open(err);
    Arena *scratch = arena_acquire();

    SDL_GPUShaderCreateInfo info = {};
    switch (type) {
    case ShaderStage_Vertex: {
        info = (SDL_GPUShaderCreateInfo){
            .code_size = source.count,
            .code = (u8 *)source.ptr,
            .format = OS_SHADER_FORMAT,
            .stage = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers = 0,
            .num_storage_textures = 0,
            .num_storage_buffers = 0,
            .num_uniform_buffers = 0,
            .props = 0,
        };
        break;
    }
    case ShaderStage_Fragment: {
        info = (SDL_GPUShaderCreateInfo){
            .code_size = source.count,
            .code = (u8 *)source.ptr,
            .format = OS_SHADER_FORMAT,
            .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
            .num_samplers = 1,
            .num_storage_textures = 0,
            .num_storage_buffers = 0,
            .num_uniform_buffers = 0,
            .props = 0,
        };
        break;
    }
    }
    SDL_GPUShader *shader = SDL_CreateGPUShader(device, &info);
    if (!shader) {
        err_report(err, "%s", SDL_GetError());
    }

    arena_release(scratch);
    scope_close(scope, "Load shader");
    return shader;
}

fn SDL_GPUBuffer *sdl_create_gpu_buffer(ErrorContext *err,
                                        SDL_GPUDevice *device,
                                        SDL_GPUBufferCreateInfo *info,
                                        Str name) {
    if (!device) return nullptr;
    Scope scope = scope_open(err);
    Arena *scratch = arena_acquire();

    SDL_GPUBuffer *gpu_buffer = SDL_CreateGPUBuffer(device, info);
    if (!gpu_buffer) {
        err_report(err, "%s", SDL_GetError());
    } else {
        SDL_SetGPUBufferName(device, gpu_buffer, str_to_c(scratch, name));
    }

    arena_release(scratch);
    scope_close(scope, "Create buffer '%.*s'", SF(name));
    return gpu_buffer;
}

fn SDL_GPUTransferBuffer *sdl_create_gpu_transfer_buffer(ErrorContext *err,
                                                         SDL_GPUDevice *device,
                                                         SDL_GPUTransferBufferCreateInfo *info) {
    if (!device) return nullptr;
    Scope scope = scope_open(err);

    SDL_GPUTransferBuffer *buffer = SDL_CreateGPUTransferBuffer(device, info);
    if (!buffer) {
        err_report(err, "%s", SDL_GetError());
    }

    scope_close(scope, "Create GPU transfer buffer");
    return buffer;
}

fn SDL_GPUCommandBuffer *sdl_acquire_gpu_command_buffer(ErrorContext *err, SDL_GPUDevice *device) {
    Scope scope = scope_open(err);

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
    if (!command_buffer) {
        err_report(err, "%s", SDL_GetError());
    }

    scope_close(scope, "Acquire GPU command buffer");
    return command_buffer;
}

fn void sdl_submit_gpu_command_buffer(SDL_GPUCommandBuffer *command_buffer) {
    if (command_buffer) SDL_SubmitGPUCommandBuffer(command_buffer);
}

fn u64 quad_vertices(u64 quad_count) {
    return quad_count * 4;
}

fn u64 quad_indices(u64 quad_count) {
    return quad_count * 6;
}
