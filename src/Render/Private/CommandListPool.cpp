#include "Render/CommandListPool.generated.h"
#include "Render/RenderDX12.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Containers/ComPtr.h"
#include "Threading/Mutex.h"
#include "Util/Util.h"
#include "Util/Debug.h"

#include <d3d12.h>

static TQueue<TComPtr<ID3D12GraphicsCommandList7>> gCommandLists[4];
static TracyLockable(Mutex, gCommandListLock);

D3D12CmdList GetCommandList(D3D12_COMMAND_LIST_TYPE Type, const wchar_t *DebugName)
{
	D3D12CmdList Result;
	Result.Type = Type;
	Result.CommandAllocator = GetCommandAllocator(Type);
	VALIDATE(Result.CommandAllocator->Reset());

	Result.CommandAllocator->SetName(DebugName);
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

	return Result;
}

void DiscardCommandList(D3D12CmdList& CmdList)
{
	DiscardCommandAllocator(CmdList.CommandAllocator, CmdList.Type);

	ScopedLock AutoLock(gCommandListLock);
	gCommandLists[CmdList.Type].push(MOVE(CmdList.CommandList));
}
