#pragma once

#include <stdint.h>
#include <dxgiformat.h>

#include <Render/RenderForwardDeclarations.h>

#include "Containers/ArrayView.h"
#include "Containers/String.h"

bool IsSwapChainReady();
void WaitForFenceValue(ID3D12Fence* Fence, uint64_t FenceValue, void* Event);
void FlushQueue(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, uint64_t& FenceValue, void* FenceEvent);
uint64_t Signal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, uint64_t& FenceValue);

void UploadTextureData(TextureData& TexData, const uint8_t* RawData, uint32_t RawDataSize = 0);
void CreateRTV(TextureData& TexData);
void CreateSRV(TextureData& TexData);
void CreateUAV(TextureData& TexData);
void CreateDSV(TextureData& TexData);

ComPtr<ID3D12PipelineState>  CreatePSO(D3D12_COMPUTE_PIPELINE_STATE_DESC* PSODesc);
ComPtr<ID3D12PipelineState>  CreatePSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* PSODesc);

ComPtr<ID3D12Fence>               CreateFence(uint64_t InitialValue = 0, D3D12_FENCE_FLAGS Flags = (D3D12_FENCE_FLAGS)0);
ComPtr<ID3D12CommandAllocator>    CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE Type);
ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ID3D12CommandAllocator* CommandAllocator, D3D12_COMMAND_LIST_TYPE Type);

void BindDescriptors(ID3D12GraphicsCommandList* CommandList, TextureData& Tex);
void BindRenderTargets(ID3D12GraphicsCommandList* CommandList, TArrayView<uint32_t> RTVs, uint32_t DSV);
void ClearRenderTarget(ID3D12GraphicsCommandList* CommandList, uint32_t DSV, float* clearColor);
void ClearDepth(ID3D12GraphicsCommandList* CommandList, uint32_t DSV, float depthValue);

void CreateTexture(
	TextureData& TexData,
	D3D12_RESOURCE_FLAGS Flags,
	D3D12_RESOURCE_STATES InitialState,
	D3D12_CLEAR_VALUE* ClearValue = nullptr
);
ComPtr<ID3D12Resource> CreateResource(
	const D3D12_RESOURCE_DESC* ResourceDescription,
	const D3D12_HEAP_PROPERTIES* HeapProperties,
	D3D12_RESOURCE_STATES InitialState,
	const D3D12_CLEAR_VALUE* ClearValue = nullptr
);
ComPtr<ID3D12Resource> CreateBuffer(uint64_t Size, bool bUploadBuffer = false, bool bStagingBuffer = false);

ComPtr<ID3D12PipelineState> CreateShaderCombination(
	TArrayView<D3D12_INPUT_ELEMENT_DESC> PSOLayout,
	TArrayView<StringView> EntryPoints,
	StringView ShaderFile,
	TArrayView<DXGI_FORMAT> RenderTargetFormats,
	DXGI_FORMAT DepthTargetFormat = DXGI_FORMAT_UNKNOWN,
	TArrayView<D3D12_RENDER_TARGET_BLEND_DESC> BlendDescs = TArrayView<D3D12_RENDER_TARGET_BLEND_DESC>(nullptr)
);

void UploadBufferData(ID3D12Resource* Destination, const void* Data, uint64_t Size, D3D12_RESOURCE_STATES TargetState);
void FlushUpload();
void WaitForUploadFinish();

void InitRender(System::Window& Window);

ID3D12CommandQueue* GetGraphicsQueue();
ID3D12Resource*     GetBackBufferResource(uint32_t Index);
TextureData&        GetBackBuffer(uint32_t Index);
ID3D12Device*       GetGraphicsDevice();

void PresentCurrentBackBuffer();

[[nodiscard ("Return value should be stored to later wait for it")]]
uint64_t ExecuteGraphics(ID3D12CommandList* CmdList, ID3D12Fence* Fence, uint64_t& CurrentFenceValue);
//[[nodiscard ("Return value should be stored to later wait for it")]]
//uint64_t ExecuteCopy(ID3D12CommandList* CmdList, ID3D12Fence* Fence, uint64_t& CurrentFenceValue);
//[[nodiscard ("Return value should be stored to later wait for it")]]
//uint64_t ExecuteCompute(ID3D12CommandList* CmdList, ID3D12Fence* Fence, uint64_t& CurrentFenceValue);

void Submit(D3D12CmdList& CmdList, uint64_t CurrentFrameID);

void CreateBackBufferResources(System::Window& Window);

enum ShaderType
{
	ePixelShader,
	eVertexShader,
	eGeometryShader,
	eHullShader,
	eDomainShader,
	eComputeShader,
	eShaderTypeCount,
};

static const uint32_t    BACK_BUFFER_COUNT = 3;
static const DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT SCENE_COLOR_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;
static const DXGI_FORMAT READBACK_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;

template<typename ConstantBufferType>
ComPtr<ID3D12Resource> CreateConstantBuffer()
{
	ZoneScoped;
	D3D12_RESOURCE_DESC ConstantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ConstantBufferType));

	ComPtr<ID3D12Resource> Result = CreateResource(&ConstantBufferDesc, &UploadHeapProperties, D3D12_RESOURCE_STATE_GENERIC_READ);

	ConstantBufferType* CB;
	Result->Map(0, nullptr, (void**)&CB);
	*CB = ConstantBufferType();
	Result->Unmap(0, nullptr);
	return Result;
}
