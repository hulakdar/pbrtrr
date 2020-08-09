#pragma once
#include "Util/Debug.h"
#include <EASTL/allocator.h>

template <typename T, unsigned int Size>
class StackAllocator
{
public:
	void* allocate(size_t n, int flags = 0)
	{
		uint64_t Start = Occupied;
		Occupied += n;
		CHECK(Occupied <= Size, "Out of memory");
		return Memory[n + Start];
	}
	void* allocate(size_t n, size_t alignment, size_t offset, int flags = 0)
	{
		return allocate(n, flags);
	}
	void  deallocate(void* p, size_t n)
	{
		CHECK(p >= Memory && p < Memory + Occupied, "This memory does not belong to this allocator");
		Occupied -= n;
	}

	const char* get_name() const;
	void        set_name(const char* pName);

private:
	unsigned char Memory[Size * sizeof(T)];
	uint64_t Occupied = 0;
};

