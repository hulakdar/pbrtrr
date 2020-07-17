#pragma once
#include <EASTL/internal/move_help.h>
#include "Debug.h"
#define MOVE(x) EASTL_MOVE_INLINE(x)

template <typename T, int count>
int ArraySize(T (&)[count]) { return count; }

// size custom literal
inline size_t operator "" _kb(size_t kilobytes) { return kilobytes * 1024; }
inline size_t operator "" _mb(size_t megabytes) { return megabytes * 1024 * 1024; }
inline size_t operator "" _gb(size_t gigabytes) { return gigabytes * 1024 * 1024 * 1024; }
