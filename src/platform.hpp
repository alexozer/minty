#pragma once

#include "base.hpp"

void *os_alloc(u64 size);
void os_free(void *buf, u64 size);
