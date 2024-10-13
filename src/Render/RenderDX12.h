#pragma once

#include <dxgiformat.h>

#include "Common.h"
#include "Render/RenderForwardDeclarations.h"

#include "Containers/ComPtr.generated.h"
#include "Containers/ArrayView.generated.h"
#include "Containers/String.generated.h"
#include "Containers/Function.generated.h"
#include "Containers/ComPtr.generated.h"
#include "Assets/Mesh.generated.h"
#include "Assets/Scene.generated.h"
#include "Assets/Material.generated.h"
#include "Assets/File.generated.h"
#include "Assets/Pak.generated.h"
#include "Render/Texture.generated.h"

static const u32 GENERAL_HEAP_SIZE = 4096;

struct TicketGPU {
	u64 Value;
	D3D12_COMMAND_LIST_TYPE QueueType;
};

struct APIMesh
{
	MeshDescription           Description;
	D3D12_GPU_VIRTUAL_ADDRESS VertexBufferCachedPtr;
	D3D12_GPU_VIRTUAL_ADDRESS IndexBufferCachedPtr;
	ID3D12PipelineState*      PSO;
};

struct Scene
{
	TArray<Node>                StaticGeometry;
	TArray<APIMesh>             MeshDatas;
	TArray<VirtualTexture>      Textures;
	TArray<MaterialDescription> Materials;

	TComPtr<ID3D12Resource>     WantedMips;
	TComPtr<ID3D12Resource>     PickingBuffer;
	TComPtr<ID3D12Resource>     VertexBuffer;
	TComPtr<ID3D12Resource>     IndexBuffer;

	PakFileReader FileReader;

	u16 PickedSRV = ~0;
	u16 DesiredMips[GENERAL_HEAP_SIZE];
};

struct Shader
{
	TComPtr<ID3D12PipelineState> PSO;
	ID3D12RootSignature* RootSignature = nullptr;
};

static const u8 BACK_BUFFER_COUNT = 3;
static const DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT SCENE_COLOR_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;
static const DXGI_FORMAT READBACK_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;

struct GraphicsDeviceCapabilities
{
	u64 FeatureLevel : 2;
	u64 Tearing : 1;
	u64 DoublePrecisionFloatInShaders : 1;
	u64 OutputMergerLogicOp    : 1;
	u64 PrecisionSupports10bit : 1; // D3D12_SHADER_MIN_PRECISION_SUPPORT
	u64 PrecisionSupports16bit : 1; // D3D12_SHADER_MIN_PRECISION_SUPPORT
	u64 TiledResourcesTier     : 3; // D3D12_TILED_RESOURCES_TIER
	u64 ResourceBindingTier    : 2; // D3D12_RESOURCE_BINDING_TIER
	u64 PSSpecifiedStencilRef  : 1;
	u64 TypedUAVLoadAdditionalFormats  : 1;
	u64 RasterizerOrderefViews : 1;
	u64 ConservativeRasterizationTier : 2; // D3D12_CONSERVATIVE_RASTERIZATION_TIER
	u64 GPUVirtualAddressMaxBits : 8;
	u64 ResourceHeapTier : 2;
	u64 ShaderModel6     : 1;
	u64 ShaderModelMinor : 3;
	u64 WaveOperations   : 1;
	u64 WaveLaneCountLog2 : 3;
	u64 ComputeUnitsCount : 7;
	u64 Int64ShaderOperations : 1;
	u64 RootSignatureVersion : 2;
	u64 TileBasedRenderer : 1;
	u64 UMA : 1;
	u64 CacheCoherentUMA : 1;
	u64 IsolatedMMU : 1;
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
	u64 ShadingRateImageTileSizeLog2 : 3;
	u64 BackgroundProcessing : 1;
	u64 MeshShaderTier : 1;
	u64 SamplerFeedback : 1;
	u64 SamplerFeedbackTier1 : 1;
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

	D3D12_FORMAT_SUPPORT1 GeneralSupport[192]{};
	D3D12_FORMAT_SUPPORT2 UAVSupport[192]{};
};
