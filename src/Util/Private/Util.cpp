#include <Tracy.hpp>
#include <stdint.h>

#include "Threading/Mutex.h"
#include "Containers/Map.h"
#include "Util/Util.h"
#include "Util/Debug.h"
#include "Util/Math.h"
#include "Common.h"

#ifdef DEBUG

#define PageSize 4096U
#define SentinelExpectedValue 0xdeadbeefdeadbeef

struct AllocationData
{
	void *FullAllocationPointer;
	u64	FullSize;
	u64	Size;
	u64	Sentinel;
};

void* StompMalloc(u64 Size, u32 Alignment)
{
	if(Size == 0U)
		return nullptr;

	const u64 AlignedSize = (Alignment > 0U) ? ((Size + Alignment - 1U) & -static_cast<i32>(Alignment)) : Size;
	const u64 AllocFullPageSize = AlignedSize + sizeof(AllocationData) + (PageSize - 1) & ~(PageSize - 1U);

	void* FullAllocationPointer = VirtualAlloc(
		nullptr,
		AllocFullPageSize + PageSize,
		MEM_RESERVE,
		PAGE_READWRITE
	);

	static const u64 AllocationDataSize = sizeof(AllocationData);

	void* ReturnedPointer = reinterpret_cast<void*>(reinterpret_cast<u8*>(FullAllocationPointer) + AllocFullPageSize - AlignedSize);

	AllocationData *AllocDataPtr = reinterpret_cast<AllocationData*>(reinterpret_cast<u8*>(ReturnedPointer) - AllocationDataSize);

	VirtualAlloc(AllocDataPtr, AllocFullPageSize, MEM_COMMIT, PAGE_READWRITE);
	new (AllocDataPtr) AllocationData{ FullAllocationPointer, AllocFullPageSize + PageSize, AlignedSize, SentinelExpectedValue };

	// Page protect the last page, this will cause the exception in case the is an overrun.
	VirtualProtect((u8*)FullAllocationPointer + AllocFullPageSize, PageSize, PAGE_NOACCESS, nullptr);

	return ReturnedPointer;
}

void StompFree(void* InPtr)
{
	if(InPtr == nullptr)
		return;

	AllocationData *AllocDataPtr = reinterpret_cast<AllocationData*>(InPtr) - 1;

	// Check that our sentinel is intact.
	CHECK(AllocDataPtr->Sentinel == SentinelExpectedValue, "There was a memory underrun related to this allocation");

	VirtualFree(AllocDataPtr->FullAllocationPointer, AllocDataPtr->FullSize, MEM_DECOMMIT);
}

void* operator new[](std::size_t size, std::align_val_t align)
{
	void* ptr = StompMalloc(size, (u32)align);
	TracyAlloc(ptr, size);
	return ptr;
}

void* operator new[](size_t size, const char*, int, unsigned, const char*, int)
{
	void* ptr = StompMalloc(size, 0);
	TracyAlloc(ptr, size);
	return ptr;
}

void* operator new[](size_t size, size_t alignment, size_t, const char*, int, unsigned, const char*, int) 
{
	void* ptr = StompMalloc(size, alignment);
	TracyAlloc(ptr, size);
	return ptr;
}  

void* operator new(std::size_t size, std::align_val_t align)
{
	void* ptr = StompMalloc(size, (u32)align);
	TracyAlloc(ptr, size);
	return ptr;
}

void* operator new(size_t count)
{
	void* ptr = StompMalloc(count, 0);
	TracyAlloc(ptr, count);
	return ptr;
}
void operator delete(void* ptr) noexcept
{
	TracyFree(ptr);
	StompFree(ptr);
}

void operator delete[](void* ptr, size_t) noexcept
{
	TracyFree(ptr);
	StompFree(ptr);
}
#else

// for eastl
// maybe we could write our own custom allocator in future
void* operator new[](size_t size, const char*, int, unsigned, const char*, int)
{
	uint8_t *ptr = new uint8_t[size];
	return ptr;
}

void* operator new[](size_t size, size_t, size_t, const char*, int, unsigned, const char*, int) 
{
	uint8_t *ptr = new uint8_t[size];
	return ptr;
}  

void* operator new(size_t count)
{
	auto ptr = malloc(count);
	TracyAlloc(ptr, count);
	return ptr;
}
void operator delete(void* ptr) noexcept
{
	TracyFree(ptr);
	free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept
{
	TracyFree(ptr);
	free(ptr);
}
#endif

