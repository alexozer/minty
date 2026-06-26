#pragma once

#include "base.h"

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

typedef enum TimerMode TimerMode;
typedef enum TimerAction TimerAction;
typedef enum ShaderType ShaderType;
typedef enum BlendType BlendType;
typedef enum TextureFilterType TextureFilterType;
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
    SegmentDef *ptr;
    u64 count;
} Arr_PxRect;

typedef struct {
    SegmentDef *ptr;
    u64 count;
} Arr_stbrp_rect;

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
