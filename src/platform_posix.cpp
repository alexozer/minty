#include "platform.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include "base.hpp"

void* os_alloc(u64 size) {
    void* buf = mmap(nullptr, (size_t)size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (buf == nullptr) {
        log_fatal("mmap failed");
    }
    return buf;
}

void os_free(void* buf, u64 size) {
    munmap(buf, (size_t)size);
}
