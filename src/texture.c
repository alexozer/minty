#include "texture.h"
#include "gpu_utils.h"

fn TextureSystem *tex_init(ErrorContext *err,
                           Arena *arena,
                           SDL_GPUDevice *device,
                           SDL_Window *window,
                           SDL_GPUShader *vertex_shader,
                           SDL_GPUShader *dummy_fragment_shader) {
    TextureSystem *ctx = arena_push(arena, TextureSystem);
    ctx->icon_atlas = init_atlas(err, arena, device, S("Icon Atlas"), (SizePX){2048, 2048},
                                 ICON_TEXTURE_FORMAT, FilterType_Linear);
    ctx->glyph_atlas = init_atlas(err, arena, device, S("Glyph Atlas"), (SizePX){1024, 1024},
                                  GLYPH_TEXTURE_FORMAT, FilterType_Nearest);

    ctx->clear_icon_pipeline = gpu_make_pipeline(err, device, vertex_shader, dummy_fragment_shader,
                                                 ICON_TEXTURE_FORMAT, BlendType_None);
    ctx->clear_glyph_pipeline = gpu_make_pipeline(err, device, vertex_shader, dummy_fragment_shader,
                                                  GLYPH_TEXTURE_FORMAT, BlendType_None);
    return ctx;
}

fn void clear_texture(SDL_GPUCommandBuffer *command_buffer,
                      SDL_GPUGraphicsPipeline *pipeline,
                      SDL_GPUTexture *texture) {
    SDL_GPUColorTargetInfo color_target_infos[] = {{
        .texture = texture,
        .clear_color = {0.f, 0.f, 0.f, 1.f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    }};
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command_buffer, color_target_infos,
                                                     c_arr_count(color_target_infos), nullptr);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_EndGPURenderPass(pass);
}

fn Atlas *init_atlas(ErrorContext *err,
                     Arena *arena,
                     SDL_GPUDevice *device,
                     Str name,
                     SizePX atlas_size,
                     SDL_GPUTextureFormat texture_format,
                     FilterType filter) {
    Atlas *atlas = arena_push(arena, Atlas);
    Arena *scratch = arena_acquire();

    // Allocate and clear GPU texture

    SDL_GPUTextureCreateInfo gpu_texture_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = texture_format,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = (u32)atlas_size.w,
        .height = (u32)atlas_size.h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &gpu_texture_info);
    if (!texture) {
        err_report(err, "%s", SDL_GetError());
    } else {
        char *name_cstr = str_to_c(scratch, name);
        SDL_SetGPUTextureName(device, texture, name_cstr);
    }

    // Make transfer buffer.
    // Note that we can't just size it to the size of the atlas due rect alignment bloating the
    // size.
    SDL_GPUTransferBufferCreateInfo transfer_buffer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = atlas_size.w * atlas_size.h / 2,
    };
    SDL_GPUTransferBuffer *transfer_buffer =
        SDL_CreateGPUTransferBuffer(device, &transfer_buffer_info);

    // Make sampler

    SDL_GPUSamplerCreateInfo sampler_info = {};
    switch (filter) {
    case FilterType_Linear: {
        sampler_info = (SDL_GPUSamplerCreateInfo){
            .min_filter = SDL_GPU_FILTER_LINEAR,
            .mag_filter = SDL_GPU_FILTER_LINEAR,
            .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        };
        break;
    }
    case FilterType_Nearest: {
        sampler_info = (SDL_GPUSamplerCreateInfo){
            .min_filter = SDL_GPU_FILTER_NEAREST,
            .mag_filter = SDL_GPU_FILTER_NEAREST,
            .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        };
        break;
    }
    }

    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &sampler_info);

    // Init rect packer state
    Arr_stbrp_node packer_nodes = arena_push_arr(arena, stbrp_node, atlas_size.w);
    stbrp_init_target(&atlas->packer_ctx, (i32)atlas_size.w, (i32)atlas_size.h, packer_nodes.ptr,
                      (i32)packer_nodes.count);

    // Fill atlas descriptor
    atlas->size = atlas_size;
    atlas->texture = texture;
    atlas->sampler = sampler;
    atlas->transfer_buffer = transfer_buffer;
    atlas->transfer_buffer_size = transfer_buffer_info.size;
    atlas->packer_nodes = packer_nodes;

    arena_release(scratch);
    return atlas;
}

fn void upload_texture(SDL_GPUCopyPass *copy_pass,
                       SDL_GPUTexture *texture,
                       SDL_GPUTransferBuffer *transfer_buffer,
                       u32 offset,
                       RectPX placement) {
    SDL_GPUTextureTransferInfo src = {
        .transfer_buffer = transfer_buffer,
        .offset = offset,
    };
    SDL_GPUTextureRegion dest = {
        .texture = texture,
        .mip_level = 0,
        .layer = 0,
        .x = (u32)placement.x,
        .y = (u32)placement.y,
        .z = 0,
        .w = (u32)placement.w,
        .h = (u32)placement.h,
        .d = 1,
    };
    SDL_UploadToGPUTexture(copy_pass, &src, &dest, false);
}

void pack_textures_into_existing_atlas(Atlas *atlas,
                                       SDL_GPUDevice *device,
                                       SDL_GPUCopyPass *copy_pass,
                                       Arr_P_CPUTexture textures) {
    Arena *scratch = arena_acquire();

    //
    // Compute packing (on top of existing packing skyline)
    //

    Arr_stbrp_rect packer_rects = arena_push_arr(scratch, stbrp_rect, textures.count);
    for (u64 i = 0; i < textures.count; i++) {
        A(packer_rects, i).id = (i32)i;
        A(packer_rects, i).w = A(textures, i)->dims.w + 2;
        A(packer_rects, i).h = A(textures, i)->dims.h + 2;
    }

    log_assert(stbrp_pack_rects(&atlas->packer_ctx, packer_rects.ptr, (i32)packer_rects.count) ==
               1);

    // TODO use only 1px of padding, not 2px
    Arr_RectPX placements = arena_push_arr(scratch, RectPX, textures.count);
    for (u64 i = 0; i < textures.count; i++) {
        if (A(textures, i)->dims.w > 0 && A(textures, i)->dims.h > 0) {
            A(placements, i).x = (u16)A(packer_rects, i).x + 1;
            A(placements, i).y = (u16)A(packer_rects, i).y + 1;
            A(placements, i).w = A(textures, i)->dims.w;
            A(placements, i).h = A(textures, i)->dims.h;
        }
    }

    //
    // Pack textures into transfer buffer
    //

    void *ptr = SDL_MapGPUTransferBuffer(device, atlas->transfer_buffer, true);
    Arr_u8 arr = {.ptr = ptr, .count = atlas->transfer_buffer_size};
    Packer packer = packer_from_arr(arr);

    Vec_u32 offsets = {};
    vec_prealloc(scratch, &offsets, textures.count);

    for (u64 i = 0; i < textures.count; i++) {
        Opt_u64 offset = packer_try_push(&packer, A(textures, i)->buffer, 512);
        log_assert(offset.present);  // TODO
        vec_push(scratch, &offsets, (u32)offset.opt);
    }
    SDL_UnmapGPUTransferBuffer(device, atlas->transfer_buffer);

    //
    // Upload textures
    //

    for (u64 i = 0; i < offsets.count; i++) {
        if (A(textures, i)->dims.w > 0 && A(textures, i)->dims.h > 0) {
            upload_texture(copy_pass, atlas->texture, atlas->transfer_buffer, A(offsets, i),
                           A(placements, i));
        }
    }

    arena_release(scratch);
}

// MeshOutput tex_build_mesh(TextureSystem *ctx,
//                           SDL_GPUDevice *device,
//                           SDL_GPUCommandBuffer *command_buffer,
//                           FixedVec_P_CPUTexture *requests) {
//     Arena *scratch = arena_acquire();
//     /*
//     For each request:
//         If request is not in cache (aka, packed into atlas)
//             Add to list to pack into atlas
//     For each new request:
//         Pack into atlas
//     If required, do each of these steps:
//         Clear icon and glyph textures in new render passes
//         Upload sprites to new textures in new copy passes
//     For each request:
//         Spit out quad
//    */
//     FixedVec_P_CPUTexture uncached_icon_requests =
//         fvec_alloc(scratch, P_CPUTexture, requests->count);
//     FixedVec_P_CPUTexture uncached_glyph_requests =
//         fvec_alloc(scratch, P_CPUTexture, requests->count);
//     for (u64 i = 0; i < requests->count; i++) {
//         CPUTexture *request = A(*requests, i);
//         if (request->handle.idx == 0) {
//             if (request->format == ICON_TEXTURE_FORMAT) {
//                 fvec_push(&uncached_icon_requests, request);
//             } else if (request->format == GLYPH_TEXTURE_FORMAT) {
//                 fvec_push(&uncached_glyph_requests, request);
//             } else {
//                 log_fatal("Unexpected texture format: %d", request->format);
//             }
//         }
//     }
//
//     if (uncached_icon_requests.count > 0 || uncached_glyph_requests.count > 0) {
//         SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);
//         pack_textures_into_existing_atlas(ctx->icon_atlas, device, copy_pass,
//                                           fvec_arr(&uncached_icon_requests));
//         pack_textures_into_existing_atlas(ctx->glyph_atlas, device, copy_pass,
//                                           fvec_arr(&uncached_glyph_requests));
//         SDL_EndGPUCopyPass(copy_pass);
//     }
//
//     arena_release(scratch);
// }

fn Arr_P_RenderInst tex_prepare_to_render(Arena *frame_arena,
                                          TextureSystem *ctx,
                                          RenderState *render_state,
                                          Arr_TextureRequest requests) {
    FixedVec_P_RenderInst render_insts = fvec_alloc(frame_arena, P_RenderInst, (u64)8);
    // if (!ctx->textures_cleared) {
    //     ctx->textures_cleared = true;

    // Clear atlas textures by performing render pass with no draw calls
    RenderInst *icon_clear_inst = arena_push(frame_arena, RenderInst);
    icon_clear_inst->type = RenderInstType_ClearTexture;
    icon_clear_inst->order = 0;
    icon_clear_inst->pipeline = render_state->clear_icon_pipeline;
    icon_clear_inst->texture = ctx->icon_atlas->texture;
    icon_clear_inst->sampler = ctx->icon_atlas->sampler;
    icon_clear_inst->vertex_shader = render_state->vertex_shader;
    icon_clear_inst->fragment_shader = render_state->icon_frag_shader;

    RenderInst *glyph_clear_inst = arena_push(frame_arena, RenderInst);
    glyph_clear_inst->type = RenderInstType_ClearTexture;
    glyph_clear_inst->order = 0;
    glyph_clear_inst->pipeline = render_state->clear_glyph_pipeline;
    glyph_clear_inst->texture = ctx->glyph_atlas->texture;
    glyph_clear_inst->sampler = ctx->glyph_atlas->sampler;
    glyph_clear_inst->vertex_shader = render_state->vertex_shader;
    glyph_clear_inst->fragment_shader = render_state->glyph_frag_shader;

    fvec_push(&render_insts, icon_clear_inst);
    fvec_push(&render_insts, glyph_clear_inst);
    // }

    return fvec_arr(&render_insts);
}

// void push_atlas_quad(SizePX window_size,
//                      Atlas *atlas,
//                      Mesh *mesh,
//                      RectPX src,
//                      RectPX dst,
//                      Color color) {
//     Arr_u16 indices = fvec_extend_zero(&mesh->indices, 6);
//     A(indices, 0) = (u16)(mesh->vertices.count + 0);
//     A(indices, 1) = (u16)(mesh->vertices.count + 1);
//     A(indices, 2) = (u16)(mesh->vertices.count + 2);
//     A(indices, 3) = (u16)(mesh->vertices.count + 2);
//     A(indices, 4) = (u16)(mesh->vertices.count + 1);
//     A(indices, 5) = (u16)(mesh->vertices.count + 3);
//
//     Arr_Vertex vertices = fvec_extend_zero(&mesh->vertices, 4);
//     // Top left
//     A(vertices, 0) = (Vertex){
//         .x = (f32)dst.x,
//         .y = (f32)dst.y,
//         .z = 0,
//         .u = (f32)src.x / (f32)atlas->size.w,
//         .v = (f32)src.y / (f32)atlas->size.h,
//         .r = color.r,
//         .g = color.g,
//         .b = color.b,
//         .a = color.a,
//     };
//     // Top right
//     A(vertices, 1) = (Vertex){
//         .x = (f32)(dst.x + dst.w),
//         .y = (f32)dst.y,
//         .z = 0,
//         .u = (f32)(src.x + src.w) / (f32)atlas->size.w,
//         .v = (f32)src.y / (f32)atlas->size.h,
//         .r = color.r,
//         .g = color.g,
//         .b = color.b,
//         .a = color.a,
//     };
//     // Bottom left
//     A(vertices, 2) = (Vertex){
//         .x = (f32)dst.x,
//         .y = (f32)(dst.y + dst.h),
//         .z = 0,
//         .u = (f32)src.x / (f32)atlas->size.w,
//         .v = (f32)(src.y + src.h) / (f32)atlas->size.h,
//         .r = color.r,
//         .g = color.g,
//         .b = color.b,
//         .a = color.a,
//     };
//     // Bottom right
//     A(vertices, 3) = (Vertex){
//         .x = (f32)(dst.x + dst.w),
//         .y = (f32)(dst.y + dst.h),
//         .z = 0,
//         .u = (f32)(src.x + src.w) / (f32)atlas->size.w,
//         .v = (f32)(src.y + src.h) / (f32)atlas->size.h,
//         .r = color.r,
//         .g = color.g,
//         .b = color.b,
//         .a = color.a,
//     };
//
//     // Kinda awkward but whatever
//     window_to_ndc(&A(vertices, 0), window_size);
//     window_to_ndc(&A(vertices, 1), window_size);
//     window_to_ndc(&A(vertices, 2), window_size);
//     window_to_ndc(&A(vertices, 3), window_size);
// }
