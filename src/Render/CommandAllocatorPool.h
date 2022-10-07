#pragma once
#include <Render/RenderForwardDeclarations.h>

ComPtr<ID3D12CommandAllocator> GetCommandAllocator(D3D12_COMMAND_LIST_TYPE Type);
void DiscardCommandAllocator(ComPtr<ID3D12CommandAllocator>& Allocator, D3D12_COMMAND_LIST_TYPE Type);
