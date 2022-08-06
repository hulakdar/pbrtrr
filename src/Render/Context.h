#pragma once

#include <Common.h>
#include <dxgiformat.h>

#include <Render/RenderForwardDeclarations.h>

#include "Containers/ArrayView.h"
#include "Containers/String.h"
#include "Containers/Function.h"

bool IsSwapChainReady();
void WaitForFenceValue(ID3D12Fence* Fence, u64 FenceValue, void* Event);
void FlushQueue(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64& FenceValue, void* FenceEvent);
u64 Signal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, u64& FenceValue);

void UploadTextureData(TextureData& TexData, const u8* RawData, u32 RawDataSize);
void UploadBufferData(ID3D12Resource* Destination, const void* Data, uint64_t Size, D3D12_RESOURCE_STATES TargetState);
void UploadBufferData(ID3D12Resource* Destination, u64 Size, D3D12_RESOURCE_STATES TargetState, TFunction<void(void*, u64)> UploadFunction);
void FlushUpload(u64 CurrentFrameID);

void CreateRTV(TextureData& TexData);
void CreateSRV(TextureData& TexData);
void CreateUAV(TextureData& TexData);
void CreateDSV(TextureData& TexData);

ComPtr<ID3D12PipelineState>  CreatePSO(D3D12_COMPUTE_PIPELINE_STATE_DESC* PSODesc);
ComPtr<ID3D12PipelineState>  CreatePSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* PSODesc);

ComPtr<ID3D12Fence>               CreateFence(u64 InitialValue = 0, D3D12_FENCE_FLAGS Flags = (D3D12_FENCE_FLAGS)0);
ComPtr<ID3D12CommandAllocator>    CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE Type);
ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ID3D12CommandAllocator* CommandAllocator, D3D12_COMMAND_LIST_TYPE Type);

void BindDescriptors(ID3D12GraphicsCommandList* CommandList, TextureData& Tex);
void BindRenderTargets(ID3D12GraphicsCommandList* CommandList, TArrayView<u32> RTVs, u32 DSV);
void ClearRenderTarget(ID3D12GraphicsCommandList* CommandList, u32 DSV, float* clearColor);
void ClearDepth(ID3D12GraphicsCommandList* CommandList, u32 DSV, float depthValue);

void CreateResourceForTexture(
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

ComPtr<ID3D12Resource> CreateBuffer(u64 Size, BufferType Type = (BufferType)0);

ComPtr<ID3D12PipelineState> CreateShaderCombination(
	TArrayView<D3D12_INPUT_ELEMENT_DESC> PSOLayout,
	TArrayView<StringView> EntryPoints,
	StringView ShaderFile,
	TArrayView<DXGI_FORMAT> RenderTargetFormats,
	D3D12_CULL_MODE CullMode,
	DXGI_FORMAT DepthTargetFormat = DXGI_FORMAT_UNKNOWN,
	TArrayView<D3D12_RENDER_TARGET_BLEND_DESC> BlendDescs = TArrayView<D3D12_RENDER_TARGET_BLEND_DESC>(nullptr)
);

void InitRender(System::Window& Window);

ID3D12CommandQueue* GetGraphicsQueue();
ID3D12Resource*     GetBackBufferResource(u32 Index);
TextureData&        GetBackBuffer(u32 Index);
ID3D12Device*       GetGraphicsDevice();

void PresentCurrentBackBuffer();
void Submit(D3D12CmdList& CmdList, u64 CurrentFrameID);
void Submit(TArray<D3D12CmdList>& CmdLists, u64 CurrentFrameID);
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

static const u8 BACK_BUFFER_COUNT = 3;
static const DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT SCENE_COLOR_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;
static const DXGI_FORMAT READBACK_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;

struct GraphicsDeviceCapabilities
{
	u64 FeatureLevel : 15;
	u64 Tearing : 1;
	u64 DoublePrecisionFloatInShaders : 1;
	u64 OutputMergerLogicOp    : 1;
	u64 MinPrecisionSupport    : 2; // D3D12_SHADER_MIN_PRECISION_SUPPORT
	u64 TiledResourcesTier     : 3; // D3D12_TILED_RESOURCES_TIER
	u64 ResourceBindingTier    : 2; // D3D12_RESOURCE_BINDING_TIER
	u64 PSSpecifiedStencilRef  : 1;
	u64 TypedUAVLoadAdditionalFormats  : 1;
	u64 RasterizerOrderefViews : 1;
	u64 ConservativeRasterizationTier : 2; // D3D12_CONSERVATIVE_RASTERIZATION_TIER
	u64 GPUVirtualAddressMaxBits : 5; // - 32
	u64 ResourceHeapTier : 1; // D3D12_RESOURCE_HEAP_TIER - 1 
	u64 ShaderModel5     : 1; // D3D_SHADER_MODEL >= D3D_SHADER_MODEL_5_1
	u64 ShaderModel6     : 4; // D3D_SHADER_MODEL - 0x60
	u64 WaveOperations   : 1;
	u64 WaveLaneCount    : 7;
	u64 WaveCountTotal   : 10;
	u64 Int64ShaderOperations : 1;
	u64 RootSignatureVersion : 1; // D3D_ROOT_SIGNATURE_VERSION - 1
	u64 TileBasedRenderer : 1;
	u64 UMA : 1;
	u64 CacheCoherentUMA : 1;
	u64 DepthBoundTest   : 1;
	u64 ProgrammableSamplePositionsTier : 2; // D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER
	u64 ShaderCacheSupportSinglePSO : 1;
	u64 ShaderCacheSupportLibrary : 1;
	u64 ShaderCacheSupportAutomaticInprocCache : 1;
	u64 ShaderCacheSupportAutomaticDiscCache : 1;
	u64 ShaderCacheSupportDriverManagedCache : 1;
	u64 ShaderCacheSupportShaderControlClear : 1;
	u64 ShaderCacheSupportShaderSessionDelete : 1;
	u64 ExistingSystemMemoryHeaps : 1;
	u64 MSAA64KbAlignedTexture : 1;
	u64 SharedResourceCompatibilityTier : 2; // D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER
	u64 Native16BitShaderOps : 1;
	u64 HeapSerialization : 1;
	u64 RenderPassTier : 2; // D3D12_RENDER_PASS_TIER
	u64 Raytracing : 1; // D3D12_RAYTRACING_TIER != 0
	u64 RaytracingTier : 1; // D3D12_RAYTRACING_TIER - 10
	u64 AdditionalShadingRates : 1;
	u64 PerPrimitiveShadingRate : 1;
	u64 VariableShadingRateTier : 2; // D3D12_VARIABLE_SHADING_RATE_TIER
	u64 ShadingRateImageTileSize : 6;
	u64 BackgroundProcessing : 1;
	u64 MeshShader : 1;
	u64 SamplerFeedback : 1;
	u64 SamplerFeedbackTier : 1; // D3D12_SAMPLER_FEEDBACK_TIER == 100
// Windows 11 exclusive below
	u64 UnalignedBlockTextures : 1;
	u64 MeshShaderPipelineStats : 1;
	u64 MeshShaderFullRangeRenderTargetArrayIndex : 1;
	u64 AtomicInt64OnTypedResources : 1;
	u64 AtomicInt64OnGroupSharedResources : 1;
	u64 DerivativesInMeshAndAmpliciationShaders : 1;
	u64 WaveMatrixOperations : 1;
	u64 VariableRateShadingSumCombiner : 1;
	u64 MeshPerPrimitiveShadingRate : 1;
	u64 AtomicInt64OnDescriptorHeapResource : 1;
};
