#include "texture.h"
#include "gpu_utils.h"
#include "types.h"

// TODO We may not want a max texture count...
constexpr u64 MAX_TEXTURES = 4096;

fn TextureSystem *tex_init(ErrorContext *err,
                           Arena *arena,
                           SDL_GPUDevice *device,
                           SDL_GPUShader *vertex_shader,
                           SDL_GPUShader *dummy_fragment_shader) {
    Scope scope = scope_open(err);

    TextureSystem *ctx = arena_push(arena, TextureSystem);
    ctx->icon_atlas = tex_init_atlas(err, arena, device, S("Icon Atlas"), (SizePX){2048, 2048},
                                     ICON_TEXTURE_FORMAT, FilterType_Linear);
    ctx->glyph_atlas = tex_init_atlas(err, arena, device, S("Glyph Atlas"), (SizePX){1024, 1024},
                                      GLYPH_TEXTURE_FORMAT, FilterType_Nearest);

    ctx->clear_icon_pipeline = gpu_make_pipeline(err, device, vertex_shader, dummy_fragment_shader,
                                                 ICON_TEXTURE_FORMAT, BlendType_None);
    ctx->clear_glyph_pipeline = gpu_make_pipeline(err, device, vertex_shader, dummy_fragment_shader,
                                                  GLYPH_TEXTURE_FORMAT, BlendType_None);

    ctx->cache_entries = fvec_alloc(arena, TextureCacheEntry, MAX_TEXTURES);
    fvec_push_zero(&ctx->cache_entries);  // Zero handle is invalid, reserve slot 0
    ctx->free_handles = fvec_alloc(arena, TextureHandle, MAX_TEXTURES);

    scope_close(scope, "Initialize texture system");
    return ctx;
}

fn Atlas *tex_init_atlas(ErrorContext *err,
                         Arena *arena,
                         SDL_GPUDevice *device,
                         Str name,
                         SizePX atlas_size,
                         SDL_GPUTextureFormat texture_format,
                         FilterType filter) {
    Atlas *atlas = arena_push(arena, Atlas);
    Arena *scratch = arena_acquire();

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
    // size. Here we're hoping in practice that this is big enough - we could also split uploads
    // across frames if needed (or use cycling to use multiple transfer buffers in a single frame?)
    SDL_GPUTransferBufferCreateInfo transfer_buffer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = atlas_size.w * atlas_size.h * 3 / 2,
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

fn Arr_P_RenderInst tex_prepare_to_render(Arena *frame_arena,
                                          TextureSystem *ctx,
                                          RenderState *render_state,
                                          SizePX window_size,
                                          Arr_TextureRequest requests) {
    FixedVec_P_RenderInst render_insts = fvec_alloc(frame_arena, P_RenderInst, (u64)8);
    if (!ctx->textures_cleared) {
        tex_build_clear_insts(frame_arena, ctx, render_state, &render_insts);
        ctx->textures_cleared = true;
    }
    tex_build_upload_insts(frame_arena, ctx, render_state, requests, &render_insts);
    tex_build_draw_insts(frame_arena, ctx, render_state, window_size, requests, &render_insts);

    return fvec_arr(&render_insts);
}

fn void tex_build_clear_insts(Arena *frame_arena,
                              TextureSystem *ctx,
                              RenderState *render_state,
                              FixedVec_P_RenderInst *render_insts) {
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

    fvec_push(render_insts, icon_clear_inst);
    fvec_push(render_insts, glyph_clear_inst);
}

fn void tex_build_upload_insts(Arena *frame_arena,
                               TextureSystem *ctx,
                               RenderState *render_state,
                               Arr_TextureRequest requests,
                               FixedVec_P_RenderInst *render_insts) {
    Arena *scratch = arena_acquire();

    FixedVec_P_CPUTexture uncached_icon_requests =
        fvec_alloc(scratch, P_CPUTexture, requests.count);
    FixedVec_P_CPUTexture uncached_glyph_requests =
        fvec_alloc(scratch, P_CPUTexture, requests.count);
    for (u64 i = 0; i < requests.count; i++) {
        TextureRequest *request = &A(requests, i);
        CPUTexture *texture = request->texture;

        if (texture->handle.idx == 0) {
            if (texture->format == ICON_TEXTURE_FORMAT) {
                fvec_push(&uncached_icon_requests, texture);
            } else if (texture->format == GLYPH_TEXTURE_FORMAT) {
                fvec_push(&uncached_glyph_requests, texture);
            } else {
                log_fatal("Unexpected texture format: %d", texture->format);
            }
        }
    }

    if (uncached_icon_requests.count > 0) {
        pack_textures_into_existing_atlas(frame_arena, ctx, ctx->icon_atlas, render_state,
                                          fvec_arr(&uncached_icon_requests), render_insts);
    }
    if (uncached_glyph_requests.count > 0) {
        pack_textures_into_existing_atlas(frame_arena, ctx, ctx->glyph_atlas, render_state,
                                          fvec_arr(&uncached_glyph_requests), render_insts);
    }

    arena_release(scratch);
}

fn TextureCacheEntry *alloc_cache_entry(TextureSystem *ctx) {
    if (ctx->free_handles.count > 0) {
        TextureHandle handle = fvec_pop(&ctx->free_handles);
        TextureCacheEntry *entry = &A(ctx->cache_entries, handle.idx);
        entry->handle = handle;
        return entry;

    } else {
        TextureHandle handle = {.idx = (u16)ctx->cache_entries.count};
        TextureCacheEntry *entry = fvec_push_zero(&ctx->cache_entries);
        entry->handle = handle;
        return entry;
    }
}

fn void pack_textures_into_existing_atlas(Arena *frame_arena,
                                          TextureSystem *ctx,
                                          Atlas *atlas,
                                          RenderState *render_state,
                                          Arr_P_CPUTexture textures,
                                          FixedVec_P_RenderInst *render_insts) {
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
        log_assert(A(textures, i)->dims.w > 0);
        log_assert(A(textures, i)->dims.h > 0);
        A(placements, i).x = (u16)A(packer_rects, i).x + 1;
        A(placements, i).y = (u16)A(packer_rects, i).y + 1;
        A(placements, i).w = A(textures, i)->dims.w;
        A(placements, i).h = A(textures, i)->dims.h;
    }

    //
    // Record texture cache entries
    //
    for (u64 i = 0; i < textures.count; i++) {
        TextureCacheEntry *entry = alloc_cache_entry(ctx);
        entry->placement = A(placements, i);
        A(textures, i)->handle = entry->handle;
    }

    //
    // Pack textures into transfer buffer
    //

    void *ptr = SDL_MapGPUTransferBuffer(render_state->device, atlas->transfer_buffer, true);
    Arr_u8 arr = {.ptr = ptr, .count = atlas->transfer_buffer_size};
    Packer packer = packer_from_arr(arr);

    Vec_u32 offsets = {};
    vec_prealloc(scratch, &offsets, textures.count);

    for (u64 i = 0; i < textures.count; i++) {
        Opt_u64 offset = packer_try_push(&packer, A(textures, i)->buffer, 512);
        log_assert(offset.present);  // TODO
        vec_push(scratch, &offsets, (u32)offset.opt);
    }
    SDL_UnmapGPUTransferBuffer(render_state->device, atlas->transfer_buffer);

    //
    // Build texture upload render insts
    //

    RenderInst *inst = arena_push(frame_arena, RenderInst);
    inst->type = RenderInstType_Upload;
    inst->texture = atlas->texture;
    inst->texture_uploads = arena_push_arr(frame_arena, TextureUpload, offsets.count);

    for (u64 i = 0; i < offsets.count; i++) {
        A(inst->texture_uploads, i).transfer_buffer = atlas->transfer_buffer;
        A(inst->texture_uploads, i).transfer_buffer_offset = A(offsets, i);
        A(inst->texture_uploads, i).dest = A(placements, i);
    }

    fvec_push(render_insts, inst);

    arena_release(scratch);
}

fn void tex_build_draw_insts(Arena *frame_arena,
                             TextureSystem *ctx,
                             RenderState *render_state,
                             SizePX window_size,
                             Arr_TextureRequest requests,
                             FixedVec_P_RenderInst *render_insts) {
    // Max vertex/index count are global limits, but are reused here as the per draw-call limit too
    // - there'll be some unused space for sure.
    MeshBuilder icon_mesh = {
        .vertices = fvec_alloc(frame_arena, Vertex, MAX_VERTEX_COUNT),
        .indices = fvec_alloc(frame_arena, u16, MAX_INDEX_COUNT),
    };
    MeshBuilder glyph_mesh = {
        .vertices = fvec_alloc(frame_arena, Vertex, MAX_VERTEX_COUNT),
        .indices = fvec_alloc(frame_arena, u16, MAX_INDEX_COUNT),
    };

    for (u64 i = 0; i < requests.count; i++) {
        TextureRequest *req = &A(requests, i);

        // TODO think harder about when/how to round glyph coords to pixels. Don't want to end up
        // with a stretched glyph!
        // We also may not want to round non-glyph things to pixels, or may not want to during
        // animations...
        RectPX dest = {
            .x = (u16)SDL_lroundf(req->transform.x),
            .y = (u16)SDL_lroundf(req->transform.y),
            .w = (u16)SDL_lroundf(req->transform.w),
            .h = (u16)SDL_lroundf(req->transform.h),
        };

        RectPX src = A(ctx->cache_entries, req->texture->handle.idx).placement;
        if (req->texture->format == ICON_TEXTURE_FORMAT) {
            push_atlas_quad(window_size, ctx->icon_atlas, &icon_mesh, src, dest, req->color);
        } else if (req->texture->format == GLYPH_TEXTURE_FORMAT) {
            push_atlas_quad(window_size, ctx->glyph_atlas, &glyph_mesh, src, dest, req->color);
        }
    }

    RenderInst *icon_inst = arena_push(frame_arena, RenderInst);
    icon_inst->type = RenderInstType_Draw;
    icon_inst->order = 0;
    icon_inst->pipeline = render_state->icon_pipeline;
    icon_inst->texture = ctx->icon_atlas->texture;
    icon_inst->sampler = ctx->icon_atlas->sampler;
    icon_inst->vertex_shader = render_state->vertex_shader;
    icon_inst->fragment_shader = render_state->icon_frag_shader;
    icon_inst->mesh = (Mesh){
        .vertices = fvec_arr(&icon_mesh.vertices),
        .indices = fvec_arr(&icon_mesh.indices),
    };
    fvec_push(render_insts, icon_inst);

    RenderInst *glyph_inst = arena_push(frame_arena, RenderInst);
    glyph_inst->type = RenderInstType_Draw;
    glyph_inst->order = 0;
    glyph_inst->pipeline = render_state->glyph_pipeline;
    glyph_inst->texture = ctx->glyph_atlas->texture;
    glyph_inst->sampler = ctx->glyph_atlas->sampler;
    glyph_inst->vertex_shader = render_state->vertex_shader;
    glyph_inst->fragment_shader = render_state->glyph_frag_shader;
    glyph_inst->mesh = (Mesh){
        .vertices = fvec_arr(&glyph_mesh.vertices),
        .indices = fvec_arr(&glyph_mesh.indices),
    };
    fvec_push(render_insts, glyph_inst);
}

fn void push_atlas_quad(SizePX window_size,
                        Atlas *atlas,
                        MeshBuilder *mesh,
                        RectPX src,
                        RectPX dst,
                        Color color) {
    Arr_u16 indices = fvec_extend_zero(&mesh->indices, 6);
    A(indices, 0) = (u16)(mesh->vertices.count + 0);
    A(indices, 1) = (u16)(mesh->vertices.count + 1);
    A(indices, 2) = (u16)(mesh->vertices.count + 2);
    A(indices, 3) = (u16)(mesh->vertices.count + 2);
    A(indices, 4) = (u16)(mesh->vertices.count + 1);
    A(indices, 5) = (u16)(mesh->vertices.count + 3);

    Arr_Vertex vertices = fvec_extend_zero(&mesh->vertices, 4);
    // Top left
    A(vertices, 0) = (Vertex){
        .x = (f32)dst.x,
        .y = (f32)dst.y,
        .z = 0,
        .u = (f32)src.x / (f32)atlas->size.w,
        .v = (f32)src.y / (f32)atlas->size.h,
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
    // Top right
    A(vertices, 1) = (Vertex){
        .x = (f32)(dst.x + dst.w),
        .y = (f32)dst.y,
        .z = 0,
        .u = (f32)(src.x + src.w) / (f32)atlas->size.w,
        .v = (f32)src.y / (f32)atlas->size.h,
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
    // Bottom left
    A(vertices, 2) = (Vertex){
        .x = (f32)dst.x,
        .y = (f32)(dst.y + dst.h),
        .z = 0,
        .u = (f32)src.x / (f32)atlas->size.w,
        .v = (f32)(src.y + src.h) / (f32)atlas->size.h,
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
    // Bottom right
    A(vertices, 3) = (Vertex){
        .x = (f32)(dst.x + dst.w),
        .y = (f32)(dst.y + dst.h),
        .z = 0,
        .u = (f32)(src.x + src.w) / (f32)atlas->size.w,
        .v = (f32)(src.y + src.h) / (f32)atlas->size.h,
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };

    // Kinda awkward but whatever
    window_to_ndc(&A(vertices, 0), window_size);
    window_to_ndc(&A(vertices, 1), window_size);
    window_to_ndc(&A(vertices, 2), window_size);
    window_to_ndc(&A(vertices, 3), window_size);
}

fn void window_to_ndc(Vertex *vertex, SizePX window_size) {
    vertex->x = (vertex->x / (f32)window_size.w) * 2.f - 1.f;
    vertex->y = -((vertex->y / (f32)window_size.h) * 2.f - 1.f);
}
