#pragma once

#include "base.h"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_keycode.h>
#include <kb_text_shape.h>
#include <stb_rect_pack.h>
#include <xao.h>

#include <ft2build.h>
#include FT_FREETYPE_H

//
// Constants
//

// TODO toggle through build system or something
constexpr bool RENDERER_DEBUG_MODE_ENABLED = true;

constexpr u64 MAX_QUAD_COUNT = 10000;

struct SizePX {
    u16 w, h;
};
derive_struct(SizePX);

constexpr SizePX DEFAULT_WINDOW_SIZE = {360, 600};
constexpr SizePX MIN_WINDOW_SIZE = {200, 100};

constexpr SDL_GPUTextureFormat ICON_TEXTURE_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
constexpr SDL_GPUTextureFormat GLYPH_TEXTURE_FORMAT = SDL_GPU_TEXTUREFORMAT_R8_UNORM;

//
// Types
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

struct SegmentDef {
    Str name;
    Arr_u8 icon_png;           // PNG/JPG/etc. encoded image
    Opt_Texture icon_texture;  // Decoded image
    Opt_Duration pb_split;
    Opt_Duration best_segment;
    Arr_Duration segment_history;
};
derive_struct(SegmentDef);

struct WallTime {
    Str time;
    bool ntp_synced;
};
derive_struct(WallTime);

struct Attempt {
    WallTime start_time;
    WallTime end_time;
    Opt_Duration run_duration;
};
derive_struct(Attempt);

struct FileDef {
    Opt_Texture game_icon;
    Str game_name;
    Str category_name;
    u64 total_attempts;
    u64 completed_attempts;
    Duration offset;
    Arr_SegmentDef segments;
    Arr_Attempt attempt_history;
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

struct SegSummary {
    Opt_Duration live_split;
    Opt_Duration live_segment;

    // How far ahead/behind this split is compared to PB
    Opt_Duration live_delta;

    // Duration gained or lost this split relative to PB
    Opt_Duration gained;

    Opt_Duration pb_split;
    Opt_Duration pb_segment;

    Opt_Duration best_segment;

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
constexpr Color COLOR_LIGHT_GRAY = {.r = 0xbf, .g = 0xbf, .b = 0xbf, .a = 0xff};
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
derive_type(P_RectF);

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
    // Glyph offsets in font units
    i32 x_fu;
    i32 y_fu;
    i32 x_advance_fu;
};
derive_struct(ShapedGlyph);
derive_type(Arr_ShapedGlyph);

struct ShapedGlyphPX {
    u32 glyph_id;
    PosF pos_px;
    f32 x_advance_px;
};
derive_struct(ShapedGlyphPX);
derive_type(Arr_ShapedGlyphPX);

struct GlyphBitmap {
    Texture texture;
    // Can actually go negative!
    i16 offset_x;
    i16 offset_y;
    RectF bbox;
};
derive_struct(GlyphBitmap);

struct GlyphBitmapSet {
    bool rendered;
    Arr_GlyphBitmap steps;
};
derive_struct(GlyphBitmapSet);

struct FontHandle {
    u16 idx;
    u16 generation;
};
derive_struct(FontHandle);

// TODO: more principled way to load/cache non-GPU resources like this
struct FontFile {
    FontHandle handle;
    Arr_u8 contents;
};
derive_struct(FontFile);

enum TextBBoxType {
    TextBBoxType_Glyph,
    TextBBoxType_Pen,
};
derive_enum(TextBBoxType);

struct Layout {
    Color text_color;
    Color background_color;
    Color personal_best_color;
    Color ahead_gaining_time_color;
    Color ahead_losing_time_color;
    Color behind_gaining_time_color;
    Color behind_losing_time_color;
    Color best_segment_color;
    Color not_running_color;
    Color paused_color;
    Color text_outline_color;
    Color shadows_color;

    FontFile times_font;
    FontFile timer_font;
    FontFile text_font;

    Opt_Texture background_image;

    FontFile nunito_sans_bold;
    FontFile kosugi_maru_regular;
    FontFile departure_mono_regular;
};
derive_struct(Layout);

struct Session {
    FileDef file;
    Layout layout;
    Timer timer;
};
derive_struct(Session);

struct FontInst {
    Arena *arena;

    FontHandle font_handle;
    FontFile *font_file;
    Str family_name;
    Str style_name;
    u32 px_per_em;  // AKA font size in pixels
    f32 outline_px;
    f32 center_y_px;
    Opt_u32 ellipsis_glyph_id;

    FT_Library ft_ctx;
    FT_Face ft_face;
    kbts_shape_context *kbts_ctx;

    // Keyed by glyph ID
    Arr_GlyphBitmapSet bitmap_sets;
};
derive_struct(FontInst);

derive_maps(Arr_ShapedGlyph);

struct FontSystem {
    Arena *arena;
    FVec_FontInst fonts;
    FontHandle last_handle;
    Maps_Arr_ShapedGlyph shape_cache;
};
derive_struct(FontSystem);

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

struct QuadRequest {
    // No texture means plain colored rectangle.
    // TODO: make optional pointer types use nullptr as None (simple with union?)
    Opt_P_Texture texture;
    RectF transform;
    Color top_left_color;
    Color top_right_color;
    Color bottom_left_color;
    Color bottom_right_color;
    u32 depth;
};
derive_struct(QuadRequest);

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

enum UI_Flag : u32 {
    UI_Flag_None = 0,
    UI_Flag_ChildLayoutX = bit(0),
    UI_Flag_ChildLayoutY = bit(1),
    UI_Flag_ChildLayoutZ = bit(2),
    UI_Flag_DrawText = bit(3),
    UI_Flag_DrawTexture = bit(4),
    UI_Flag_TextAlignLeft = bit(5),
    UI_Flag_TextAlignRight = bit(6),
    UI_Flag_TextAlignCenter = bit(7),
    UI_Flag_InsertChildAtIndex = bit(8),
    UI_Flag_ClipChilds = bit(9),
    UI_Flag_TextureContain = bit(10),
    UI_Flag_TextureZoom = bit(11),
    UI_Flag_TextureBlendColor = bit(12),
    UI_Flag_TextClipEllipsis = bit(13),
    UI_Flag_IgnoreUserScale = bit(14),
    UI_Flag_DrawColoredBG = bit(15),
    // Useful for placing two text blocks next to each other
    UI_Flag_TextBBoxPen = bit(16),
    UI_Flag_TextBBoxGlyph = bit(17),
};
derive_enum(UI_Flag);

derive_struct_pre(UI_Box);
struct UI_Box {
    UI_Flag flags;
    UI_Size input_size;
    Texture *texture;
    Color color;
    Str text_content;
    FontFile *font_file;
    u16 font_size_px;
    f32 font_outline_px;
    i16 depth;

    UI_Box *parent;
    Vec_P_UI_Box childs;

    RectF bbox;
};
derive_struct_post(UI_Box);

struct UI_Style {
    UI_Size input_size;
    Str text_content;
    UI_Box *parent;
    UI_Flag flags;
    Texture *texture;
    Color color;
    u16 child_idx;
    FontFile *font_file;
    u16 font_size_px;
    f32 font_outline_px;
    i16 depth;
};
derive_struct(UI_Style);

struct UI_System {
    f32 ui_scale;
};
derive_struct(UI_System);

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

    // Per-frame buffers
    FVec_QuadRequest quad_requests;
};
derive_struct(RenderState);

struct App {
    Arena *app_arena;  // Lives for duration of application
    SDL_Window *window;

    SDL_Keycode prev_keys;
    // TODO: float-based scrolling on NDC could mess with pixel-perfect alignment
    f32 scroll;
    bool debug_draw;
    i32 zoom;

    Arena *session_arena;
    Session *session;  // Nullable

    RenderState *render_state;
    TextureSystem *texture_system;
    FontSystem font_system;
};
derive_struct(App);
