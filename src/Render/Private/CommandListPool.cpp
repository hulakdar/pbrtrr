#include "Render/CommandListPool.h"
#include "Render/CommandAllocatorPool.h"
#include "Render/Context.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Containers/ComPtr.h"
#include "Threading/Mutex.h"
#include "Util/Util.h"

#include <d3d12.h>

TQueue<ComPtr<ID3D12GraphicsCommandList>> gCommandLists[4];
TracyLockable(Mutex, gCommandListLock);

D3D12CmdList GetCommandList(D3D12_COMMAND_LIST_TYPE Type, uint64_t CurrentFrameID)
{
	D3D12CmdList Result;
	Result.Type = Type;
	Result.CommandAllocator = GetCommandAllocator(Type, CurrentFrameID);
	VALIDATE(Result.CommandAllocator->Reset());
    {
		ScopedLock AutoLock(gCommandListLock);
		if (!gCommandLists[Type].empty())
		{
			Result.CommandList = MOVE(gCommandLists[Type].front());
			gCommandLists[Type].pop();
		}
    }
	if (!Result.CommandList)
	{
		Result.CommandList = CreateCommandList(Result.CommandAllocator.Get(), Type);
	}
	VALIDATE(Result.CommandList->Reset(Result.CommandAllocator.Get(), nullptr));
	return Result;
}

void DiscardCommandList(D3D12CmdList& CmdList, uint64_t CurrentFrameID)
{
	DiscardCommandAllocator(CmdList.CommandAllocator, CmdList.Type, CurrentFrameID);

	ScopedLock AutoLock(gCommandListLock);
	gCommandLists[CmdList.Type].push(MOVE(CmdList.CommandList));
}
