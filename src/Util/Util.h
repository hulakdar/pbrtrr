#pragma once
#include <EASTL/internal/move_help.h>
#include "Containers/String.h"

#define MOVE(x) EASTL_MOVE_INLINE(x)

template <typename T>
T ReadAndAdvance(uint8_t*& DataPtr)
{
	T Result = *(T*)DataPtr;
	DataPtr += sizeof(T);
	return Result;
}

template <typename T>
void WriteAndAdvance(uint8_t*& DataPtr, const T& Data)
{
	T* Result = (T*)DataPtr;
	*Result = Data;
	DataPtr += sizeof(T);
}

template <typename T, int Count>
int ArrayCount(T (&)[Count]) { return Count; }

template <typename T, typename C>
T& RandomElement(C& Container) { return Container[rand() % Container.size()]; }

StringView LoadWholeFile(StringView Path);

// size custom literal
constexpr size_t operator "" _kb(size_t kilobytes) { return kilobytes * 1024; }
constexpr size_t operator "" _mb(size_t megabytes) { return megabytes * 1024_kb; }
constexpr size_t operator "" _gb(size_t gigabytes) { return gigabytes * 1024_mb; }
