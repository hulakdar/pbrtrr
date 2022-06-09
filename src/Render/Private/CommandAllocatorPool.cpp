#include "Render/CommandAllocatorPool.h"
#include "Render/Context.h"
#include "Containers/Queue.h"
#include "Containers/ComPtr.h"
#include "Containers/Map.h"
#include "Threading/Mutex.h"
#include "Util/Util.h"

#include <d3d12.h>

struct WorkingAllocator
{
	uint64_t SafeToUseFrameId;
	ComPtr<ID3D12CommandAllocator> Allocator;
};

TMap<ID3D12CommandAllocator*, D3D12_COMMAND_LIST_TYPE> gAllocatorTypes;
TQueue<WorkingAllocator> gInFlightAllocators[4];
TracyLockable(Mutex, gFreeAllocatorsLock);

ComPtr<ID3D12CommandAllocator> GetCommandAllocator(D3D12_COMMAND_LIST_TYPE Type, uint64_t CurrentFrameID)
{
	ComPtr<ID3D12CommandAllocator> Result = nullptr;
	{
		ScopedLock AutoLock(gFreeAllocatorsLock);
		if (!gInFlightAllocators[Type].empty() && gInFlightAllocators[Type].front().SafeToUseFrameId <= CurrentFrameID)
		{
			Result = MOVE(gInFlightAllocators[Type].front().Allocator);
			gInFlightAllocators[Type].pop();
		}
		else
		{
			Result = CreateCommandAllocator(Type);
			gAllocatorTypes[Result.Get()] = Type;
		}
	}
	CHECK(Result.Get(), "!?");
	return Result;
}

void DiscardCommandAllocator(ComPtr<ID3D12CommandAllocator>&& Allocator, D3D12_COMMAND_LIST_TYPE Type, uint64_t CurrentFrameID)
{
	ScopedLock AutoLock(gFreeAllocatorsLock);
	gInFlightAllocators[Type].push(WorkingAllocator{CurrentFrameID + 3, MOVE(Allocator)});
}
