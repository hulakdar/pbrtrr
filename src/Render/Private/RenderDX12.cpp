#include "Render/RenderDX12.h"
#include "Render/Texture.h"
#include "Render/RenderThread.h"
#include "Render/CommandListPool.h"

#include "Containers/Map.h"
#include "Util/Math.h"
#include "Util/Debug.h"
#include "Util/Util.h"
#include "Threading/Mutex.h"

#include "System/Window.h"

#include <imgui.h>
#include <dxgi1_6.h>
#include <dstorage.h>
#include "d3dx12.h"

#include "Render/TransientResourcesPool.h"
#include <tracy/TracyD3D12.hpp>
#include <Assets/Shader.h>
#include <Assets/Material.h>
#include <Assets/File.h>
#include <Assets/Scene.h>
#include <WinPixEventRuntime/pix3.h>
#include <Render/Buffer.h>

static const u32 RTV_HEAP_SIZE = 4096;
static const u32 DSV_HEAP_SIZE = 32;

static const D3D12_HEAP_PROPERTIES DefaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
static const D3D12_HEAP_PROPERTIES UploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
static const D3D12_HEAP_PROPERTIES ReadbackHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
static const D3D12_HEAP_PROPERTIES UmaHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_MEMORY_POOL_L0);

static TComPtr<ID3D12DescriptorHeap>    gRTVDescriptorHeap;
static TComPtr<ID3D12DescriptorHeap>    gDSVDescriptorHeap;
static TComPtr<ID3D12DescriptorHeap>    gGeneralDescriptorHeap;
static TComPtr<ID3D12RootSignature>     gRootSignature;
static TComPtr<ID3D12RootSignature>     gComputeRootSignature;

static TComPtr<ID3D12CommandQueue> gQueues[4];
static TComPtr<ID3D12Fence>        gFences[4];
static std::atomic<u64>            gFenceValues[4];

static TComPtr<IDXGIFactory4> gDXGIFactory;
static TComPtr<ID3D12Device>  gDevice;

static u32 gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

static TComPtr<IDXGISwapChain2> gSwapChain;
static TextureData             gBackBuffers[BACK_BUFFER_COUNT] = {};
static HANDLE                  gSwapChainWaitableObject = nullptr;

static GraphicsDeviceCapabilities gDeviceCaps;

static IDStorageQueue2* gDirectStorageQueue;
static IDStorageStatusArray* gDirectStorageStatusArray;
static bool gDirectStorageNeedsFlush = false;
static u32 gDirectStorageNumRequests = 0;

ID3D12Device* GetGraphicsDevice()
{
	return gDevice.Get();
}

ID3D12CommandQueue* GetGPUQueue(D3D12_COMMAND_LIST_TYPE QueueType)
{
	CHECK(QueueType <= D3D12_COMMAND_LIST_TYPE_COPY && QueueType != D3D12_COMMAND_LIST_TYPE_BUNDLE, "Unsupported queue type");

	return gQueues[QueueType].Get();
}

namespace {
	bool CheckTearingSupport(TComPtr<IDXGIFactory4>& dxgiFactory)
	{
		BOOL allowTearing = FALSE;
	 
		TComPtr<IDXGIFactory5> factory5;
		if (SUCCEEDED(dxgiFactory.As(&factory5)))
		{
			if (FAILED(factory5->CheckFeatureSupport(
				DXGI_FEATURE_PRESENT_ALLOW_TEARING, 
				&allowTearing, sizeof(allowTearing))))
			{
				allowTearing = FALSE;
			}
		}
	 
		return allowTearing;
	}

	TComPtr<IDXGIFactory4> CreateFactory()
	{
		TComPtr<IDXGIFactory4> dxgiFactory;
		u32 FactoryFlags = 0;
//#ifdef DEBUG
#if 1
		FactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
		VALIDATE(CreateDXGIFactory2(FactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

		return dxgiFactory;
	}

#if 0
	void PrintBlob(IDxcBlobEncoding* ErrorBlob)
	{
		BOOL Known;
		u32 CodePage;
		ErrorBlob->GetEncoding(&Known, &CodePage);
		if (Known && CodePage != CP_UTF8)
		{
			std::wstring_view Str((wchar_t*)ErrorBlob->GetBufferPointer(), ErrorBlob->GetBufferSize());
			Debug::Print(Str);
		}
		else
		{
			std::string_view Str((char *)ErrorBlob->GetBufferPointer(), ErrorBlob->GetBufferSize());
			Debug::Print(Str);
		}
	}
#endif
}

void WaitForFenceValue(ID3D12Fence* Fence, u64 FenceValue, void* Event)
{
	ZoneScoped;
	if (Fence->GetCompletedValue() < FenceValue)
	{
		VALIDATE(Fence->SetEventOnCompletion(FenceValue, Event));
		::WaitForSingleObject(Event, MAXDWORD);
		PIXNotifyWakeFromFenceSignal(Event);
	}
}

void FlushQueue(D3D12_COMMAND_LIST_TYPE QueueType)
{
	ZoneScoped;
	TicketGPU Ticket = Signal(QueueType);
	WaitForCompletion(Ticket);
}

TComPtr<ID3D12Resource> CreateResource(const D3D12_RESOURCE_DESC* ResourceDescription, const D3D12_HEAP_PROPERTIES* HeapProperties, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* ClearValue)
{
	ZoneScoped;
	TComPtr<ID3D12Resource> Result;

	VALIDATE(gDevice->CreateCommittedResource(
		HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		ResourceDescription,
		InitialState,
		ClearValue,
		IID_PPV_ARGS(&Result)
	));
	return Result;
}

TComPtr<ID3D12Resource> CreateVirtualResource(const D3D12_RESOURCE_DESC* ResourceDescription, const D3D12_HEAP_PROPERTIES* HeapProperties, D3D12_RESOURCE_STATES InitialState)
{
	ZoneScoped;
	TComPtr<ID3D12Resource> Result;

	VALIDATE(gDevice->CreateReservedResource(
		ResourceDescription,
		InitialState,
		nullptr,
		IID_PPV_ARGS(&Result)
	));
	return Result;
}

TComPtr<ID3D12Resource> CreateBuffer(u64 Size, BufferType Type)
{
	D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(Size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	const D3D12_HEAP_PROPERTIES* HeapProps = &DefaultHeapProps;

	D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_COMMON;

	if (Type == BUFFER_UPLOAD)
	{
		BufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		HeapProps = &UploadHeapProperties;
		InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}
	else if (Type == BUFFER_READBACK)
	{
		BufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		HeapProps = &ReadbackHeapProperties;
		InitialState = D3D12_RESOURCE_STATE_COPY_DEST;
	}
	else if (gDeviceCaps.CacheCoherentUMA)
	{
		HeapProps = &UmaHeapProperties;
	}

	return CreateResource(&BufferDesc, HeapProps, InitialState, nullptr);
}

void SetupDeviceCapabilities(D3D_FEATURE_LEVEL FeatureLevel, ID3D12Device* Device)
{
	switch (FeatureLevel)
	{
		case D3D_FEATURE_LEVEL_12_0: gDeviceCaps.FeatureLevel = 0; break;
		case D3D_FEATURE_LEVEL_12_1: gDeviceCaps.FeatureLevel = 1; break;
		case D3D_FEATURE_LEVEL_12_2: gDeviceCaps.FeatureLevel = 2; break;
		default: CHECK(false);
	}
	gDeviceCaps.Tearing = CheckTearingSupport(gDXGIFactory);

	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS Options{};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof(Options))))
		{
			gDeviceCaps.DoublePrecisionFloatInShaders = Options.DoublePrecisionFloatShaderOps;
			gDeviceCaps.OutputMergerLogicOp = Options.OutputMergerLogicOp;
			gDeviceCaps.PrecisionSupports10bit = (Options.MinPrecisionSupport & D3D12_SHADER_MIN_PRECISION_SUPPORT_10_BIT) != 0;
			gDeviceCaps.PrecisionSupports16bit = (Options.MinPrecisionSupport & D3D12_SHADER_MIN_PRECISION_SUPPORT_16_BIT) != 0;
			gDeviceCaps.TiledResourcesTier = Options.TiledResourcesTier;
			gDeviceCaps.ResourceBindingTier = Options.ResourceBindingTier;
			gDeviceCaps.PSSpecifiedStencilRef = Options.PSSpecifiedStencilRefSupported;
			gDeviceCaps.TypedUAVLoadAdditionalFormats = Options.TypedUAVLoadAdditionalFormats;
			gDeviceCaps.RasterizerOrderefViews = Options.ROVsSupported;
			gDeviceCaps.ConservativeRasterizationTier = Options.ConservativeRasterizationTier;
			gDeviceCaps.GPUVirtualAddressMaxBits = Options.MaxGPUVirtualAddressBitsPerResource;
			gDeviceCaps.ResourceHeapTier = Options.ResourceHeapTier;
		}
	}
	{
		D3D12_FEATURE_DATA_SHADER_MODEL Options{ D3D_SHADER_MODEL_6_7 };
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &Options, sizeof(Options))))
		{
			gDeviceCaps.ShaderModel6 = (Options.HighestShaderModel >= D3D_SHADER_MODEL_6_0);
			gDeviceCaps.ShaderModelMinor = Options.HighestShaderModel < D3D_SHADER_MODEL_6_0 ? 1 : Options.HighestShaderModel - 0x60;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS1  Options{};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &Options, sizeof(Options))))
		{
			gDeviceCaps.WaveOperations = Options.WaveOps;
			gDeviceCaps.WaveLaneCountLog2 = __lzcnt(Options.WaveLaneCountMin);
			CHECK(Options.WaveLaneCountMin == Options.WaveLaneCountMax);
			gDeviceCaps.ComputeUnitsCount = Options.TotalLaneCount / Options.WaveLaneCountMin;
			gDeviceCaps.Int64ShaderOperations = Options.Int64ShaderOps;
		}
	}
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE Options { D3D_ROOT_SIGNATURE_VERSION_1_1 };
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &Options, sizeof(Options))))
		{
			gDeviceCaps.RootSignatureVersion = Options.HighestVersion;
		}
	}
	{
		D3D12_FEATURE_DATA_ARCHITECTURE1 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &Options, sizeof(Options))))
		{
			gDeviceCaps.TileBasedRenderer = Options.TileBasedRenderer;
			gDeviceCaps.UMA = Options.UMA;
			gDeviceCaps.CacheCoherentUMA = Options.CacheCoherentUMA;
			gDeviceCaps.IsolatedMMU = Options.IsolatedMMU;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS2  Options{};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &Options, sizeof(Options))))
		{
			gDeviceCaps.DepthBoundTest = Options.DepthBoundsTestSupported;
			gDeviceCaps.ProgrammableSamplePositionsTier = Options.ProgrammableSamplePositionsTier;
		}
	}
	{
		D3D12_FEATURE_DATA_SHADER_CACHE  Options { D3D12_SHADER_CACHE_SUPPORT_NONE };
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE, &Options, sizeof(Options))))
		{
			gDeviceCaps.ShaderCacheSupportSinglePSO = (Options.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_SINGLE_PSO) != 0;
			gDeviceCaps.ShaderCacheSupportLibrary = (Options.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY) != 0;
			gDeviceCaps.ShaderCacheSupportAutomaticInprocCache = (Options.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_AUTOMATIC_INPROC_CACHE) != 0;
			gDeviceCaps.ShaderCacheSupportAutomaticDiscCache = (Options.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_AUTOMATIC_DISK_CACHE) != 0;
			gDeviceCaps.ShaderCacheSupportDriverManagedCache = (Options.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_DRIVER_MANAGED_CACHE) != 0;
		}
	}
	{
		D3D12_FEATURE_DATA_EXISTING_HEAPS Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_EXISTING_HEAPS, &Options, sizeof(Options))))
		{
			gDeviceCaps.ExistingSystemMemoryHeaps = Options.Supported;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS4 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &Options, sizeof(Options))))
		{
			gDeviceCaps.MSAA64KbAlignedTexture = Options.MSAA64KBAlignedTextureSupported;
			gDeviceCaps.SharedResourceCompatibilityTier = Options.SharedResourceCompatibilityTier;
			gDeviceCaps.Native16BitShaderOps = Options.Native16BitShaderOpsSupported;
		}
	}
	{
		D3D12_FEATURE_DATA_SERIALIZATION Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_SERIALIZATION, &Options, sizeof(Options))))
		{
			gDeviceCaps.HeapSerialization = Options.HeapSerializationTier != 0;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5 , &Options, sizeof(Options))))
		{
			gDeviceCaps.RenderPassTier = Options.RenderPassesTier;
			gDeviceCaps.Raytracing = Options.RaytracingTier != 0;
			gDeviceCaps.RaytracingTier = Options.RaytracingTier - 10;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS6 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6 , &Options, sizeof(Options))))
		{
			gDeviceCaps.AdditionalShadingRates = Options.AdditionalShadingRatesSupported;
			gDeviceCaps.PerPrimitiveShadingRate = Options.PerPrimitiveShadingRateSupportedWithViewportIndexing;
			gDeviceCaps.VariableShadingRateTier = Options.VariableShadingRateTier;

			CHECK(Options.ShadingRateImageTileSize <= 128, "increase bits for ShadingRateImageTileSize");
			CHECK(__popcnt(Options.ShadingRateImageTileSize) == 1, "increase bits for ShadingRateImageTileSize");

			gDeviceCaps.ShadingRateImageTileSizeLog2 = __lzcnt(Options.ShadingRateImageTileSize);

			gDeviceCaps.BackgroundProcessing = Options.BackgroundProcessingSupported;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &Options, sizeof(Options))))
		{
			gDeviceCaps.MeshShaderTier = Options.MeshShaderTier == D3D12_MESH_SHADER_TIER_1;
			gDeviceCaps.SamplerFeedback = Options.SamplerFeedbackTier != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
			gDeviceCaps.SamplerFeedbackTier1 = Options.SamplerFeedbackTier == D3D12_SAMPLER_FEEDBACK_TIER_1_0;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS8 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS8, &Options, sizeof(Options))))
		{
			gDeviceCaps.UnalignedBlockTextures = Options.UnalignedBlockTexturesSupported;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS9 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &Options, sizeof(Options))))
		{
			gDeviceCaps.MeshShaderPipelineStats = Options.MeshShaderPipelineStatsSupported;
			gDeviceCaps.MeshShaderFullRangeRenderTargetArrayIndex = Options.MeshShaderSupportsFullRangeRenderTargetArrayIndex;
			gDeviceCaps.AtomicInt64OnTypedResources = Options.AtomicInt64OnTypedResourceSupported;
			gDeviceCaps.AtomicInt64OnGroupSharedResources = Options.AtomicInt64OnGroupSharedSupported;
			gDeviceCaps.DerivativesInMeshAndAmpliciationShaders = Options.DerivativesInMeshAndAmplificationShadersSupported;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS10 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS10, &Options, sizeof(Options))))
		{
			gDeviceCaps.MeshPerPrimitiveShadingRate = Options.MeshShaderPerPrimitiveShadingRateSupported;
			gDeviceCaps.VariableRateShadingSumCombiner = Options.VariableRateShadingSumCombinerSupported;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS11 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS11, &Options, sizeof(Options))))
		{
			gDeviceCaps.AtomicInt64OnDescriptorHeapResource = Options.AtomicInt64OnDescriptorHeapResourceSupported;
		}
	}

	{
		D3D12_FEATURE_DATA_FORMAT_SUPPORT Options {};
		for (DXGI_FORMAT Format = DXGI_FORMAT(0); Format <= DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE; Format = DXGI_FORMAT(Format + 1))
		{
			Options.Format = Format;
			if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Options, sizeof(Options))))
			{
				gDeviceCaps.GeneralSupport[Format] = Options.Support1;
				gDeviceCaps.UAVSupport    [Format] = Options.Support2;
			}
		}
	}
}

TComPtr<ID3D12Device> CreateDevice()
{
	TComPtr<ID3D12Device> Result;

	SIZE_T MaxSize = 0;
	TComPtr<IDXGIAdapter1> Adapter;
	D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_11_0;
#ifndef TEST_WARP
	D3D_FEATURE_LEVEL FeatureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
        D3D_FEATURE_LEVEL_1_0_CORE,
	};
	for (int i = 0; i < ArrayCount(FeatureLevels); ++i)
	{
		FeatureLevel = FeatureLevels[i];
		bool Success = false;
		for (u32 Idx = 0; DXGI_ERROR_NOT_FOUND != gDXGIFactory->EnumAdapters1(Idx, &Adapter); ++Idx)
		{
			DXGI_ADAPTER_DESC1 desc;
			Adapter->GetDesc1(&desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			if (desc.DedicatedVideoMemory > MaxSize && SUCCEEDED(D3D12CreateDevice(Adapter.Get(), FeatureLevel, IID_PPV_ARGS(&Result))))
			{
				Adapter->GetDesc1(&desc);
				//Debug::("D3D12-capable hardware found:", desc.Description, desc.DedicatedVideoMemory >> 20, "MB dedicated video memory");
				MaxSize = desc.DedicatedVideoMemory;
				Success = true;
			}
		}
		if (Success)
			break;
	}
	CHECK(FeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE, "wtf is this");
#endif

	if (Result.Get() == nullptr)
	{
		//Debug::("Failed to find a hardware adapter.  Falling back to WARP.\n");
		VALIDATE(gDXGIFactory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)));
		VALIDATE(D3D12CreateDevice(Adapter.Get(), FeatureLevel, IID_PPV_ARGS(&Result)));
	}

	SetupDeviceCapabilities(FeatureLevel, Result.Get());
#if !defined(PROFILE) && !defined(RELEASE)
	TComPtr<ID3D12InfoQueue> pInfoQueue;
	if (SUCCEEDED(Result.As(&pInfoQueue)))
	{
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

		// Suppress whole categories of messages
		//D3D12_MESSAGE_CATEGORY Categories[] = {};
 
		// Suppress messages based on their severity level
		D3D12_MESSAGE_SEVERITY Severities[] =
		{
			D3D12_MESSAGE_SEVERITY_INFO
		};
 
		// Suppress individual messages by their ID
		D3D12_MESSAGE_ID DenyIds[] = {
			D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
			//D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
			//D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
		};
 
		D3D12_INFO_QUEUE_FILTER NewFilter = {};
		//NewFilter.DenyList.NumCategories = _countof(Categories);
		//NewFilter.DenyList.pCategoryList = Categories;
		NewFilter.DenyList.NumSeverities = _countof(Severities);
		NewFilter.DenyList.pSeverityList = Severities;
		NewFilter.DenyList.NumIDs = _countof(DenyIds);
		NewFilter.DenyList.pIDList = DenyIds;
 
		VALIDATE(pInfoQueue->PushStorageFilter(&NewFilter));
	}
#endif

	return Result;
}

TComPtr<ID3D12CommandQueue> CreateCommandQueue(D3D12_COMMAND_LIST_TYPE QueueType)
{
	TComPtr<ID3D12CommandQueue> Result;

	D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
	QueueDesc.Type = QueueType;
	QueueDesc.NodeMask = 1;

	gDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Result));
	return Result;
}

TComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE Type, u32 NumDescriptors, D3D12_DESCRIPTOR_HEAP_FLAGS Flags)
{
	ZoneScoped;
	TComPtr<ID3D12DescriptorHeap> Result;

	D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
	Desc.NumDescriptors = NumDescriptors;
	Desc.Type = Type;
	Desc.Flags = Flags;

	VALIDATE(gDevice->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Result)));

	return Result;
}

TComPtr<ID3D12RootSignature> CreateRootSignature(TComPtr<ID3DBlob> RootBlob)
{
	ZoneScoped;
	TComPtr<ID3D12RootSignature> Result;
	{
		VALIDATE(
			gDevice->CreateRootSignature(
				0,
				RootBlob->GetBufferPointer(),
				RootBlob->GetBufferSize(),
				IID_PPV_ARGS(&Result)
			)
		);
	}
	return Result;
}

static u32 gPresentFlags = 0;
static u32 gSyncInterval = 2;

void InitRender(System::Window& Window)
{
	ZoneScoped;

#if DEBUG
	// d3d12 debug layer
	{
		TComPtr<ID3D12Debug3> debugInterface;
		VALIDATE(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
		debugInterface->EnableDebugLayer();
		//debugInterface->SetEnableGPUBasedValidation(true);
		//debugInterface->SetEnableSynchronizedCommandQueueValidation(true);
	}
	{
		TComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
		VALIDATE(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)));

		if (pDredSettings)
		{
			// Turn on auto-breadcrumbs and page fault reporting.
			pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		}
	}
#endif

	gDXGIFactory = CreateFactory();
	gDevice = CreateDevice();

	{
		DSTORAGE_QUEUE_DESC QueueDesc{};
		QueueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
		QueueDesc.Capacity = DSTORAGE_MIN_QUEUE_CAPACITY;
		QueueDesc.Priority = DSTORAGE_PRIORITY_REALTIME;
		QueueDesc.Name = "DirectStorage main queue: source->file, priority->realtime";
		QueueDesc.Device = gDevice.Get();
		GetStorageFactory()->CreateQueue(&QueueDesc, IID_PPV_ARGS(&gDirectStorageQueue));
		GetStorageFactory()->CreateStatusArray(UINT16_MAX, "Status array", IID_PPV_ARGS(&gDirectStorageStatusArray));
	}

	for (int i = 0; i < ArrayCount(gDescriptorSizes); ++i)
	{
		gDescriptorSizes[i] = gDevice->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}
	for (D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_DIRECT; Type <= D3D12_COMMAND_LIST_TYPE_COPY; Type = (D3D12_COMMAND_LIST_TYPE)(Type + 1))
	{
		if (Type != D3D12_COMMAND_LIST_TYPE_BUNDLE)
		{
			gQueues[Type] = CreateCommandQueue(Type);
		}
		gFences[Type] = CreateFence(0);
		gFenceValues[Type] = 1;
	}

	gGeneralDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GENERAL_HEAP_SIZE, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
	gRTVDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_HEAP_SIZE, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
	gDSVDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DSV_HEAP_SIZE, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

	CreateBackBufferResources(Window);

	if (gDeviceCaps.Tearing)
	{
		gPresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
		gSyncInterval = 0;
	}

	// raster
	{
		CD3DX12_ROOT_PARAMETER Params[4] = {};

		CD3DX12_DESCRIPTOR_RANGE RangeSRVs(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GENERAL_HEAP_SIZE, 0);
		Params[0].InitAsDescriptorTable(1, &RangeSRVs);

		Params[1].InitAsUnorderedAccessView(0);
		Params[2].InitAsUnorderedAccessView(1);

		Params[3].InitAsConstants(20, 0);

		CD3DX12_STATIC_SAMPLER_DESC Samplers[2] = {};
		Samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
		Samplers[1].Init(1, D3D12_FILTER_ANISOTROPIC);

		CD3DX12_ROOT_SIGNATURE_DESC DescRootSignature;

		DescRootSignature.Init(
			ArrayCount(Params), Params,
			ArrayCount(Samplers), Samplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);

		TComPtr<ID3DBlob> RootBlob;
		{
			TComPtr<ID3DBlob> ErrorBlob;
			HRESULT HR = D3D12SerializeRootSignature(
				&DescRootSignature,
				D3D_ROOT_SIGNATURE_VERSION_1,
				&RootBlob,
				&ErrorBlob);
			if (!SUCCEEDED(HR))
			{
				//PrintBlob(ErrorBlob.Get());
				//Debug::Print(StringView((char*)ErrorBlob->GetBufferPointer(), ErrorBlob->GetBufferSize()));
				DEBUG_BREAK();
			}
		}

		gRootSignature = CreateRootSignature(RootBlob);
	}

	// compute
	{
		CD3DX12_ROOT_PARAMETER Params[4] = {};

		CD3DX12_DESCRIPTOR_RANGE RangeSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		Params[0].InitAsDescriptorTable(1, &RangeSRV);
		Params[1].InitAsUnorderedAccessView(0);

		CD3DX12_DESCRIPTOR_RANGE RangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 12, 1);
		Params[2].InitAsDescriptorTable(1, &RangeUAV);
		Params[3].InitAsConstants(4, 0);

		CD3DX12_STATIC_SAMPLER_DESC Samplers[1] = {};
		Samplers[0].Init(
			0,
			D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP
		);

		CD3DX12_ROOT_SIGNATURE_DESC DescRootSignature;

		DescRootSignature.Init(
			ArrayCount(Params), Params,
			ArrayCount(Samplers), Samplers,
			D3D12_ROOT_SIGNATURE_FLAG_NONE	
		);

		TComPtr<ID3DBlob> RootBlob;
		{
			TComPtr<ID3DBlob> ErrorBlob;
			HRESULT HR = D3D12SerializeRootSignature(
				&DescRootSignature,
				D3D_ROOT_SIGNATURE_VERSION_1,
				&RootBlob,
				&ErrorBlob);
			if (!SUCCEEDED(HR))
			{
				//Debug::Print(StringView((char*)ErrorBlob->GetBufferPointer(), ErrorBlob->GetBufferSize()));
				printf("%.*s\n", (int)ErrorBlob->GetBufferSize(), (char*)ErrorBlob->GetBufferPointer());
				//PrintBlob(ErrorBlob.Get());
				DEBUG_BREAK();
			}
		}

		gComputeRootSignature = CreateRootSignature(RootBlob);
	}

	InitUpload();
}

TComPtr<ID3D12Fence> CreateFence(u64 InitialValue)
{
	ZoneScoped;
	TComPtr<ID3D12Fence> Result;

	VALIDATE(gDevice->CreateFence(InitialValue, D3D12_FENCE_FLAGS::D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Result)));
	return Result;
}

static u16 StatusArrayIndex = 0;

void DrawDebugInfoDX12()
{
	ImGui::Begin("RenderDX12");

	HANDLE ErrorEvent = gDirectStorageQueue->GetErrorEvent();
	if (WaitForSingleObjectEx(ErrorEvent, 0, true) == WAIT_OBJECT_0)
	{
		DSTORAGE_ERROR_RECORD Record;
		gDirectStorageQueue->RetrieveErrorRecord(&Record);
		ImGui::Text("FailureCount:%d", Record.FailureCount);
	}
	{
		DSTORAGE_QUEUE_INFO Info;
		gDirectStorageQueue->Query(&Info);
		ImGui::Text("Desc:");
		ImGui::Text("	Name: %s", Info.Desc.Name);
		ImGui::Text("	Capacity: %d", Info.Desc.Capacity);
		ImGui::Text("EmptySlotCount: %d", Info.EmptySlotCount);
		ImGui::Text("RequestCountUntilAutoSubmit: %d", Info.RequestCountUntilAutoSubmit);
	}
	ImGui::Text("NumRequests:%d", gDirectStorageNumRequests);
	ImGui::Text("CompletionResults:");
	for (int i = 0; i < 65535;++i)
	{
		if (gDirectStorageStatusArray->IsComplete(i))
		{
			HRESULT CompletionResult = gDirectStorageStatusArray->GetHResult(i);
		
			ImGui::Text("%d", CompletionResult);
		}
		else
		{
			break;
		}
	}
	ImGui::End();
}

void PresentCurrentBackBuffer()
{
	ZoneScoped;

	PIXScopedEvent(GetGPUQueue(D3D12_COMMAND_LIST_TYPE_DIRECT), __LINE__, "Present");

	VALIDATE(gSwapChain->Present(gSyncInterval, gPresentFlags));

	UploadOnPresent();
}

TComPtr<ID3D12PipelineState> CreatePSO(D3D12_COMPUTE_PIPELINE_STATE_DESC* PSODesc)
{
	ZoneScoped;
	TComPtr<ID3D12PipelineState> Result;
	VALIDATE(gDevice->CreateComputePipelineState(PSODesc, IID_PPV_ARGS(&Result)));
	return Result;
}

TComPtr<ID3D12PipelineState> CreatePSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* PSODesc)
{
	ZoneScoped;
	TComPtr<ID3D12PipelineState> Result;

	VALIDATE(gDevice->CreateGraphicsPipelineState(PSODesc, IID_PPV_ARGS(&Result)));
	return Result;
}

Shader CreateShaderCombinationGraphics(
	u8              MeshDescFlags,
	RawDataView     VS,
	RawDataView     PS,
	TArrayView<u32> RTDesc,
	u32             DepthDesc
)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};


	D3D12_INPUT_ELEMENT_DESC Elements[10];
	u32 Count = 0;

	if (MeshDescFlags & MeshFlags::GUI)
	{
		Elements[0] = D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,offsetof(ImDrawVert, pos) };
		Elements[1] = D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,offsetof(ImDrawVert, uv) };
		Elements[2] = D3D12_INPUT_ELEMENT_DESC{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col) };
		Count = 3;

		PSODesc.BlendState.RenderTarget[0] = D3D12_RENDER_TARGET_BLEND_DESC {
			TRUE, FALSE,
			D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
			D3D12_BLEND_ZERO, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_LOGIC_OP_NOOP,
			D3D12_COLOR_WRITE_ENABLE_ALL,
		};
		PSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	}
	else
	{
		DXGI_FORMAT PositionFormat = DXGI_FORMAT_R32G32B32_FLOAT;

		bool bPositionPacked = MeshDescFlags & MeshFlags::PositionPacked;
		if (bPositionPacked)
		{
			PositionFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
		}
		Elements[Count++] = D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, PositionFormat, 0, 0 };
		u8 Offset = bPositionPacked ? sizeof(Vec4PackShorts) : sizeof(Vec3);

		if (MeshDescFlags & MeshFlags::HasNormals)
		{
			Elements[Count++] = D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R16G16B16A16_UNORM, 0, Offset };
			Offset += sizeof(Vec4PackUnorm);
		}
		if (MeshDescFlags & MeshFlags::HasUV0)
		{
			Elements[Count++] = D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, Offset };
			Offset += sizeof(Vec2h);
		}
		else
		{
			Elements[Count++] = D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 0 };
		}
		if (MeshDescFlags & MeshFlags::HasVertexColor)
		{
			Elements[Count++] = D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, Offset };
			Offset += sizeof(TVec4<u8>);
		}
		else
		{
			Elements[Count++] = D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0 };
		}
		PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		PSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	}

	PSODesc.NumRenderTargets = (u32)RTDesc.size();

	CHECK(PSODesc.NumRenderTargets <= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT, "D3D12 Does not support more then 8 render targets.");
	for (u32 i = 0; i < PSODesc.NumRenderTargets; ++i)
	{
		PSODesc.RTVFormats[i] = (DXGI_FORMAT)RTDesc[i];
	}
	if (DepthDesc != DXGI_FORMAT_UNKNOWN)
	{
		PSODesc.DSVFormat = (DXGI_FORMAT)DepthDesc;
		PSODesc.DepthStencilState.DepthEnable = true;
		PSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		PSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
	}

	PSODesc.VS.pShaderBytecode = VS.data();
	PSODesc.VS.BytecodeLength  = VS.size();
	PSODesc.PS.pShaderBytecode = PS.data();
	PSODesc.PS.BytecodeLength  = PS.size();
	PSODesc.pRootSignature = gRootSignature.Get();
	PSODesc.InputLayout.NumElements = Count;
	PSODesc.InputLayout.pInputElementDescs = Elements;
	PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	PSODesc.SampleDesc.Count = 1;
	PSODesc.SampleMask = 0xFFFFFFFF;
	PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	Shader Result;
	Result.PSO = CreatePSO(&PSODesc);
	Result.RootSignature = PSODesc.pRootSignature;
	return Result;
}

Shader CreateShaderCombinationCompute(
	RawDataView CS
)
{
/*
	LPVOID Data = ShaderBlob->GetBufferPointer();
	SIZE_T Size = ShaderBlob->GetBufferSize();

	TComPtr<ID3D12ShaderReflection> Reflection;
	D3DReflect(Data, Size, IID_PPV_ARGS(&Reflection));

	D3D12_SHADER_DESC Desc;
	{
		Reflection->GetDesc(&Desc);
		D3D12_SHADER_VERSION_TYPE ShaderType = (D3D12_SHADER_VERSION_TYPE)D3D12_SHVER_GET_TYPE(Desc.Version);
		CHECK(Type == ShaderType, "Parsing error?");
	}

	for (u32 i = 0; i < Desc.ConstantBuffers; ++i)
	{
		ID3D12ShaderReflectionConstantBuffer *ConstantBufferReflection = Reflection->GetConstantBufferByIndex(i);

		D3D12_SHADER_BUFFER_DESC ConstantBufferDesc;
		ConstantBufferReflection->GetDesc(&ConstantBufferDesc);
		for (u32 j = 0; j < Desc.ConstantBuffers; ++j)
		{
			ID3D12ShaderReflectionVariable* VariableReflection = ConstantBufferReflection->GetVariableByIndex(j);
			D3D12_SHADER_VARIABLE_DESC VariableDesc;
			VariableReflection->GetDesc(&VariableDesc);
			continue;
		}
		continue;
	}

	for (u32 i = 0; i < Desc.BoundResources; ++i)
	{
		D3D12_SHADER_INPUT_BIND_DESC BindDesc;
		Reflection->GetResourceBindingDesc(i, &BindDesc);
		continue;
	}

	for (u32 i = 0; i < Desc.InputParameters; ++i)
	{
		D3D12_SIGNATURE_PARAMETER_DESC InputDesc;
		Reflection->GetInputParameterDesc(i, &InputDesc);
		continue;
	}

	for (u32 i = 0; i < Desc.OutputParameters; ++i)
	{
		D3D12_SIGNATURE_PARAMETER_DESC OutputDesc;
		Reflection->GetOutputParameterDesc(i, &OutputDesc);
		continue;
	}
*/

	D3D12_COMPUTE_PIPELINE_STATE_DESC PSODesc = {};
	PSODesc.CS.BytecodeLength = CS.size();
	PSODesc.CS.pShaderBytecode = CS.data();
	PSODesc.pRootSignature = gComputeRootSignature.Get();

	Shader Result;
	Result.PSO = CreatePSO(&PSODesc);
	Result.RootSignature = PSODesc.pRootSignature;
	return Result;
}

TComPtr<ID3D12CommandAllocator> CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE QueueType)
{
	ZoneScoped;
	TComPtr<ID3D12CommandAllocator> Result;

	VALIDATE(gDevice->CreateCommandAllocator(QueueType, IID_PPV_ARGS(&Result)));
	return Result;
}

TComPtr<ID3D12GraphicsCommandList7> CreateCommandList(ID3D12CommandAllocator* CommandAllocator, D3D12_COMMAND_LIST_TYPE Type)
{
	ZoneScoped;
	TComPtr<ID3D12GraphicsCommandList7> Result;

	VALIDATE(gDevice->CreateCommandList(0, Type, CommandAllocator, nullptr, IID_PPV_ARGS(&Result)));
	VALIDATE(Result->Close());
	return Result;
}

void CreateResourceForTexture(TextureData& TexData, D3D12_RESOURCE_FLAGS Flags, D3D12_RESOURCE_STATES InitialState, D3D12_CLEAR_VALUE* ClearValue)
{
	//ZoneScoped;
	D3D12_RESOURCE_DESC TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		(DXGI_FORMAT)TexData.Format,
		TexData.Width, TexData.Height,
		1, TexData.NumMips,
		TexData.SampleCount, 0,
		Flags
	);

	const D3D12_HEAP_PROPERTIES* HeapProps = &DefaultHeapProps;
	if (gDeviceCaps.CacheCoherentUMA)
	{
		HeapProps = &UmaHeapProperties;
	}
	TComPtr<ID3D12Resource> Resource = CreateResource(&TextureDesc, HeapProps, InitialState, ClearValue);
	TexData.ID = StoreTexture(Resource.Get(), "");
	TexData.Flags = (u8)Flags;
}

u64 GetGeneralHandleGPU(u32 Index)
{
	CHECK(Index != UINT_MAX, "Uninitialized index");
	CD3DX12_GPU_DESCRIPTOR_HANDLE rtv(gGeneralDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		Index, gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);

	return rtv.ptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle(u32 Index)
{
	CHECK(Index != UINT_MAX, "Uninitialized index");
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		Index, gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);

	return rtv;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle(u32 Index)
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(
		gDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		Index, gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]
	);

	return dsv;
}

TextureData& GetBackBuffer(u32 Index)
{
	return gBackBuffers[Index];
}

ID3D12Resource* GetBackBufferResource(u32 Index)
{
	return GetTextureResource(gBackBuffers[Index].ID);
}

void BindDescriptors(ID3D12GraphicsCommandList7* CommandList, u32 SrvOffset)
{
	//ZoneScoped;
	ID3D12DescriptorHeap* DescriptorHeaps[] = {
		gGeneralDescriptorHeap.Get()
	};

	CommandList->SetGraphicsRootSignature(gRootSignature.Get());
	CommandList->SetDescriptorHeaps(ArrayCount(DescriptorHeaps), DescriptorHeaps);

	CommandList->SetGraphicsRootDescriptorTable(0, CD3DX12_GPU_DESCRIPTOR_HANDLE(gGeneralDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), SrvOffset, gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]));
}

void BindRenderTargets(ID3D12GraphicsCommandList7* CommandList, TArrayView<u32> RTVs, u32 DSV)
{
	D3D12_CPU_DESCRIPTOR_HANDLE RTVHandles[8] { 0 };
	for (int i = 0; i < RTVs.size(); ++i)
	{
		RTVHandles[i] = GetRTVHandle(RTVs[i]);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE DSVHandle = GetDSVHandle(DSV);
	D3D12_CPU_DESCRIPTOR_HANDLE* DSVHandlePtr = nullptr;
	if (DSV != -1)
		DSVHandlePtr = &DSVHandle;
	CommandList->OMSetRenderTargets((u32)RTVs.size(), RTVHandles, true, DSVHandlePtr);
}

void ClearRenderTarget(ID3D12GraphicsCommandList7* CommandList, u32 RTV, float* clearColor)
{
	CommandList->ClearRenderTargetView(GetRTVHandle(RTV), clearColor, 0, nullptr);
}

void ClearDepth(ID3D12GraphicsCommandList7* CommandList, u32 DSV, float depthValue)
{
	CommandList->ClearDepthStencilView(GetDSVHandle(DSV), D3D12_CLEAR_FLAG_DEPTH, depthValue, 0, 0, nullptr);
}

TComPtr<IDXGISwapChain2> CreateSwapChain(System::Window& Window, DXGI_SWAP_CHAIN_FLAG Flags)
{
	ZoneScoped;
	TComPtr<IDXGISwapChain2> Result;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = Window.mSize.x;
	swapChainDesc.Height = Window.mSize.y;
	swapChainDesc.Format = BACK_BUFFER_FORMAT;
	swapChainDesc.Scaling = DXGI_SCALING_NONE;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = BACK_BUFFER_COUNT;
	swapChainDesc.Flags = Flags;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	{
		TComPtr<IDXGISwapChain1> Tmp;
		VALIDATE(gDXGIFactory->CreateSwapChainForHwnd(GetGPUQueue(D3D12_COMMAND_LIST_TYPE_DIRECT), Window.mHwnd, &swapChainDesc, nullptr, nullptr, &Tmp));
		Tmp.As(&Result);
		CHECK(Result, "DXGI 1.3 not supported?");
	}

	return Result;
}

void UpdateRenderTargetViews(unsigned Width, unsigned Height)
{
	ZoneScoped;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	u32 DesriptorSize = gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV];

	for (int i = 0; i < BACK_BUFFER_COUNT; ++i)
	{
		TComPtr<ID3D12Resource> backBuffer;
		VALIDATE(gSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		gDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

		String Name = StringFromFormat("Back buffer %d", i);
		gBackBuffers[i].ID = StoreTexture(backBuffer.Get(), Name.c_str());
		gBackBuffers[i].RTV = (u16)i;
		gBackBuffers[i].Width  = (u16)Width;
		gBackBuffers[i].Height = (u16)Height;

		rtvHandle.Offset(DesriptorSize);
	}
}

void Submit(D3D12CmdList& CmdList)
{
	ZoneScopedN("Submit one command list");
	ID3D12CommandList* CommandLists[] = {
		CmdList.Get()
	};
	CmdList->Close();

	GetGPUQueue(CmdList.Type)->ExecuteCommandLists(1, CommandLists);

	//WaitForCompletion(Signal(CmdList.Type));

	DiscardCommandList(CmdList);
}

void Submit(TArray<D3D12CmdList>& CmdLists)
{
	ZoneScopedN("Submit multiple command lists");

	TArray<ID3D12CommandList*> CommandLists;
	CommandLists.reserve(CmdLists.size());
	D3D12_COMMAND_LIST_TYPE Type = CmdLists[0].Type;
	for (auto& CmdList : CmdLists)
	{
		CmdList->Close();
		CommandLists.push_back(CmdList.Get());
		CHECK(CmdList.Type == Type, "Mixed command lists");
	}
	GetGPUQueue(Type)->ExecuteCommandLists((u32)CommandLists.size(), CommandLists.data());
	//WaitForCompletion(Signal(Type));

	for (auto& CmdList : CmdLists)
	{
		DiscardCommandList(CmdList);
	}
}

TicketGPU Signal(D3D12_COMMAND_LIST_TYPE QueueType)
{
	TicketGPU Result;
	Result.QueueType = QueueType;
	Result.Value = gFenceValues[QueueType].fetch_add(1, std::memory_order_relaxed);
	gQueues[QueueType]->Signal(gFences[QueueType].Get(), Result.Value);

	return Result;
}

TicketGPU CurrentFrameTicket()
{
	TicketGPU Result;
	Result.QueueType = D3D12_COMMAND_LIST_TYPE_DIRECT;
	Result.Value = gFenceValues[Result.QueueType].load(std::memory_order_relaxed) + 1;

	return Result;
}

bool WorkIsDone(const TicketGPU& Ticket)
{
	return gFences[Ticket.QueueType]->GetCompletedValue() >= Ticket.Value;
}

void WaitForCompletion(const TicketGPU& Ticket)
{
	if (!WorkIsDone(Ticket))
	{
		HANDLE Event = CreateEvent(0, 0, 0, 0);
		WaitForFenceValue(gFences[Ticket.QueueType].Get(), Ticket.Value, Event);
	}
}

void InsertWait(D3D12_COMMAND_LIST_TYPE QueueType, const TicketGPU& Ticket)
{
	CHECK(Ticket.QueueType != QueueType, "You don't need to wait for the fence on the same queue");

	gQueues[QueueType]->Wait(gFences[Ticket.QueueType].Get(), Ticket.Value);
}

void CreateBackBufferResources(System::Window& Window)
{
	DXGI_SWAP_CHAIN_FLAG SwapChainFlag = (DXGI_SWAP_CHAIN_FLAG)(DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

	if (gDeviceCaps.Tearing)
	{
		SwapChainFlag = (DXGI_SWAP_CHAIN_FLAG)(SwapChainFlag | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
	}

	for (int i = 0; i < BACK_BUFFER_COUNT; ++i)
	{
		FreeTextureResource(gBackBuffers[i].ID);
	}

	if (gSwapChain)
	{
		gSwapChain->ResizeBuffers(
			BACK_BUFFER_COUNT,
			Window.mSize.x, Window.mSize.y,
			BACK_BUFFER_FORMAT,
			SwapChainFlag
		);
	}
	else
	{
		gSwapChain = CreateSwapChain(Window, SwapChainFlag);
	}

	if (SwapChainFlag & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
	{
		VALIDATE(gSwapChain->SetMaximumFrameLatency(2));
		gSwapChainWaitableObject = gSwapChain->GetFrameLatencyWaitableObject();
		CHECK(gSwapChainWaitableObject, "Failed to get waitable object");
	}

	UpdateRenderTargetViews(Window.mSize.x, Window.mSize.y);
}

bool IsSwapChainReady()
{
	return WaitForSingleObjectEx(gSwapChainWaitableObject, 0, true) == WAIT_OBJECT_0;
}

static std::atomic<u16> gCurrentRenderTargetIndex = BACK_BUFFER_COUNT;
void CreateRTV(TextureData& TexData)
{
	u16 Index = gCurrentRenderTargetIndex.fetch_add(1, std::memory_order_relaxed);
	CHECK(Index < RTV_HEAP_SIZE, "Too much RTV descriptors. Need new plan.");

	//ZoneScoped;
	D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
	RTVDesc.Format = (DXGI_FORMAT)TexData.Format;
	RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	CD3DX12_CPU_DESCRIPTOR_HANDLE Handle(
		gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		Index,
		gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]
	);
	gDevice->CreateRenderTargetView(GetTextureResource(TexData.ID), &RTVDesc, Handle);

	TexData.RTV = Index;
}

static std::atomic<u16> gCurrentGeneralIndex = 1;
void CreateSRV(TextureData& TexData, bool ScatterRedChannel)
{
	u16 Index = gCurrentGeneralIndex.fetch_add(1, std::memory_order_relaxed);
	CHECK(Index < GENERAL_HEAP_SIZE, "Too much SRV descriptors. Need new plan.");

	//ZoneScoped;
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = (DXGI_FORMAT)TexData.Format;
	SRVDesc.Texture2D.MipLevels = TexData.NumMips;

	if (ScatterRedChannel)
	{
		SRVDesc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0,0,0,0);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Handle = gGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	Handle.ptr += Index * gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
	gDevice->CreateShaderResourceView(GetTextureResource(TexData.ID), &SRVDesc, Handle);

	TexData.SRV = Index;
}

// returns index in SRV heap


void CreateUAV(Buffer& Buffer)
{
	u16 Index = gCurrentGeneralIndex++;
	CHECK(Index < GENERAL_HEAP_SIZE, "Too much SRV descriptors. Need new plan.");

	//ZoneScoped;
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format = (DXGI_FORMAT)Buffer.Format;
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	D3D12_CPU_DESCRIPTOR_HANDLE Handle = gGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	if (Buffer.UAV != MAXWORD)
	{
		Handle.ptr += Index * gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
	}
	gDevice->CreateUnorderedAccessView(Buffer.Resource, NULL, &UAVDesc, Handle);
}

u16 CreateUAVBatched(TextureData Datas[], u32 NumDescriptors)
{
	u16 Index = gCurrentGeneralIndex.fetch_add(NumDescriptors);
	CHECK(Index + NumDescriptors < GENERAL_HEAP_SIZE, "Too much SRV descriptors. Need new plan.");

	for (int i = 0; i < NumDescriptors; ++i)
	{
		TextureData& TexData = Datas[i];
		//ZoneScoped;
		D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
		UAVDesc.Format = (DXGI_FORMAT)TexData.Format;
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

		D3D12_CPU_DESCRIPTOR_HANDLE Handle = gGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

		if (TexData.UAV != MAXWORD)
		{
			Handle.ptr += (Index + i) * gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
		}
		gDevice->CreateUnorderedAccessView(GetTextureResource(TexData.ID), NULL, &UAVDesc, Handle);

		TexData.UAV = Index + i;
	}
	return Index;
}

void CreateUAV(TextureData& TexData)
{
	u16 Index;
	if (TexData.UAV != MAXWORD)
	{
		Index = TexData.UAV;
	}
	else
	{
		Index = gCurrentGeneralIndex++;
		CHECK(Index < GENERAL_HEAP_SIZE, "Too much SRV descriptors. Need new plan.");
	}

	//ZoneScoped;
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format = (DXGI_FORMAT)TexData.Format;
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE Handle = gGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	if (TexData.UAV != MAXWORD)
	{
		Handle.ptr += Index * gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
	}
	gDevice->CreateUnorderedAccessView(GetTextureResource(TexData.ID), NULL, &UAVDesc, Handle);

	TexData.UAV = Index;
}

static std::atomic<u16> gCurrentDSVIndex = 0;
void CreateDSV(TextureData& TexData)
{
	u16 Index = gCurrentDSVIndex.fetch_add(1, std::memory_order_relaxed);
	CHECK(Index < DSV_HEAP_SIZE, "Too much DSV descriptors. Need new plan.");
	//ZoneScoped;

	D3D12_DEPTH_STENCIL_VIEW_DESC Desc = {};
	Desc.Format = (DXGI_FORMAT)TexData.Format;
	Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(gDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	dsvHandle.ptr += Index * gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_DSV];
	gDevice->CreateDepthStencilView(GetTextureResource(TexData.ID), &Desc, dsvHandle);

	TexData.DSV = Index;
}