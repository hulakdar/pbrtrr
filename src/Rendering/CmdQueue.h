#pragma once

#include "Util/Util.h"

#include "external/d3dx12.h"
#include <stdint.h>

namespace Rendering
{

class CCmdQueue
{
public:
	auto Get() { return CmdQueue.Get(); }

	void Init(Ptr<ID3D12Device2>& InDevice, D3D12_COMMAND_LIST_TYPE InCmdListType)
	{
		this->CmdListType = InCmdListType;
		this->Device = InDevice;

		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = CmdListType;
		desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 0;

		VALIDATE(Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&CmdQueue)));
		VALIDATE(Device->CreateFence(CurrentFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)));

		FenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		assert(FenceEvent);
	}

	Ptr<ID3D12GraphicsCommandList2> GetCommandList()
	{
		Ptr<ID3D12CommandAllocator>		CmdAllocator;
		Ptr<ID3D12GraphicsCommandList2>	CmdList;

		if (!AllocatorQueue.empty() && IsFenceComplete(AllocatorQueue.front().fenceValue))
		{
			CmdAllocator = AllocatorQueue.front().CmdAllocator;
			AllocatorQueue.pop();
		 
			VALIDATE(CmdAllocator->Reset());
		}
		else
		{
			CmdAllocator = CreateCommandAllocator();
		}

		if (!ListQueue.empty())
		{
			CmdList = ListQueue.front();
			ListQueue.pop();
		 
			VALIDATE(CmdList->Reset(CmdAllocator.Get(), nullptr));
		}
		else
		{
			CmdList = CreateCommandList(CmdAllocator);
		}

		// Associate the command allocator with the command list so that it can be
		// retrieved when the command list is executed.
		VALIDATE(CmdList->SetPrivateDataInterface(__uuidof(ID3D12CommandAllocator), CmdAllocator.Get()));
	 
		return CmdList;
	}

	uint64_t Execute(Ptr<ID3D12GraphicsCommandList2>& CmdList)
	{
		CmdList->Close();

		ID3D12CommandAllocator* CmdAllocator;
		{
			UINT PrivateDataSize;
			VALIDATE(CmdList->GetPrivateData(
				__uuidof(ID3D12CommandAllocator),
				&PrivateDataSize,
				&CmdAllocator));
		}

		ID3D12CommandList* const CmdListsArray[]{
			CmdList.Get()
		};

		CmdQueue->ExecuteCommandLists(ArraySize(CmdListsArray), CmdListsArray);
		uint64_t FenceValueFromSignal = Signal();

		AllocatorQueue.push(CmdAllocatorEntry{ FenceValueFromSignal, CmdAllocator });
		ListQueue.emplace(std::move(CmdList));

		CmdAllocator->Release(); // decrement COM reference value

		return FenceValueFromSignal;
	}

	bool IsFenceComplete(uint64_t fenceValue)
	{
		return fenceValue >= Fence->GetCompletedValue();
	}

	uint64_t Signal()
	{
		uint64_t fenceValueForSignal = ++CurrentFenceValue;
		VALIDATE(CmdQueue->Signal(Fence.Get(), fenceValueForSignal));

		return fenceValueForSignal;
	}

	void WaitForFenceValue(uint64_t fenceValue)
	{
		if (Fence->GetCompletedValue() < fenceValue)
		{
			VALIDATE(Fence->SetEventOnCompletion(fenceValue, FenceEvent));
			::WaitForSingleObject(FenceEvent, static_cast<DWORD>(-1));
		}
	}

	void Flush()
	{
		uint64_t fenceValueForSignal = Signal();
		WaitForFenceValue(fenceValueForSignal);
	}

protected:
	Ptr<ID3D12CommandAllocator>		CreateCommandAllocator()
	{
		Ptr<ID3D12CommandAllocator> CmdAllocator;
		VALIDATE(Device->CreateCommandAllocator(CmdListType, IID_PPV_ARGS(&CmdAllocator)));

		return CmdAllocator;
	}
	Ptr<ID3D12GraphicsCommandList2> CreateCommandList(Ptr<ID3D12CommandAllocator> Allocator)
	{
		Ptr<ID3D12GraphicsCommandList2> CmdList;
		VALIDATE(Device->CreateCommandList(0, CmdListType, Allocator.Get(), nullptr,IID_PPV_ARGS(&CmdList)));

		return CmdList;
	}

private:
	Ptr<ID3D12CommandQueue>	CmdQueue;
	Ptr<ID3D12Device2>      Device;
	Ptr<ID3D12Fence>        Fence;

	HANDLE                  FenceEvent;
	D3D12_COMMAND_LIST_TYPE CmdListType;
	uint64_t                CurrentFenceValue = 0;

	struct CmdAllocatorEntry
	{
		uint64_t fenceValue;
		Ptr<ID3D12CommandAllocator> CmdAllocator;
	};

	using CmdAllocatorQueue = Queue<CmdAllocatorEntry>;
	using CmdListQueue = Queue<Ptr<ID3D12GraphicsCommandList2>>;

	CmdAllocatorQueue   AllocatorQueue;
	CmdListQueue        ListQueue;
};

}
