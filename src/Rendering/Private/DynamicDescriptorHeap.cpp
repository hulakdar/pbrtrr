#include "Rendering/Device.h"
#include "Rendering/DynamicDescriptorHeap.h"

namespace Rendering
{
	static inline UINT GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE heapType) {
		return CDevice::Instance.GetDescriptorSize(heapType);
	}

	bool CDynamicDescriptorHeap::StageDescriptors(
		uint32_t rootParameterIndex,
		uint32_t offset,
		uint32_t numDescriptors,
		const D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptor)
	{
		CHECK_RETURN(numDescriptors > NumDescriptorsPerHeap,
			"Cannot stage more than the maximum number of descriptors per heap.", false);
		CHECK_RETURN(rootParameterIndex >= MaxDescriptorTables,
			"Cannot stage more than MaxDescriptorTables root parameters.", false);

		SDescriptorTableCacheEntry& descriptorTableCache = DescriptorTableCache[rootParameterIndex];
		// Check that the number of descriptors to copy does not exceed the number
		// of descriptors expected in the descriptor table.
		CHECK_RETURN( (offset + numDescriptors) > descriptorTableCache.NumDescriptors,
			"Number of descriptors exceeds the number of descriptors in the descriptor table.", false);

		D3D12_CPU_DESCRIPTOR_HANDLE* dstDescriptor = (descriptorTableCache.BaseDescriptor + offset);
		for (uint32_t i = 0; i < numDescriptors; ++i)
		{
			dstDescriptor[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(srcDescriptor, i, GetDescriptorSize(DescriptorHeapType));
		}

		StaleDescriptorTableBitMask[rootParameterIndex] = true;
		return true;
	}

	/**
	 * Copy all of the staged descriptors to the GPU visible descriptor heap and
	 * bind the descriptor heap and the descriptor tables to the command list.
	 * The passed-in function object is used to set the GPU visible descriptors
	 * on the command list. Two possible functions are:
	 *   * Before a draw    : ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable
	 *   * Before a dispatch: ID3D12GraphicsCommandList::SetComputeRootDescriptorTable
	 * 
	 * Since the CDynamicDescriptorHeap can't know which function will be used, it must
	 * be passed as an argument to the function.
	 */
	template<typename FuncType>
	void CDynamicDescriptorHeap::CommitStagedDescriptors(CCmdList& commandList, FuncType setFunc)
	{
		// Compute the number of descriptors that need to be copied 
		uint32_t numDescriptorsToCommit = ComputeStaleDescriptorCount();

		if (numDescriptorsToCommit > 0)
		{
			auto device = CDevice::Instance.Get();
			auto d3d12GraphicsCommandList = commandList.Get();
			CHECK(d3d12GraphicsCommandList != nullptr, "CmdList is not valid");

			if ( !CurrentDescriptorHeap || NumFreeHandles < numDescriptorsToCommit )
			{
				CurrentDescriptorHeap = RequestDescriptorHeap();
				CurrentCPUDescriptorHandle = CurrentDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
				CurrentGPUDescriptorHandle = CurrentDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
				NumFreeHandles = NumDescriptorsPerHeap;
			 
				commandList.SetDescriptorHeap(DescriptorHeapType, CurrentDescriptorHeap.Get());
			 
				// When updating the descriptor heap on the command list, all descriptor
				// tables must be (re)recopied to the new descriptor heap (not just
				// the stale descriptor tables).
				StaleDescriptorTableBitMask = DescriptorTableBitMask;
			}

			for ( UINT rootIndex = (UINT)StaleDescriptorTableBitMask.find_first(); rootIndex < MaxDescriptorTables; rootIndex = (UINT)StaleDescriptorTableBitMask.find_next(rootIndex))
			{
				UINT numSrcDescriptors = DescriptorTableCache[rootIndex].NumDescriptors;
				D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorHandles = DescriptorTableCache[rootIndex].BaseDescriptor;
				D3D12_CPU_DESCRIPTOR_HANDLE pDestDescriptorRangeStarts[] =
				{
					CurrentCPUDescriptorHandle
				};
				UINT pDestDescriptorRangeSizes[] =
				{
					numSrcDescriptors
				};
				// Copy the staged CPU visible descriptors to the GPU visible descriptor heap.
				device->CopyDescriptors(1, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
					numSrcDescriptors, pSrcDescriptorHandles, nullptr, DescriptorHeapType);
				// Set the descriptors on the command list using the passed-in setter function.
				(d3d12GraphicsCommandList->*setFunc)(rootIndex, CurrentGPUDescriptorHandle);

				// Offset current CPU and GPU descriptor handles.
				CurrentCPUDescriptorHandle.Offset(numSrcDescriptors, GetDescriptorSize(DescriptorHeapType));
				CurrentGPUDescriptorHandle.Offset(numSrcDescriptors, GetDescriptorSize(DescriptorHeapType));
				NumFreeHandles -= numSrcDescriptors;
				StaleDescriptorTableBitMask.flip(rootIndex);
			}
		}
	}

	void CDynamicDescriptorHeap::CommitStagedDescriptorsForDraw(CCmdList& commandList)
	{
		CommitStagedDescriptors(commandList, &ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable);
	}

	void CDynamicDescriptorHeap::CommitStagedDescriptorsForDispatch(CCmdList& commandList)
	{
		CommitStagedDescriptors(commandList, &ID3D12GraphicsCommandList::SetComputeRootDescriptorTable);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE CDynamicDescriptorHeap::CopyDescriptor(CCmdList& comandList, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor)
	{
		if (!CurrentDescriptorHeap || NumFreeHandles < 1)
		{
			CurrentDescriptorHeap = RequestDescriptorHeap();
			CurrentCPUDescriptorHandle = CurrentDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			CurrentGPUDescriptorHandle = CurrentDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
			NumFreeHandles = NumDescriptorsPerHeap;
	 
			comandList.SetDescriptorHeap(DescriptorHeapType, CurrentDescriptorHeap.Get());
	 
			// When updating the descriptor heap on the command list, all descriptor
			// tables must be (re)recopied to the new descriptor heap (not just
			// the stale descriptor tables).
			StaleDescriptorTableBitMask = DescriptorTableBitMask;
		}
		auto device = CDevice::Instance.Get();

		D3D12_GPU_DESCRIPTOR_HANDLE hGPU = CurrentGPUDescriptorHandle;
		device->CopyDescriptorsSimple(1, CurrentCPUDescriptorHandle, cpuDescriptor, DescriptorHeapType);
	 
		CurrentCPUDescriptorHandle.Offset(1, GetDescriptorSize(DescriptorHeapType));
		CurrentGPUDescriptorHandle.Offset(1, GetDescriptorSize(DescriptorHeapType));
		NumFreeHandles -= 1;
	 
		return hGPU;
	}

	//void CDynamicDescriptorHeap::ParseRootSignature(const CRootSignature& rootSignature)
	//{
	//	// If the root signature changes, all descriptors must be (re)bound to the
	//	// command list.
	//	StaleDescriptorTableBitMask.reset();
	// 
	//	const auto& rootSignatureDesc = rootSignature.GetRootSignatureDesc();

	//	// Get a bit mask that represents the root parameter indices that match the 
	//	// descriptor heap type for this dynamic descriptor heap.
	//	DescriptorTableBitMask = rootSignature.GetDescriptorTableBitMask(DescriptorHeapType);

	//	uint32_t currentOffset = 0;
	//	for (DWORD rootIndex = DescriptorTableBitMask.find_first(); rootIndex < MaxDescriptorTables; rootIndex = DescriptorTableBitMask.find_next(rootIndex))
	//	{
	//		uint32_t numDescriptors = rootSignature.GetNumDescriptors(rootIndex);
	//	 
	//		SDescriptorTableCacheEntry& descriptorTableCache = DescriptorTableCache[rootIndex];
	//		descriptorTableCache.NumDescriptors = numDescriptors;
	//		descriptorTableCache.BaseDescriptor = DescriptorHandleCache.get() + currentOffset;
	//	 
	//		currentOffset += numDescriptors;
	//	}

	//	// Make sure the maximum number of descriptors per descriptor heap has not been exceeded.
	//	CHECK(currentOffset <= NumDescriptorsPerHeap, "The root signature requires more than the maximum number of descriptors per descriptor heap. Consider increasing the maximum number of descriptors per descriptor heap.");
	//}

	ComPtr<ID3D12DescriptorHeap> CDynamicDescriptorHeap::RequestDescriptorHeap()
	{
		ComPtr<ID3D12DescriptorHeap> descriptorHeap;
		if (!AvailableDescriptorHeaps.empty())
		{
			descriptorHeap = AvailableDescriptorHeaps.front();
			AvailableDescriptorHeaps.pop();
		}
		else
		{
			descriptorHeap = CreateDescriptorHeap();
			DescriptorHeapPool.push(descriptorHeap);
		}
	 
		return descriptorHeap;
	}

	ComPtr<ID3D12DescriptorHeap> CDynamicDescriptorHeap::CreateDescriptorHeap()
	{
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
		descriptorHeapDesc.Type = DescriptorHeapType;
		descriptorHeapDesc.NumDescriptors = NumDescriptorsPerHeap;
		descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 
		auto device = CDevice::Instance.Get();
		ComPtr<ID3D12DescriptorHeap> descriptorHeap;
		VALIDATE(device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap)));
	 
		return descriptorHeap;
	}

	uint32_t CDynamicDescriptorHeap::ComputeStaleDescriptorCount() const
	{
		uint32_t numStaleDescriptors = 0;
	 
		for (size_t i = StaleDescriptorTableBitMask.find_first(); i < MaxDescriptorTables; i = StaleDescriptorTableBitMask.find_next(i))
		{
			numStaleDescriptors += DescriptorTableCache[i].NumDescriptors;
		}
		return numStaleDescriptors;
	}
}
