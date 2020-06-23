#pragma once
#include <EASTL/internal/move_help.h>
#include "Debug.h"

#ifndef RELEASE
# define CHECK(x, msg) if (!(x)) {Print(msg); DEBUG_BREAK();}
# define CHECK_RETURN(x, msg, ret) if (!(x)) {Print(msg); DEBUG_BREAK(); return ret;}
#else
# define CHECK(x, msg)
# define CHECK_RETURN(x, msg, ret)
#endif
#define MOVE(x) EASTL_MOVE_INLINE(x)
#define VALIDATE(x) if (!IfErrorThenPrint(x)) {DEBUG_BREAK();}
#define DISABLE_OPTIMIZATION __pragma(optimize("", off))

template <typename T, int count>
int ArraySize(T (&)[count]) { return count; }

bool IfErrorThenPrint(long Result);

// size custom literal
inline size_t operator "" _kb(size_t kilobytes) { return kilobytes * 1024; }
inline size_t operator "" _mb(size_t megabytes) { return megabytes * 1024 * 1024; }
inline size_t operator "" _gb(size_t gigabytes) { return gigabytes * 1024 * 1024 * 1024; }
