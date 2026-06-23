#include "platform.hpp"

#include <process.h>

#include "base.hpp"

void* os_alloc(u64 size) {
    return calloc(size, 1);
}

void os_free(void* buf, u64 size) {
    free(buf);
}
