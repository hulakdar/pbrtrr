#pragma once
#include "Containers/BitVector.h"
#include "Containers/Array.h"
#include "Containers/UniquePtr.h"

#include <d3d12.h>
#include <cstdint>
#include <Threading\Mutex.h>
#include <Util\Util.h>

namespace Rendering
{

class CDescriptorAllocatorPage;

class CDescriptorAllocation
{
public:
	CDescriptorAllocation(
		D3D12_CPU_DESCRIPTOR_HANDLE descriptor,
		uint32_t numHandles,
		uint32_t descriptorSize,
		CDescriptorAllocatorPage* page
	)
		: Descriptor(descriptor)
		, NumHandles(numHandles)
		, DescriptorSize(descriptorSize)
		, Page(page)
	{}

	// The number of descriptors in this allocation.
	uint32_t NumHandles = 0;
	// A pointer back to the original page where this allocation came from.
	CDescriptorAllocatorPage* Page = nullptr;

	// Check if this a valid descriptor.
	bool IsNull() const { return Descriptor.ptr; }

	// Get a descriptor at a particular offset in the allocation.
	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandle(uint32_t offset = 0) const
	{
		CHECK( offset < NumHandles, "" );
		return { Descriptor.ptr + ( DescriptorSize * (size_t)offset ) };
	}

	CDescriptorAllocation() {}

	// Copies are not allowed.
	CDescriptorAllocation(const CDescriptorAllocation&) = delete;
	CDescriptorAllocation& operator=(const CDescriptorAllocation&) = delete;

	// Move is allowed.
	CDescriptorAllocation(CDescriptorAllocation&& other) { *this = MOVE(other); }
	CDescriptorAllocation& operator=(CDescriptorAllocation&& other);

	// The destructor will automatically free the allocation.
	~CDescriptorAllocation() { Free(); }

private:
	// Free the descriptor back to the heap it came from.
	void Free();

	// The base descriptor.
	D3D12_CPU_DESCRIPTOR_HANDLE Descriptor { 0 };
	// The offset to the next descriptor.
	uint32_t DescriptorSize = 0;
};

class CDescriptorAllocator
{
public:
    CDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptorsPerHeap = 256)
		: HeapType(type)
		, NumDescriptorsPerHeap(numDescriptorsPerHeap) { }

    virtual ~CDescriptorAllocator();
 
    /**
     * Allocate a number of contiguous descriptors from a CPU visible descriptor heap.
     * 
     * @param numDescriptors The number of contiguous descriptors to allocate. 
     * Cannot be more than the number of descriptors per descriptor heap.
     */
    CDescriptorAllocation Allocate(uint32_t numDescriptors = 1);
 
    /**
     * When the frame has completed, the stale descriptors can be released.
     */
    void ReleaseStaleDescriptors( uint64_t frameNumber );

private:
	using DescriptorHeapPool = TArray< TUniquePtr<CDescriptorAllocatorPage> >;
 
    // Create a new heap with a specific number of descriptors.
    CDescriptorAllocatorPage* CreateAllocatorPage();
 
    D3D12_DESCRIPTOR_HEAP_TYPE HeapType;
    uint32_t NumDescriptorsPerHeap;
 
    DescriptorHeapPool  HeapPool;
    BitVector           AvailableHeaps;
 
    Mutex AllocationMutex; // really needed?
};

}
