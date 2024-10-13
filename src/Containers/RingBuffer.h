#pragma once
#include <atomic>

#include "Common.h"

using u64_atomic = std::atomic_uint64_t;

struct RingBufferGeneric
{
	RingBufferGeneric(u64 InSize);
	~RingBufferGeneric();

	void* Aquire(u64 NumBytes);

	u8* Data;
	u64 Size;

	u64_atomic WriteOffset;
};


