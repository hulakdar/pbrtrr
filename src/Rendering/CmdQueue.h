#pragma once

#include "Util/Util.h"
#include "Rendering/CmdList.h"

#include "external/d3dx12.h"
#include <stdint.h>

namespace Rendering
{

class CCmdQueue
{
public:
	auto Get() { return CmdQueue.Get(); }

	void Init(ComPtr<ID3D12Device2>& InDevice, D3D12_COMMAND_LIST_TYPE InCmdListType);

	CCmdList	GetCommandList();

	[[nodiscard]]
	uint64_t	Execute(CCmdList& CmdList);

	bool		IsFenceComplete(uint64_t fenceValue);
	uint64_t	Signal();
	void		WaitForFenceValue(uint64_t fenceValue);
	void		Flush();

protected:
	ComPtr<ID3D12CommandAllocator>	CreateCommandAllocator();
	CCmdList					CreateCommandList(ComPtr<ID3D12CommandAllocator> Allocator);

private:
	ComPtr<ID3D12CommandQueue>	CmdQueue;
	ComPtr<ID3D12Device2>      Device;
	ComPtr<ID3D12Fence>        Fence;

	HANDLE                  FenceEvent;
	D3D12_COMMAND_LIST_TYPE CmdListType;
	uint64_t                CurrentFenceValue = 0;

	struct CmdAllocatorEntry
	{
		uint64_t fenceValue;
		ComPtr<ID3D12CommandAllocator> CmdAllocator;
	};

	using CmdAllocatorQueue = Queue<CmdAllocatorEntry>;
	using CmdListQueue = Queue<CCmdList>;

	CmdAllocatorQueue   AllocatorQueue;
	CmdListQueue        ListQueue;
};

}
