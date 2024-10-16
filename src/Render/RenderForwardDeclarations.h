#pragma once
#include "Common.h"

namespace System
{
	class Window;
}

template <typename T>
struct TComPtr;

#pragma warning (push)
#pragma warning (disable : 4471)

enum DXGI_FORMAT;
enum DXGI_SWAP_CHAIN_FLAG;

enum D3D12_FENCE_FLAGS;
enum D3D12_RESOURCE_STATES;
enum D3D12_COMMAND_LIST_TYPE;
enum D3D12_RESOURCE_FLAGS;
enum D3D12_CULL_MODE;

struct D3D12_CLEAR_VALUE;
struct D3D12_RESOURCE_DESC;
struct D3D12_HEAP_PROPERTIES;
struct D3D12_INPUT_ELEMENT_DESC;
struct D3D12_RENDER_TARGET_BLEND_DESC;
struct D3D12_COMPUTE_PIPELINE_STATE_DESC;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;

struct ID3D12Fence;
struct ID3D12Device;
struct ID3D12Resource;
struct IDXGISwapChain2;
struct ID3D12CommandList;
struct ID3D12CommandQueue;
struct ID3D12PipelineState;
struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList;
struct ID3D12GraphicsCommandList7;
struct ID3D12RootSignature;
struct IDStorageFile;

typedef u64 D3D12_GPU_VIRTUAL_ADDRESS;

#pragma warning (pop)
