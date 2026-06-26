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

typedef enum TimerMode TimerMode;
typedef enum TimerAction TimerAction;
typedef enum ShaderType ShaderType;

typedef struct {
    u8 *ptr;
    u64 count;
} Arr_u8;

typedef struct {
    bool present;
    Duration opt;
} Opt_Duration;

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
