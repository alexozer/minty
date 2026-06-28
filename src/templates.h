#pragma once

#include "bootstrap.h"

typedef struct TYPE {
} TYPE;

// BEGIN_TEMPLATE_STRUCT Arr
typedef struct Arr_TYPE {
    TYPE *ptr;
    u64 count;
} Arr_TYPE;
// END_TEMPLATE_STRUCT

// BEGIN_TEMPLATE_STRUCT Vec
typedef struct Vec_TYPE {
    TYPE *ptr;
    u64 count;
    u64 capacity;
} Vec_TYPE;
// END_TEMPLATE_STRUCT

// BEGIN_TEMPLATE_STRUCT Opt
typedef struct Opt_TYPE {
    TYPE opt;
    bool present;
} Opt_TYPE;
// END_TEMPLATE_STRUCT
