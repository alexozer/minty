//
// GENERATED FILE - DO NOT MODIFY
// Generated from template 'src/templates.h', modify this instead
//

#pragma once

#include "bootstrap.h"
#include "generated.h"

typedef struct Arr_u64 {
    u64 *ptr;
    u64 count;
} Arr_u64;

typedef struct Arr_Str {
    Str *ptr;
    u64 count;
} Arr_Str;

typedef struct Vec_u64 {
    u64 *ptr;
    u64 count;
    u64 capacity;
} Vec_u64;

typedef struct Vec_Str {
    Str *ptr;
    u64 count;
    u64 capacity;
} Vec_Str;

typedef struct Opt_u64 {
    u64 opt;
    bool present;
} Opt_u64;

typedef struct Opt_Str {
    Str opt;
    bool present;
} Opt_Str;

