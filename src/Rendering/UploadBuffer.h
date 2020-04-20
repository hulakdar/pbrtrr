#pragma once

#include "Util/Util.h"
#include "external/d3dx12.h"

namespace Rendering
{

class CUploadBuffer
{
public:
	// Use to upload data to the GPU
	struct Allocation
	{
		void* CPU;
		D3D12_GPU_VIRTUAL_ADDRESS GPU;
	};

	/**
	 * @param pageSize The size to use to allocate new pages in GPU memory.
	 */
	CUploadBuffer(size_t sizeInBytes = 2_mb) : PageSize(sizeInBytes) {}

	size_t PageSize;

	/**
	 * Allocate memory in an Upload heap.
	 * An allocation must not exceed the size of a page.
	 * Use a memcpy or similar method to copy the 
	 * buffer data to CPU pointer in the Allocation structure returned from 
	 * this function.
	 */
	Allocation Allocate(size_t sizeInBytes, size_t alignment);


	/**
	 * Release all allocated pages. This should only be done when the command list
	 * is finished executing on the CommandQueue.
	 */
	void Reset();

private:
    // A single page for the allocator.
	struct SPage
	{
		SPage(size_t sizeInBytes) : PageSize(sizeInBytes) {}

		// Check to see if the page has room to satisfy the requested
		// allocation.
		bool HasSpace(size_t sizeInBytes, size_t alignment) const;

		// Allocate memory from the page.
		Allocation Allocate(size_t sizeInBytes, size_t alignment);

		// Reset the page for reuse.
		void Reset();

		private:
 
		ComPtr<ID3D12Resource> ResourceHandle;
	 
		// Base pointer.
		void* CPUPtr;
		D3D12_GPU_VIRTUAL_ADDRESS GPUPtr;
	 
		// Allocated page size.
		size_t PageSize;
		// Current allocation offset in bytes.
		size_t Offset;
	};

	// A pool of memory pages.
	using PagePool = TDeque< TUniquePtr<SPage> >;
};

}
