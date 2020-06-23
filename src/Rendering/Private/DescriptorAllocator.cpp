#include "Rendering/DescriptorAllocator.h"
#include "Rendering/Device.h"
#include "Containers/Queue.h"
#include "Containers/ComPtr.h"
#include <map>

namespace Rendering
{
	class CDescriptorAllocatorPage
	{
	public:
		CDescriptorAllocatorPage(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
		    : HeapType( type )
			, NumDescriptorsInHeap( numDescriptors )
			, NumFreeHandles( numDescriptors )
		{
			auto device = CDevice::Instance.Get();

			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
			heapDesc.Type = HeapType;
			heapDesc.NumDescriptors = NumDescriptorsInHeap;

			VALIDATE(device->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &DescriptorHeap ) ) );

			BaseDescriptor = DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			IncrementSize = device->GetDescriptorHandleIncrementSize(HeapType);

			// Initialize the free lists
			AddNewBlock( 0, NumFreeHandles );
		}

		/**
		 * Check to see if this descriptor page has a contiguous block of descriptors
		 * large enough to satisfy the request.
		 */
		bool HasSpace(uint32_t numDescriptors) const
		{
			return FreeListBySize.lower_bound(numDescriptors) != FreeListBySize.end();
		}

		/**
		 * Allocate a number of descriptors from this descriptor heap.
		 * If the allocation cannot be satisfied, then a NULL descriptor
		 * is returned.
		 */
		CDescriptorAllocation Allocate(uint32_t numDescriptors)
		{
			ScopedLock lock(AllocationMutex);

			// There are less than the requested number of descriptors left in the heap.
			// Return a NULL descriptor and try another heap.
			if ( numDescriptors > NumFreeHandles )
			{
				return CDescriptorAllocation();
			}

			// Get the first block that is large enough to satisfy the request.
			auto smallestBlockIt = FreeListBySize.lower_bound( numDescriptors );
			if ( smallestBlockIt == FreeListBySize.end() )
			{
				// There was no free block that could satisfy the request.
				return CDescriptorAllocation();
			}
			// The size of the smallest block that satisfies the request.
			auto blockSize = smallestBlockIt->first;

			// The pointer to the same entry in the FreeListByOffset map.
			auto offsetIt = smallestBlockIt->second;

			// The offset in the descriptor heap.
			auto offset = offsetIt->first;

			// Remove the existing free block from the free list.
			FreeListBySize.erase( smallestBlockIt );
			FreeListByOffset.erase( offsetIt );

			// Compute the new free block that results from splitting this block.
			auto newOffset = offset + numDescriptors;
			auto newSize = blockSize - numDescriptors;

			if ( newSize > 0 )
			{
				// If the allocation didn't exactly match the requested size,
				// return the left-over to the free list.
				AddNewBlock( newOffset, newSize );
			}

			// Decrement free handles.
			NumFreeHandles -= numDescriptors;

			return CDescriptorAllocation(
				CD3DX12_CPU_DESCRIPTOR_HANDLE(BaseDescriptor, offset, IncrementSize),
				numDescriptors, IncrementSize, this);
		}

		/**
		 * Return a descriptor back to the heap.
		 * @param frameNumber Stale descriptors are not freed directly, but put
		 * on a stale allocations queue. Stale allocations are returned to the heap
		 * using the DescriptorAllocatorPage::ReleaseStaleAllocations method.
		 */
		void Free(CDescriptorAllocation&& descriptor, uint64_t frameNumber) {
			 // Compute the offset of the descriptor within the descriptor heap.
			auto offset = ComputeOffset( descriptor.GetDescriptorHandle() );

			ScopedLock lock(AllocationMutex);

			// Don't add the block directly to the free list until the frame has completed.
			StaleDescriptors.emplace( offset, descriptor.NumHandles, frameNumber );
		}

		/**
		 * Returned the stale descriptors back to the descriptor heap.
		 */
		void ReleaseStaleDescriptors( uint64_t frameNumber )
		{
			ScopedLock lock( AllocationMutex );

			while ( !StaleDescriptors.empty() && StaleDescriptors.front().FrameNumber <= frameNumber )
			{
				auto& staleDescriptor = StaleDescriptors.front();

				// The offset of the descriptor in the heap.
				auto offset = staleDescriptor.Offset;
				// The number of descriptors that were allocated.
				auto numDescriptors = staleDescriptor.Size;

				FreeBlock( offset, numDescriptors );

				StaleDescriptors.pop();
			}
		}

		uint32_t NumFreeHandles;
		D3D12_DESCRIPTOR_HEAP_TYPE HeapType;
	protected:
		// Compute the offset of the descriptor handle from the start of the heap.
		uint32_t ComputeOffset(D3D12_CPU_DESCRIPTOR_HANDLE handle)
		{
			return static_cast<uint32_t>( handle.ptr - BaseDescriptor.ptr ) / IncrementSize;
		}

		// Adds a new block to the free list.
		void AddNewBlock( uint32_t offset, uint32_t numDescriptors )
		{
			auto offsetIt = FreeListByOffset.emplace( offset, numDescriptors );
			auto sizeIt = FreeListBySize.emplace( numDescriptors, offsetIt.first );
			offsetIt.first->second.FreeListBySizeIt = sizeIt->second;
		}

		// Free a block of descriptors.
		// This will also merge free blocks in the free list to form larger blocks
		// that can be reused.
		void FreeBlock(uint32_t offset, uint32_t numDescriptors)
		{
			// Find the first element whose offset is greater than the specified offset.
			// This is the block that should appear after the block that is being freed.
			auto nextBlockIt = FreeListByOffset.upper_bound(offset);

			// Find the block that appears before the block being freed.
			auto prevBlockIt = nextBlockIt;
			// If it's not the first block in the list.
			if (prevBlockIt != FreeListByOffset.begin())
			{
				// Go to the previous block in the list.
				--prevBlockIt;
			}
			else
			{
				// Otherwise, just set it to the end of the list to indicate that no
				// block comes before the one being freed.
				prevBlockIt = FreeListByOffset.end();
			}

			// Add the number of free handles back to the heap.
			// This needs to be done before merging any blocks since merging
			// blocks modifies the numDescriptors variable.
			NumFreeHandles += numDescriptors;

			if ( prevBlockIt != FreeListByOffset.end() &&
				 offset == prevBlockIt->first + prevBlockIt->second.Size )
			{
				// The previous block is exactly behind the block that is to be freed.
				//
				// PrevBlock.Offset           Offset
				// |                          |
				// |<-----PrevBlock.Size----->|<------Size-------->|
				//

				// Increase the block size by the size of merging with the previous block.
				offset = prevBlockIt->first;
				numDescriptors += prevBlockIt->second.Size;

				// Remove the previous block from the free list.
				FreeListBySize.erase( prevBlockIt->second.FreeListBySizeIt );
				FreeListByOffset.erase( prevBlockIt );
			}

			if ( nextBlockIt != FreeListByOffset.end() &&
				 offset + numDescriptors == nextBlockIt->first )
			{
				// The next block is exactly in front of the block that is to be freed.
				//
				// Offset               NextBlock.Offset 
				// |                    |
				// |<------Size-------->|<-----NextBlock.Size----->|
			 
				// Increase the block size by the size of merging with the next block.
				numDescriptors += nextBlockIt->second.Size;
			 
				// Remove the next block from the free list.
				FreeListBySize.erase( nextBlockIt->second.FreeListBySizeIt );
				FreeListByOffset.erase( nextBlockIt );
			}
			// Add the freed block to the free list.
			AddNewBlock( offset, numDescriptors );
		}

		struct SFreeBlockInfo
		{
			SFreeBlockInfo( uint32_t size ) : Size( size ) {} 
			uint32_t Size;
			std::map<uint32_t, SFreeBlockInfo>::iterator FreeListBySizeIt;
		};

		struct SStaleDescriptorInfo
		{
			SStaleDescriptorInfo( uint32_t offset, uint32_t size, uint64_t frame )
				: Offset( offset )
				, Size( size )
				, FrameNumber( frame )
			{}
		 
			// The offset within the descriptor heap.
			uint32_t Offset;
			// The number of descriptors
			uint32_t Size;
			// The frame number that the descriptor was freed.
			uint64_t FrameNumber;
		};

		// A map that lists the free blocks by the offset within the descriptor heap.
		using CFreeListByOffset = std::map<uint32_t, SFreeBlockInfo>;
		 
		// A map that lists the free blocks by size.
		// Needs to be a multimap since multiple blocks can have the same size.
		using CFreeListBySize = std::multimap<uint32_t, CFreeListByOffset::iterator>;
		 
		// A map that lists the free blocks by the offset within the descriptor heap.
		CFreeListByOffset FreeListByOffset;

		// A map that lists the free blocks by size.
		// Needs to be a multimap since multiple blocks can have the same size.
		CFreeListBySize FreeListBySize;

		// Stale descriptors are queued for release until the frame that they were freed
		// has completed.
		TQueue<SStaleDescriptorInfo> StaleDescriptors;

		ComPtr<ID3D12DescriptorHeap> DescriptorHeap;
		D3D12_CPU_DESCRIPTOR_HANDLE BaseDescriptor;
		uint32_t IncrementSize;
		uint32_t NumDescriptorsInHeap;
	 
		Mutex AllocationMutex;
	};

	CDescriptorAllocation& CDescriptorAllocation::operator=(CDescriptorAllocation&& other)
	{
		// Free this descriptor if it points to anything.
		Free();
	 
		Descriptor = other.Descriptor;
		NumHandles = other.NumHandles;
		DescriptorSize = other.DescriptorSize;
		Page = other.Page;
	 
		other.Descriptor.ptr = 0;
		other.NumHandles = 0;
		other.DescriptorSize = 0;
		other.Page = 0;
	 
		return *this;
	}

	void CDescriptorAllocation::Free()
	{
		if ( !IsNull() && Page )
		{
			Page->Free(MOVE( *this ), CDevice::Instance.FrameCount + 1);
			 
			Descriptor.ptr = 0;
			NumHandles = 0;
			DescriptorSize = 0;
			Page = nullptr;
		}
	}

	CDescriptorAllocation CDescriptorAllocator::Allocate(uint32_t numDescriptors)
	{
		ScopedLock lock( AllocationMutex );
 
		CDescriptorAllocation allocation;

		for (int i = 0; i < AvailableHeaps.size(); i++)
		{
			if (AvailableHeaps[i])
			{
				auto& allocatorPage = HeapPool[i];

				allocation = allocatorPage->Allocate(numDescriptors);

				if (allocatorPage->NumFreeHandles == 0)
				{
					AvailableHeaps[i] = false;
				}

				// A valid allocation has been found.
				if (!allocation.IsNull())
				{
					break;
				}
			}
		}

		// No available heap could satisfy the requested number of descriptors.
		if ( allocation.IsNull() )
		{
			NumDescriptorsPerHeap = Math::Max(NumDescriptorsPerHeap, numDescriptors);
			auto newPage = CreateAllocatorPage();

			allocation = newPage->Allocate( numDescriptors );
		}

		return allocation;
	}

	void CDescriptorAllocator::ReleaseStaleDescriptors(uint64_t frameNumber)
	{
		ScopedLock lock( AllocationMutex );

		for ( size_t i = 0; i < HeapPool.size(); ++i )
		{
			auto& page = HeapPool[i];
	 
			page->ReleaseStaleDescriptors( frameNumber );
	 
			if ( page->NumFreeHandles > 0 )
			{
				AvailableHeaps[i] = true;
			}
		}
	}

	CDescriptorAllocatorPage* CDescriptorAllocator::CreateAllocatorPage()
	{
		auto newPage = HeapPool.emplace_back(new CDescriptorAllocatorPage(HeapType, NumDescriptorsPerHeap)).get();

		AvailableHeaps.resize(HeapPool.size());
		*(AvailableHeaps.end() - 1) = true;

		return newPage;
	}
}