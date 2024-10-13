#include <d3d12.h>

#include "Render/RenderDX12.h"
#include "Render/RenderThread.h"
#include "Containers/Queue.h"
#include "Containers/ComPtr.h"
#include "Containers/Map.h"
#include "Threading/Mutex.h"
#include "Util/Util.h"

static TQueue<TComPtr<ID3D12CommandAllocator>> gInFlightAllocators[4];
static TracyLockable(Mutex, gFreeAllocatorsLock);

TComPtr<ID3D12CommandAllocator> GetCommandAllocator(D3D12_COMMAND_LIST_TYPE Type)
{
	TComPtr<ID3D12CommandAllocator> Result;
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

void DiscardCommandAllocator(TComPtr<ID3D12CommandAllocator>& Allocator, D3D12_COMMAND_LIST_TYPE Type)
{
	ScopedLock AutoLock(gFreeAllocatorsLock);
	EnqueueDelayedWork([Type, Allocator = MOVE(Allocator)]() mutable {
		gInFlightAllocators[Type].push(MOVE(Allocator));
	}, CurrentFrameTicket());
}
