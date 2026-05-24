#define STB_SPRINTF_IMPLEMENTATION

// Seems that stb_sprintf can do unaligned memory writes, trips lldb on macos in debug mode;w
#define STB_SPRINTF_NOUNALIGNED

extern "C" {
#include "stb_sprintf.h"
}
