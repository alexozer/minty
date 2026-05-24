#include "base.hpp"

#include "platform.hpp"

void *os_alloc(u64 size) {
    return nullptr;
}

void os_free(void *buf, u64 size) {
}

OSResult cmd_run(Cmd *cmd) {
    return OSResult::OtherError;
}

void os_write_stderr(Arr<u8> buf) {
    // TODO
}
