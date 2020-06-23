#include "Rendering/CmdQueue.h"
#include "Util/Util.h"

namespace Rendering
{

void CCmdQueue::Init(ComPtr<ID3D12Device2>& InDevice, D3D12_COMMAND_LIST_TYPE InCmdListType)
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
}

CCmdList CCmdQueue::GetCommandList()
{
	ComPtr<ID3D12CommandAllocator>	CmdAllocator;
	CCmdList					CmdList;

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
		CmdList = MOVE(ListQueue.front());
		ListQueue.pop();

		VALIDATE(CmdList.Get()->Reset(CmdAllocator.Get(), nullptr));
	}
	else
	{
		CmdList = CreateCommandList(CmdAllocator);
	}

	// Associate the command allocator with the command list so that it can be
	// retrieved when the command list is executed.
	VALIDATE(CmdList.Get()->SetPrivateDataInterface(__uuidof(ID3D12CommandAllocator), CmdAllocator.Get()));

	return CmdList;
}

uint64_t CCmdQueue::Execute(CCmdList& CmdList)
{
	CmdList.Get()->Close();

	ID3D12CommandAllocator* CmdAllocator;
	{
		UINT PrivateDataSize = sizeof(ID3D12CommandAllocator*);
		VALIDATE(CmdList.Get()->GetPrivateData(
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
	ListQueue.push(CmdList);

	CmdAllocator->Release(); // decrement COM reference value

	return FenceValueFromSignal;
}

bool CCmdQueue::IsFenceComplete(uint64_t fenceValue)
{
	return fenceValue >= Fence->GetCompletedValue();
}

uint64_t CCmdQueue::Signal()
{
	uint64_t fenceValueForSignal = ++CurrentFenceValue;
	VALIDATE(CmdQueue->Signal(Fence.Get(), fenceValueForSignal));

	return fenceValueForSignal;
}

void CCmdQueue::WaitForFenceValue(uint64_t fenceValue)
{
	if (Fence->GetCompletedValue() < fenceValue)
	{
		VALIDATE(Fence->SetEventOnCompletion(fenceValue, FenceEvent));
		::WaitForSingleObject(FenceEvent, static_cast<DWORD>(-1));
	}
}

void CCmdQueue::Flush()
{
	uint64_t fenceValueForSignal = Signal();
	WaitForFenceValue(fenceValueForSignal);
}

ComPtr<ID3D12CommandAllocator> CCmdQueue::CreateCommandAllocator()
{
	ComPtr<ID3D12CommandAllocator> CmdAllocator;
	VALIDATE(Device->CreateCommandAllocator(CmdListType, IID_PPV_ARGS(&CmdAllocator)));

	return CmdAllocator;
}

CCmdList CCmdQueue::CreateCommandList(ComPtr<ID3D12CommandAllocator> Allocator)
{
	CCmdList CmdList;
	VALIDATE(Device->CreateCommandList(0, CmdListType, Allocator.Get(), nullptr, IID_PPV_ARGS(&CmdList.CmdList)));

	return CmdList;
}

}
