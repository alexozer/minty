#include "base.hpp"
#include "platform.hpp"

#include <process.h>

void *os_alloc(u64 size) {
    log_fatal("Unimplemented");
}

void os_free(void *buf, u64 size) {
    log_fatal("Unimplemented");
}

OSResult cmd_run(Cmd *cmd) {
    log_fatal("Unimplemented");
}

Instant os_get_monotonic_time() {
    log_fatal("Unimplemented");
}
