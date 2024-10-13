#pragma once

#include "Render/RenderForwardDeclarations.h"
#include "Containers/ComPtr.h"

struct D3D12CmdList
{
	TComPtr<ID3D12GraphicsCommandList7> CommandList;
	TComPtr<ID3D12CommandAllocator> CommandAllocator;
	D3D12_COMMAND_LIST_TYPE Type;

	operator ID3D12GraphicsCommandList7*() { return CommandList.Get(); }
	ID3D12GraphicsCommandList7* operator->() { return CommandList.Get(); }
	ID3D12GraphicsCommandList7* Get() { return CommandList.Get(); }
};
