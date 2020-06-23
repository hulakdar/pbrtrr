#pragma once

#include "Util/Util.h"
#include "containers/UniquePtr.h"
#include "containers/ComPtr.h"
#include "containers/Array.h"

#include "external/d3dx12.h"

namespace Rendering
{

struct SAllocation
{
	void* CPU;
	D3D12_GPU_VIRTUAL_ADDRESS GPU;
};

struct SPage
{
	SPage(size_t sizeInBytes);

	~SPage() { ResourceHandle->Unmap(0, nullptr); }

	// Allocated page size.
	size_t PageSize;

	// Check to see if the page has room to satisfy the requested
	// allocation.
	bool HasSpace(size_t sizeInBytes, size_t alignment) const;

	// Allocate memory from the page.
	SAllocation Allocate(size_t sizeInBytes, size_t alignment);

	// Reset the page for reuse.
	void Reset() { Offset = 0; }

private:
	ComPtr<ID3D12Resource> ResourceHandle;
 
	// Base pointer.
	void* CPUPtr = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS GPUPtr { 0 };
 
	// Current allocation offset in bytes.
	size_t Offset = 0;
};

class CUploadBuffer
{
public:

	CUploadBuffer(size_t sizeInBytes = 2_mb) : PageSize(sizeInBytes) {}

	size_t PageSize;

	/**
	 * Allocate memory in an Upload heap.
	 * An allocation must not exceed the size of a page.
	 * Use a memcpy or similar method to copy the 
	 * buffer data to CPU pointer in the Allocation structure returned from 
	 * this function.
	 */
	SAllocation Allocate(size_t sizeInBytes, size_t alignment);

	/**
	 * Release all allocated pages. This should only be done when the command list
	 * is finished executing on the CommandQueue.
	 */
	void Reset();

private:

	// Request a page from the pool of available pages
	// or create a new page if there are no available pages.
	TUniquePtr<SPage> RequestPage();

	TUniquePtr<SPage> CurrentPage;

	TArray< TUniquePtr<SPage> > PagePool;
	TArray< TUniquePtr<SPage> > AvailablePages;
};

}
