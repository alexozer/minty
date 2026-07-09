#pragma once

#include "base.h"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_keycode.h>
#include <stb_rect_pack.h>
#include <xao.h>

#include <ft2build.h>
#include FT_FREETYPE_H

//
// ::Constants
//

// TODO toggle through build system or something
constexpr bool RENDERER_DEBUG_MODE_ENABLED = true;

constexpr u64 MAX_QUAD_COUNT = 2048;
constexpr u64 MAX_VERTEX_COUNT = MAX_QUAD_COUNT * 4;
constexpr u64 MAX_INDEX_COUNT = MAX_QUAD_COUNT * 6;

struct SizePX {
    u16 w, h;
};
derive_struct(SizePX);

constexpr SizePX DEFAULT_WINDOW_SIZE = {360, 600};
constexpr SizePX MIN_WINDOW_SIZE = {200, 100};

constexpr SDL_GPUTextureFormat ICON_TEXTURE_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
constexpr SDL_GPUTextureFormat GLYPH_TEXTURE_FORMAT = SDL_GPU_TEXTUREFORMAT_R8_UNORM;

//
// ::Types
//

derive_containers(Opt_Duration);

struct TextureHandle {
    u16 idx;
    u16 generation;
};
derive_struct(TextureHandle);

// Texture in CPU memory
struct Texture {
    SDL_GPUTextureFormat format;
    Arr_u8 buffer;
    SizePX dims;
    TextureHandle handle;  // Automatically populated/replaced by texture cache
};
derive_struct(Texture);

struct SplitRecord {
    u64 attempt_num;
    Arr_Opt_Duration splits;
};
derive_struct(SplitRecord);

struct SegmentDef {
    Str name;
    Arr_u8 icon_png;  // Icon in PNG format
    Texture icon_texture;
};
derive_struct(SegmentDef);

struct FileDef {
    Str game_name;
    Str category_name;
    u64 total_attempts;
    u64 completed_attempts;
    Arr_SegmentDef segments;
    SplitRecord personal_best;
    Arr_Opt_Duration golds;
};
derive_struct(FileDef);

enum TimerMode {
    TimerMode_Init,
    TimerMode_Running,
    TimerMode_Paused,
    TimerMode_Finished,
};
derive_enum(TimerMode);

struct Timer {
    TimerMode mode;
    Vec_Opt_Duration live_splits;
    Texture Texture;
    Instant start_time;
    Instant paused_time;
    Duration total_paused_duration;
};
derive_struct(Timer);

struct Session {
    FileDef file;
    Timer timer;
};
derive_struct(Session);

struct SegSummary {
    Opt_Duration live_split;
    Opt_Duration live_seg;

    // How far ahead/behind this split is compared to PB
    Opt_Duration live_delta;

    // Duration gained or lost this split relative to PB
    Opt_Duration gained;

    Opt_Duration pb_split;
    Opt_Duration pb_seg;

    bool is_new_gold;
};
derive_struct(SegSummary);

enum TimerAction {
    TimerAction_Split,
    TimerAction_UndoSplit,
    TimerAction_DeleteSplit,
    TimerAction_ResetAndSave,
    TimerAction_ResetAndDelete,
    TimerAction_Pause,
};
derive_enum(TimerAction);

enum ShaderStage {
    ShaderStage_Vertex,
    ShaderStage_Fragment,
};
derive_enum(ShaderStage);

// Icons don't need color, but it's simpler to just have one format for now
struct Vertex {
    f32 x, y, z;
    f32 u, v;
    u8 r, g, b, a;
};
derive_struct(Vertex);

struct Color {
    u8 r, g, b, a;
};
derive_struct(Color);

constexpr Color COLOR_WHITE = {.r = 0xff, .g = 0xff, .b = 0xff, .a = 0xff};
constexpr Color COLOR_BLACK = {.r = 0, .g = 0, .b = 0, .a = 0xff};
constexpr Color COLOR_RED = {.r = 0xff, .g = 0, .b = 0, .a = 0xff};
constexpr Color COLOR_GREEN = {.r = 0, .g = 0xff, .b = 0, .a = 0xff};
constexpr Color COLOR_BLUE = {.r = 0, .g = 0, .b = 0xff, .a = 0xff};

enum Axis {
    Axis_X,
    Axis_Y,
};
derive_enum(Axis);

struct PosPX {
    u16 x, y;
};
derive_struct(PosPX);

struct RectPX {
    u16 x, y;
    u16 w, h;
};
derive_struct(RectPX);

union PosF {
    f32 dims[2];
    struct {
        f32 x, y;
    };
};
derive_union(PosF);

union SizeF {
    f32 dims[2];
    struct {
        f32 w, h;
    };
};
derive_union(SizeF);

union RectF {
    struct {
        PosF pos;
        SizeF size;
    };
    struct {
        f32 x, y;
        f32 w, h;
    };
};
derive_union(RectF);

derive_struct(stbrp_node);
derive_struct(stbrp_rect);

struct Atlas {
    SizePX size;
    SDL_GPUTexture *texture;
    SDL_GPUSampler *sampler;
    SDL_GPUTransferBuffer *transfer_buffer;
    u64 transfer_buffer_size;

    // Rect packer state for incremental repacking
    stbrp_context packer_ctx;
    Arr_stbrp_node packer_nodes;
    Vec_stbrp_rect packer_rects;
};
derive_struct(Atlas);

struct GlyphMetrics {
    f32 bearing_px_x;  // Distance from left start to glyph start
    f32 bearing_px_y;  // Distance from baseline to top of glyph
};
derive_struct(GlyphMetrics);

struct GlyphAtlas {
    Atlas *atlas;
    u16 px_per_em;  // AKA the face size in pixels
    u16 units_per_em;
    Arr_GlyphMetrics metrics;
};
derive_struct(GlyphAtlas);

struct Mesh {
    Arr_Vertex vertices;
    Arr_u16 indices;
};
derive_struct(Mesh);

struct MeshBuilder {
    FVec_Vertex vertices;
    FVec_u16 indices;
};
derive_struct(MeshBuilder);

struct ShapedGlyph {
    u32 glyph_id;
    i32 glyph_x_fu;  // fu = font unit
    i32 glyph_y_fu;
};
derive_struct(ShapedGlyph);

enum QuadRequestType : u8 {
    QuadRequestType_Texture,
    QuadRequestType_Text,
};
derive_enum(QuadRequestType);

// Output of UI system
struct QuadRequest {
    QuadRequestType type;
    u8 order;
    RectF transform;
    Texture *texture;
    Str text_content;
    Color color;
};
derive_struct(QuadRequest);

struct TextureCacheEntry {
    TextureHandle handle;
    RectPX placement;
    // We know which atlas it is based on the texture format
};
derive_struct(TextureCacheEntry);

struct TextureSystem {
    Atlas *icon_atlas;
    Atlas *glyph_atlas;
    FVec_TextureCacheEntry cache_entries;
    FVec_TextureHandle free_handles;
    bool textures_cleared;
    SDL_GPUGraphicsPipeline *clear_icon_pipeline;
    SDL_GPUGraphicsPipeline *clear_glyph_pipeline;
    Texture dummy_texture;
};
derive_struct(TextureSystem);

struct TextureRequest {
    // No texture means plain colored rectangle.
    // TODO: make optional pointer types use nullptr as None (simple with union?)
    Opt_P_Texture texture;
    RectF transform;
    Color top_left_color;
    Color top_right_color;
    Color bottom_left_color;
    Color bottom_right_color;
};
derive_struct(TextureRequest);

enum BlendType {
    BlendType_None,
    BlendType_Over,
};
derive_enum(BlendType);

enum FilterType {
    FilterType_Nearest,
    FilterType_Linear,
};
derive_enum(FilterType);

enum BoxType {
    BoxType_Empty,
    BoxType_Text,
    BoxType_Texture,
    BoxType_SolidColor,
    BoxType_TopToBottomStack,
    BoxType_LeftToRightStack,
    BoxType_BackToFrontStack,
};
derive_enum(BoxType);

// Try some fat struct stuff?
derive_struct_pre(Box);
struct Box {
    BoxType type;
    Opt_SizePX bbox;
    u64 texture_idx;
    Color color;
    Vec_P_Box children;
};
derive_struct_post(Box);

// New UI stuff

enum UI_DimType : u8 {
    UI_DimType_FixedPX,
    UI_DimType_Flex,
};
derive_enum(UI_DimType);

struct UI_Dim {
    UI_DimType type;
    f32 value;  // Might be absolute size in pixels, flex ratio, etc.
};
derive_struct(UI_Dim);

union UI_Size {
    struct {
        UI_Dim w, h;
    };
    UI_Dim dims[2];
};
derive_union(UI_Size);

enum UI_Flag : u16 {
    UI_Flag_ChildLayoutX = bit(0),
    UI_Flag_ChildLayoutY = bit(1),
    UI_Flag_DrawText = bit(2),
    UI_Flag_DrawTexture = bit(3),
    UI_Flag_TextAlignLeft = bit(4),
    UI_Flag_TextAlignRight = bit(5),
};
derive_enum(UI_Flag);

derive_struct_pre(UI_Box);
struct UI_Box {
    UI_Size input_size;
    Str text_content;
    UI_Flag flags;
    Texture *texture;

    UI_Box *parent;
    Vec_P_UI_Box childs;

    RectF output_size;

    Str id;
};
derive_struct_post(UI_Box);

enum RenderInstType : u8 {
    RenderInstType_ClearTexture,
    RenderInstType_Upload,
    RenderInstType_Draw,
};
derive_enum(RenderInstType);

struct TextureUpload {
    SDL_GPUTransferBuffer *transfer_buffer;
    u32 transfer_buffer_offset;
    RectPX dest;
};
derive_struct(TextureUpload);

// Inspired by GfxRenderInst concept from noclip.website
struct RenderInst {
    RenderInstType type;
    u16 order;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUTexture *texture;
    SDL_GPUSampler *sampler;
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    Mesh mesh;
    Arr_TextureUpload texture_uploads;
};
derive_struct(RenderInst);

struct VertexBufferRegion {
    u32 first_vertex;
    u32 first_index;
    u32 index_count;
};
derive_struct(VertexBufferRegion);

// Shared GPU resources etc.
// How much state should be moved to texture system? Unclear
struct RenderState {
    SDL_GPUDevice *device;

    // Shaders
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *icon_frag_shader;
    SDL_GPUShader *glyph_frag_shader;

    // Pipelines
    SDL_GPUGraphicsPipeline *icon_pipeline;
    SDL_GPUGraphicsPipeline *glyph_pipeline;
    SDL_GPUGraphicsPipeline *clear_icon_pipeline;
    SDL_GPUGraphicsPipeline *clear_glyph_pipeline;

    // Geometry buffers
    SDL_GPUTransferBuffer *geom_transfer_buffer;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
};
derive_struct(RenderState);

struct App {
    Arena *app_arena;  // Lives for duration of application
    SDL_Window *window;

    SDL_Keycode prev_keys;
    // TODO: float-based scrolling on NDC could mess with pixel-perfect alignment
    f32 scroll;
    f32 scale;  // `scale + 1.0f` is actual scale
    bool insert_mode_enabled;
    Vec_u8 typed_text;

    Arena *session_arena;
    Session *session;  // Nullable

    RenderState *render_state;
    TextureSystem *texture_system;

    // Text stuff
    FT_Library freetype;
    Arr_u8 font_file;
};
derive_struct(App);
