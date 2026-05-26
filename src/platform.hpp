#pragma once

#include "base.hpp"

void *os_alloc(u64 size);
void os_free(void *buf, u64 size);
OSResult cmd_run(Cmd *cmd);
Arr<char *> posix_build_args(Arena *arena, Cmd *cmd);
Arr<char *> posix_build_env(Arena *arena, Cmd *cmd);
Instant os_get_monotonic_time();
