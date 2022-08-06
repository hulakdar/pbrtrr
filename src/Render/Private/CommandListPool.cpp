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

std::atomic<u64> gListsInFlight = 0;


TracyLockable(Mutex, gDebugMapLock);
TMap<ID3D12GraphicsCommandList*, const wchar_t*> gDebugMap;

D3D12CmdList GetCommandList(D3D12_COMMAND_LIST_TYPE Type, uint64_t CurrentFrameID, const wchar_t *DebugName)
{
	D3D12CmdList Result;
	Result.Type = Type;
	Result.CommandAllocator = GetCommandAllocator(Type, CurrentFrameID);
	Result.CommandAllocator->SetName(DebugName);
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
	Result.CommandList->SetName(DebugName);

	gDebugMapLock.lock();
	gDebugMap[Result.CommandList.Get()] = DebugName;
	gDebugMapLock.unlock();

	gListsInFlight++;
	return Result;
}

void DiscardCommandList(D3D12CmdList& CmdList, uint64_t CurrentFrameID)
{
	gDebugMapLock.lock();
	gDebugMap.erase(CmdList.CommandList.Get());
	gDebugMapLock.unlock();

	gListsInFlight--;
	DiscardCommandAllocator(CmdList.CommandAllocator, CmdList.Type, CurrentFrameID);

	ScopedLock AutoLock(gCommandListLock);
	gCommandLists[CmdList.Type].push(MOVE(CmdList.CommandList));
}
