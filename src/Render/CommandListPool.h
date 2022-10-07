#pragma once
#include <Render/RenderForwardDeclarations.h>
#include "Containers/ComPtr.h"

struct D3D12CmdList
{
	ComPtr<ID3D12GraphicsCommandList> CommandList;
	ComPtr<ID3D12CommandAllocator> CommandAllocator;
	D3D12_COMMAND_LIST_TYPE Type;
	ID3D12GraphicsCommandList* operator->() { return CommandList.Get(); }
	ID3D12GraphicsCommandList* Get() { return CommandList.Get(); }
};

D3D12CmdList GetCommandList(D3D12_COMMAND_LIST_TYPE Type, const wchar_t* DebugName);
void DiscardCommandList(D3D12CmdList& CmdList);
