#include "platform.hpp"

#include <process.h>

#include "base.hpp"

void *os_alloc(u64 size) {
    log_fatal("Unimplemented");
}

void os_free(void *buf, u64 size) {
    log_fatal("Unimplemented");
}
