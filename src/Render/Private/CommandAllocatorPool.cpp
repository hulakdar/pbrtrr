#include "Render/CommandAllocatorPool.h"
#include "Render/Context.h"
#include "Render/RenderThread.h"
#include "Containers/Queue.h"
#include "Containers/ComPtr.h"
#include "Containers/Map.h"
#include "Threading/Mutex.h"
#include "Util/Util.h"

#include <d3d12.h>

TQueue<ComPtr<ID3D12CommandAllocator>> gInFlightAllocators[4];
TracyLockable(Mutex, gFreeAllocatorsLock);
//Mutex gFreeAllocatorsLock;

ComPtr<ID3D12CommandAllocator> GetCommandAllocator(D3D12_COMMAND_LIST_TYPE Type)
{
	ComPtr<ID3D12CommandAllocator> Result;
	{
		ScopedLock AutoLock(gFreeAllocatorsLock);
		if (!gInFlightAllocators[Type].empty())
		{
			Result = MOVE(gInFlightAllocators[Type].front());
			gInFlightAllocators[Type].pop();
			return Result;
		}
	}
	Result = CreateCommandAllocator(Type);
	return Result;
}

void DiscardCommandAllocator(ComPtr<ID3D12CommandAllocator>& Allocator, D3D12_COMMAND_LIST_TYPE Type)
{
	ScopedLock AutoLock(gFreeAllocatorsLock);
	EnqueueDelayedWork([Type, Allocator = MOVE(Allocator)]() mutable {
		gInFlightAllocators[Type].push(MOVE(Allocator));
	}, CurrentFrameTicket());
}
