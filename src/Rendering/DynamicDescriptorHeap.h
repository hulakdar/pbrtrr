#pragma once
#include "External/d3dx12.h"
#include "Containers/ComPtr.h"
#include "Containers/Queue.h"

#include <EASTL/bitset.h>

namespace Rendering
{
class CCmdList;
class CRootSignature;

class CDynamicDescriptorHeap
{
public:
	CDynamicDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptorsPerHeap = 1024)
		: DescriptorHeapType(heapType)
		, NumDescriptorsPerHeap(numDescriptorsPerHeap)
		, DescriptorTableBitMask(0)
		, StaleDescriptorTableBitMask(0)
		, CurrentCPUDescriptorHandle(D3D12_DEFAULT)
		, CurrentGPUDescriptorHandle(D3D12_DEFAULT)
		, NumFreeHandles(0)
	{
		// Allocate space for staging CPU visible descriptors.
		DescriptorHandleCache = std::make_unique<D3D12_CPU_DESCRIPTOR_HANDLE[]>(NumDescriptorsPerHeap);
	}

	virtual ~CDynamicDescriptorHeap();

	/**
	 * Stages a contiguous range of CPU visible descriptors.
	 * Descriptors are not copied to the GPU visible descriptor heap until
	 * the CommitStagedDescriptors function is called.
	 */
	bool StageDescriptors(
		uint32_t rootParameterIndex,
		uint32_t offset,
		uint32_t numDescriptors,
		const D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptors
	);

	void CommitStagedDescriptorsForDraw(CCmdList& commandList);
	void CommitStagedDescriptorsForDispatch(CCmdList& commandList);

	/**
	 * Copies a single CPU visible descriptor to a GPU visible descriptor heap.
	 * This is useful for the
	 *   * ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat
	 *   * ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint
	 * methods which require both a CPU and GPU visible descriptors for a UAV 
	 * resource.
	 * 
	 * @param commandList The command list is required in case the GPU visible
	 * descriptor heap needs to be updated on the command list.
	 * @param cpuDescriptor The CPU descriptor to copy into a GPU visible 
	 * descriptor heap.
	 * 
	 * @return The GPU visible descriptor.
	 */
	D3D12_GPU_DESCRIPTOR_HANDLE CopyDescriptor( CCmdList& comandList, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor);

	/**
	 * Parse the root signature to determine which root parameters contain
	 * descriptor tables and determine the number of descriptors needed for
	 * each table.
	 */
	void ParseRootSignature( const CRootSignature& rootSignature);

	/**
	 * Reset used descriptors. This should only be done if any descriptors
	 * that are being referenced by a command list has finished executing on the 
	 * command queue.
	 */
	void Reset()
	{
		AvailableDescriptorHeaps = DescriptorHeapPool;
		CurrentDescriptorHeap.Reset();
		CurrentCPUDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(D3D12_DEFAULT);
		CurrentGPUDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(D3D12_DEFAULT);
		NumFreeHandles = 0;
		DescriptorTableBitMask = 0;
		StaleDescriptorTableBitMask = 0;
	 
		// Reset the table cache
		for (int i = 0; i < MaxDescriptorTables; ++i)
		{
			DescriptorTableCache[i].Reset();
		}
	}
private:
	template<typename FuncType>
	void CommitStagedDescriptors(CCmdList& commandList, FuncType setFunc);

    // Request a descriptor heap if one is available.
    ComPtr<ID3D12DescriptorHeap> RequestDescriptorHeap();

    // Create a new descriptor heap of no descriptor heap is available.
    ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap();

	// Compute the number of stale descriptors that need to be copied
	// to GPU visible descriptor heap.
	uint32_t ComputeStaleDescriptorCount() const;

	/**
	 * The maximum number of descriptor tables per root signature.
	 * A 32-bit mask is used to keep track of the root parameter indices that
	 * are descriptor tables.
	 */
	static const uint32_t MaxDescriptorTables = 64;

	/**
	 * A structure that represents a descriptor table entry in the root signature.
	 */
	struct SDescriptorTableCacheEntry
	{
		SDescriptorTableCacheEntry()
			: NumDescriptors(0)
			, BaseDescriptor(nullptr)
		{}
	 
		// Reset the table cache.
		void Reset()
		{
			NumDescriptors = 0;
			BaseDescriptor = nullptr;
		}
	 
		// The number of descriptors in this descriptor table.
		uint32_t NumDescriptors;
		// The pointer to the descriptor in the descriptor handle cache.
		D3D12_CPU_DESCRIPTOR_HANDLE* BaseDescriptor;
	};

	// Describes the type of descriptors that can be staged using this 
	// dynamic descriptor heap.
	// Valid values are:
	//   * D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
	//   * D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
	// This parameter also determines the type of GPU visible descriptor heap to 
	// create.
	D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType;
 
	// The number of descriptors to allocate in new GPU visible descriptor heaps.
	uint32_t NumDescriptorsPerHeap;

	// The descriptor handle cache.
	std::unique_ptr<D3D12_CPU_DESCRIPTOR_HANDLE[]> DescriptorHandleCache;

	// Descriptor handle cache per descriptor table.
	SDescriptorTableCacheEntry DescriptorTableCache[MaxDescriptorTables];

	// Each bit in the bit mask represents the index in the root signature
	// that contains a descriptor table.
	eastl::bitset<MaxDescriptorTables> DescriptorTableBitMask;

	// Each bit set in the bit mask represents a descriptor table
	// in the root signature that has changed since the last time the 
	// descriptors were copied.
	eastl::bitset<MaxDescriptorTables> StaleDescriptorTableBitMask;

	TQueue< ComPtr<ID3D12DescriptorHeap> > DescriptorHeapPool;
	TQueue< ComPtr<ID3D12DescriptorHeap> > AvailableDescriptorHeaps;

	ComPtr<ID3D12DescriptorHeap>	CurrentDescriptorHeap;
    CD3DX12_GPU_DESCRIPTOR_HANDLE	CurrentGPUDescriptorHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE	CurrentCPUDescriptorHandle;
 
    uint32_t NumFreeHandles;
};

}
