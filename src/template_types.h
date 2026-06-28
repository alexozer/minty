#pragma once

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef float f32;
typedef double f64;

// TODO autogenerate

typedef struct PxSize PxSize;
typedef struct PxRect PxRect;
typedef struct SegmentDef SegmentDef;
typedef struct SplitRecord SplitRecord;
typedef struct CPUTexture CPUTexture;
typedef struct FileDef FileDef;
typedef struct Timer Timer;
typedef struct Atlas Atlas;
typedef struct GlyphMetrics GlyphMetrics;
typedef struct Vertex Vertex;
typedef struct Color Color;
typedef struct Box Box;
typedef struct Session Session;
typedef struct GlyphAtlas GlyphAtlas;
typedef struct SegSummary SegSummary;
typedef struct App App;
typedef struct ErrorContext ErrorContext;
typedef struct stbrp_rect stbrp_rect;
typedef struct stbrp_node stbrp_node;
typedef struct Mesh Mesh;
typedef struct ShapedGlyph ShapedGlyph;
typedef struct PxPos PxPos;

typedef enum TimerMode TimerMode;
typedef enum TimerAction TimerAction;
typedef enum ShaderType ShaderType;
typedef enum BlendType BlendType;
typedef enum FilterType FilterType;
typedef enum BoxType BoxType;

typedef struct {
    u8 *ptr;
    u64 count;
} Arr_u8;

typedef struct {
    bool present;
    Duration opt;
} Opt_Duration;

typedef struct {
    bool present;
    PxSize opt;
} Opt_PxSize;

typedef struct {
    Opt_Duration *ptr;
    u64 count;
} Arr_Opt_Duration;

typedef struct {
    SegmentDef *ptr;
    u64 count;
} Arr_SegmentDef;

typedef struct {
    Opt_Duration *ptr;
    u64 count;
    u64 cap;
} Vec_Opt_Duration;

typedef struct {
    PxRect *ptr;
    u64 count;
} Arr_PxRect;

typedef struct {
    stbrp_rect *ptr;
    u64 count;
} Arr_stbrp_rect;

typedef struct {
    stbrp_node *ptr;
    u64 count;
} Arr_stbrp_node;

typedef struct {
    GlyphMetrics *ptr;
    u64 count;
} Arr_GlyphMetrics;

typedef struct {
    Vertex *ptr;
    u64 count;
    u64 cap;
} Vec_Vertex;

typedef struct {
    u16 *ptr;
    u64 count;
    u64 cap;
} Vec_u16;

typedef struct {
    Box **ptr;
    u64 count;
    u64 cap;
} Vec_P_Box;

typedef struct {
    u8 *ptr;
    u64 count;
    u64 cap;
} Vec_u8;

typedef struct {
    SegSummary *ptr;
    u64 count;
} Arr_SegSummary;

typedef struct {
    CPUTexture *ptr;
    u64 count;
} Arr_CPUTexture;

typedef struct {
    u32 *ptr;
    u64 count;
} Arr_u32;

typedef struct {
    u16 *ptr;
    u64 count;
} Arr_u16;

typedef struct {
    ShapedGlyph *ptr;
    u64 count;
} Arr_ShapedGlyph;

typedef struct {
    Vertex *ptr;
    u64 count;
} Arr_Vertex;

typedef struct {
    ShapedGlyph *ptr;
    u64 count;
    u64 cap;
} Vec_ShapedGlyph;

typedef struct {
    Str *ptr;
    u64 count;
    u64 cap;
} Vec_Str;

typedef struct {
    SegmentDef *ptr;
    u64 count;
    u64 cap;
} Vec_SegmentDef;
