#include "Render/Context.h"
#include "Render/Texture.h"
#include "Render/RenderDebug.h"
#include "Render/RenderThread.h"
#include "Render/CommandListPool.h"

#include "Containers/Map.h"
#include "Util/Math.h"
#include "Util/Debug.h"
#include "Util/Util.h"
#include "Threading/Mutex.h"
#include "Threading/MainThread.h"
#include "System/Window.h"
#include "Assets/File.h"

#include <imgui.h>
#include <Tracy.hpp>
#include <dxgi1_6.h>

#include <dxcapi.h>

#include "RenderContextHelpers.h"
#include "Render/TransientResourcesPool.h"
#include <TracyD3D12.hpp>

static const u32 UPLOAD_BUFFERS = 1;
static const u32 RTV_HEAP_SIZE = 4096;
static const u32 DSV_HEAP_SIZE = 32;
static const u32 GENERAL_HEAP_SIZE = 4096;
static const u64 UPLOAD_BUFFER_SIZE = 8_mb;

static const D3D12_HEAP_PROPERTIES DefaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
static const D3D12_HEAP_PROPERTIES UploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
static const D3D12_HEAP_PROPERTIES ReadbackHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
static const D3D12_HEAP_PROPERTIES UmaHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_MEMORY_POOL_L0);

ComPtr<ID3D12DescriptorHeap>    gRTVDescriptorHeap;
ComPtr<ID3D12DescriptorHeap>    gDSVDescriptorHeap;
ComPtr<ID3D12DescriptorHeap>    gGeneralDescriptorHeap;
ComPtr<ID3D12RootSignature>     gRootSignature;
ComPtr<ID3D12RootSignature>     gComputeRootSignature;

ComPtr<ID3D12CommandQueue> gQueues[4];
ComPtr<ID3D12Fence>        gFences[4];
std::atomic<u64>           gFenceValues[4];

ComPtr<IDXGIFactory4>           gDXGIFactory;
ComPtr<ID3D12Device>            gDevice;

//ComPtr<IDxcLibrary>  gDxcLibrary;
ComPtr<IDxcUtils>    gDxcUtils;
ComPtr<IDxcCompiler> gDxcCompiler;
ComPtr<IDxcIncludeHandler> gIncludeHandler;

TracyD3D12Ctx	gCopyProfilingCtx;

D3D12CmdList gUploadCmdList;

UINT gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

TArray<D3D12_RESOURCE_BARRIER> gUploadTransitions;
TArray<PooledBuffer> gUploadBuffers;
TracyLockable(Mutex, gUploadMutex);
//Mutex gUploadMutex;

ComPtr<IDXGISwapChain2> gSwapChain;
TextureData             gBackBuffers[BACK_BUFFER_COUNT] = {};
HANDLE                  gSwapChainWaitableObject = nullptr;

GraphicsDeviceCapabilities gDeviceCaps;

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
	bool CheckTearingSupport(ComPtr<IDXGIFactory4>& dxgiFactory)
	{
		BOOL allowTearing = FALSE;
	 
		ComPtr<IDXGIFactory5> factory5;
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

	ComPtr<IDXGIFactory4> CreateFactory()
	{
		ComPtr<IDXGIFactory4> dxgiFactory;
		UINT FactoryFlags = 0;
//#ifdef DEBUG
#if 1
		FactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
		VALIDATE(CreateDXGIFactory2(FactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

		return dxgiFactory;
	}

	void PrintBlob(IDxcBlobEncoding* ErrorBlob)
	{
		BOOL Known;
		UINT32 CodePage;
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

	void PrintBlob(ID3DBlob* ErrorBlob)
	{
		std::string_view Str((char *)ErrorBlob->GetBufferPointer(), ErrorBlob->GetBufferSize());
		Debug::Print(Str);
	}

	ShaderType GetShaderTypeFromEntryPoint(StringView EntryPoint)
	{
		if (EndsWith(EntryPoint, "VS"))
		{
			return eVertexShader;
		}
		else if (EndsWith(EntryPoint, "PS"))
		{
			return ePixelShader;
		}
		else if (EndsWith(EntryPoint, "CS"))
		{
			return eComputeShader;
		}
		return eShaderTypeCount;
	}

	StringView GetTargetVersionFromType(ShaderType Type)
	{
		static StringView TargetVersions[] = {
			"ps",
			"vs",
			"",
			"",
			"",
			"cs",
		};
		if (Type != eShaderTypeCount)
		{
			return TargetVersions[Type];
		}
		DEBUG_BREAK();
		return "";
	}

	ComPtr<IDxcBlob> CompileShader(StringView FileName, StringView EntryPoint)
	{
		ShaderType Type = GetShaderTypeFromEntryPoint(EntryPoint);
		StringView TargetType = GetTargetVersionFromType(Type);

		String TargetVersion = StringFromFormat(
			"%s_%d_%d",
			TargetType.data(),
			gDeviceCaps.ShaderModelMajor ? 6 : 5,
			gDeviceCaps.ShaderModelMinor
		);
		WString Target = ToWide(TargetVersion);

		String Shader = LoadWholeFile(FileName);

		ComPtr<IDxcBlobEncoding> pSource;
		gDxcUtils->CreateBlob(Shader.data(), (u32)Shader.size(), CP_UTF8, &pSource);

		WString File = ToWide(FileName);
		WString Entry = ToWide(EntryPoint);

		PCWSTR Arguments[] = {
			L"-Zi",
			L"-Qembed_debug",
		};

		ComPtr<IDxcOperationResult> OperationResult;
		HRESULT HR = gDxcCompiler->Compile(
			pSource.Get(),
			File.c_str(), Entry.c_str(), Target.c_str(),
			Arguments, ArrayCount(Arguments),
			nullptr, 0,
			gIncludeHandler.Get(),
			&OperationResult
		);

		if(SUCCEEDED(HR))
			OperationResult->GetStatus(&HR);

		ComPtr<IDxcBlobEncoding> ErrorBlob;
		OperationResult->GetErrorBuffer(&ErrorBlob);
		if (ErrorBlob->GetBufferSize())
		{
			PrintBlob(ErrorBlob.Get());
		}

		if (!SUCCEEDED(HR))
			DEBUG_BREAK();

		ComPtr<IDxcBlob> Result;
		OperationResult->GetResult(&Result);
		return Result;
	}
}

void WaitForFenceValue(ID3D12Fence* Fence, uint64_t FenceValue, HANDLE Event)
{
	ZoneScoped;
	if (Fence->GetCompletedValue() < FenceValue)
	{
		VALIDATE(Fence->SetEventOnCompletion(FenceValue, Event));
		::WaitForSingleObject(Event, MAXDWORD);
		PIXNotifyWakeFromFenceSignal(Event);
	}
}

void FlushQueue(D3D12_COMMAND_LIST_TYPE Type)
{
	ZoneScoped;
	TicketGPU Ticket = Signal(Type);
	WaitForCompletion(Ticket);
}

ComPtr<ID3D12Resource> CreateResource(const D3D12_RESOURCE_DESC* ResourceDescription, const D3D12_HEAP_PROPERTIES* HeapProperties, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* ClearValue)
{
	ZoneScoped;
	ComPtr<ID3D12Resource> Result;

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

ComPtr<ID3D12Resource> CreateBuffer(u64 Size, BufferType Type)
{
	D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(Size);

	const D3D12_HEAP_PROPERTIES* HeapProps = &DefaultHeapProps;

	D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_COMMON;

	if (Type == BUFFER_UPLOAD)
	{
		HeapProps = &UploadHeapProperties;
		InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}
	else if (Type == BUFFER_STAGING)
	{
		HeapProps = &ReadbackHeapProperties;
		InitialState = D3D12_RESOURCE_STATE_COPY_DEST;
	}
	else if (gDeviceCaps.UMA && gDeviceCaps.CacheCoherentUMA)
	{
		HeapProps = &UmaHeapProperties;
	}

	return CreateResource(&BufferDesc, HeapProps, InitialState, nullptr);
}

void SetupDeviceCapabilities(D3D_FEATURE_LEVEL FeatureLevel, ID3D12Device* Device)
{
	gDeviceCaps.FeatureLevel = FeatureLevel;
	gDeviceCaps.Tearing = CheckTearingSupport(gDXGIFactory);

	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS Options{};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof(Options))))
		{
			gDeviceCaps.DoublePrecisionFloatInShaders = Options.DoublePrecisionFloatShaderOps;
			gDeviceCaps.OutputMergerLogicOp = Options.OutputMergerLogicOp;
			gDeviceCaps.MinPrecisionSupport = Options.MinPrecisionSupport;
			gDeviceCaps.TiledResourcesTier = Options.TiledResourcesTier;
			gDeviceCaps.ResourceBindingTier = Options.ResourceBindingTier;
			gDeviceCaps.PSSpecifiedStencilRef = Options.PSSpecifiedStencilRefSupported;
			gDeviceCaps.TypedUAVLoadAdditionalFormats = Options.TypedUAVLoadAdditionalFormats;
			gDeviceCaps.RasterizerOrderefViews = Options.ROVsSupported;
			gDeviceCaps.ConservativeRasterizationTier = Options.ConservativeRasterizationTier;
			gDeviceCaps.GPUVirtualAddressMaxBits = MAX(0, (int)Options.MaxGPUVirtualAddressBitsPerResource - 32);
			gDeviceCaps.ResourceHeapTier = Options.ResourceHeapTier - 1;
		}
	}
	{
		D3D12_FEATURE_DATA_SHADER_MODEL Options{ D3D_SHADER_MODEL_6_4 };
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &Options, sizeof(Options))))
		{
			gDeviceCaps.ShaderModelMajor = Options.HighestShaderModel >= D3D_SHADER_MODEL_6_0;
			gDeviceCaps.ShaderModelMinor = gDeviceCaps.ShaderModelMajor ?
			  Options.HighestShaderModel - 0x60
			: 1;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS1  Options{};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &Options, sizeof(Options))))
		{
			gDeviceCaps.WaveOperations = Options.WaveOps;
			gDeviceCaps.WaveLaneCount = Options.WaveLaneCountMin;
			gDeviceCaps.WaveCountTotal = Options.TotalLaneCount;
			gDeviceCaps.Int64ShaderOperations = Options.Int64ShaderOps;
		}
	}
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE Options { D3D_ROOT_SIGNATURE_VERSION_1_1 };
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &Options, sizeof(Options))))
		{
			gDeviceCaps.RootSignatureVersion = Options.HighestVersion - 1;
		}
	}
	{
		D3D12_FEATURE_DATA_ARCHITECTURE1 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &Options, sizeof(Options))))
		{
			gDeviceCaps.TileBasedRenderer = Options.TileBasedRenderer;
			gDeviceCaps.UMA = Options.UMA;
			gDeviceCaps.CacheCoherentUMA = Options.CacheCoherentUMA;
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

			CHECK(Options.ShadingRateImageTileSize <= 63, "increase bits for ShadingRateImageTileSize");
			gDeviceCaps.ShadingRateImageTileSize = Options.ShadingRateImageTileSize;

			gDeviceCaps.BackgroundProcessing = Options.BackgroundProcessingSupported;
		}
	}
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 Options {};
		if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &Options, sizeof(Options))))
		{
			gDeviceCaps.MeshShader = Options.MeshShaderTier != 0;
			gDeviceCaps.SamplerFeedback = Options.SamplerFeedbackTier != 0;
			gDeviceCaps.SamplerFeedbackTier = Options.SamplerFeedbackTier == 100;
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
}

ComPtr<ID3D12Device> CreateDevice()
{
	ComPtr<ID3D12Device> Result;

	SIZE_T MaxSize = 0;
	ComPtr<IDXGIAdapter1> Adapter;
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
		for (uint32_t Idx = 0; DXGI_ERROR_NOT_FOUND != gDXGIFactory->EnumAdapters1(Idx, &Adapter); ++Idx)
		{
			DXGI_ADAPTER_DESC1 desc;
			Adapter->GetDesc1(&desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			if (desc.DedicatedVideoMemory > MaxSize && SUCCEEDED(D3D12CreateDevice(Adapter.Get(), FeatureLevel, IID_PPV_ARGS(&Result))))
			{
				Adapter->GetDesc1(&desc);
				Debug::Print("D3D12-capable hardware found:", desc.Description, desc.DedicatedVideoMemory >> 20, "MB dedicated video memory");
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
		Debug::Print("Failed to find a hardware adapter.  Falling back to WARP.\n");
		VALIDATE(gDXGIFactory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)));
		VALIDATE(D3D12CreateDevice(Adapter.Get(), FeatureLevel, IID_PPV_ARGS(&Result)));
	}

	SetupDeviceCapabilities(FeatureLevel, Result.Get());
#if !defined(PROFILE) && !defined(RELEASE)
	ComPtr<ID3D12InfoQueue> pInfoQueue;
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

ComPtr<ID3D12CommandQueue> CreateCommandQueue(D3D12_COMMAND_LIST_TYPE Type)
{
	ComPtr<ID3D12CommandQueue> Result;

	D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
	QueueDesc.Type = Type;
	QueueDesc.NodeMask = 1;

	gDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Result));
	return Result;
}

ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32_t NumDescriptors, D3D12_DESCRIPTOR_HEAP_FLAGS Flags)
{
	ZoneScoped;
	ComPtr<ID3D12DescriptorHeap> Result;

	D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
	Desc.NumDescriptors = NumDescriptors;
	Desc.Type = Type;
	Desc.Flags = Flags;

	VALIDATE(gDevice->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Result)));

	return Result;
}

ComPtr<ID3D12RootSignature> CreateRootSignature(ComPtr<ID3DBlob> RootBlob)
{
	ZoneScoped;
	ComPtr<ID3D12RootSignature> Result;
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

void*    gUploadWaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

uint32_t gPresentFlags = 0;
uint32_t gSyncInterval = 2;

void InitRender(System::Window& Window)
{
	ZoneScoped;

#if !defined(RELEASE) && !defined(PROFILE)
	// d3d12 debug layer
	{
		ComPtr<ID3D12Debug3> debugInterface;
		VALIDATE(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
		debugInterface->EnableDebugLayer();
		debugInterface->SetEnableGPUBasedValidation(true);
		debugInterface->SetEnableSynchronizedCommandQueueValidation(true);
	}
#endif
#if !defined(RELEASE) && !defined(PROFILE)
	{
		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
		VALIDATE(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)));

		// Turn on auto-breadcrumbs and page fault reporting.
		pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
	}
#endif

	gDXGIFactory = CreateFactory();
	gDevice = CreateDevice();

	for (int i = 0; i < ArrayCount(gDescriptorSizes); ++i)
	{
		gDescriptorSizes[i] = gDevice->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}
	for (D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_DIRECT; Type <= D3D12_COMMAND_LIST_TYPE_COPY; Type = (D3D12_COMMAND_LIST_TYPE)(Type + 1))
	{
		if (Type != D3D12_COMMAND_LIST_TYPE_BUNDLE)
		{
			gQueues[Type] = CreateCommandQueue(Type);
			gFences[Type] = CreateFence(0);
		}
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
		CD3DX12_ROOT_PARAMETER Params[2] = {};

		CD3DX12_DESCRIPTOR_RANGE Range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		Params[0].InitAsDescriptorTable(1, &Range);
		Params[1].InitAsConstants(16, 0);

		CD3DX12_STATIC_SAMPLER_DESC Samplers[2] = {};
		Samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
		Samplers[1].Init(1, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT);

		CD3DX12_ROOT_SIGNATURE_DESC DescRootSignature;

		DescRootSignature.Init(
			ArrayCount(Params), Params,
			ArrayCount(Samplers), Samplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);

		ComPtr<ID3DBlob> RootBlob;
		{
			ComPtr<ID3DBlob> ErrorBlob;
			HRESULT HR = D3D12SerializeRootSignature(
				&DescRootSignature,
				D3D_ROOT_SIGNATURE_VERSION_1,
				&RootBlob,
				&ErrorBlob);
			if (!SUCCEEDED(HR))
			{
				PrintBlob(ErrorBlob.Get());
				DEBUG_BREAK();
			}
		}

		gRootSignature = CreateRootSignature(RootBlob);
	}

	// compute
	//if (false)
	{
		CD3DX12_ROOT_PARAMETER Params[4] = {};

		{
			CD3DX12_DESCRIPTOR_RANGE Range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
			Params[0].InitAsDescriptorTable(1, &Range);
		}
		Params[1].InitAsUnorderedAccessView(0);
		{
			CD3DX12_DESCRIPTOR_RANGE Range(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 12, 1);
			Params[2].InitAsDescriptorTable(1, &Range);
		}
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

		ComPtr<ID3DBlob> RootBlob;
		{
			ComPtr<ID3DBlob> ErrorBlob;
			HRESULT HR = D3D12SerializeRootSignature(
				&DescRootSignature,
				D3D_ROOT_SIGNATURE_VERSION_1,
				&RootBlob,
				&ErrorBlob);
			if (!SUCCEEDED(HR))
			{
				PrintBlob(ErrorBlob.Get());
				DEBUG_BREAK();
			}
		}

		gComputeRootSignature = CreateRootSignature(RootBlob);
	}
	gUploadCmdList = GetCommandList(D3D12_COMMAND_LIST_TYPE_COPY, L"Upload cmd list");

	gCopyProfilingCtx = TracyD3D12Context(gDevice.Get(), GetGPUQueue(D3D12_COMMAND_LIST_TYPE_COPY));

	TracyD3D12ContextName(gCopyProfilingCtx, "Copy", 4);
	TracyD3D12NewFrame(gCopyProfilingCtx);

	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(gDxcUtils.GetAddressOf()));
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(gDxcCompiler.GetAddressOf()));
	gDxcUtils->CreateDefaultIncludeHandler(gIncludeHandler.GetAddressOf());
}

void UploadTextureData(TextureData& TexData, const uint8_t *RawData, u32 RawDataSize)
{
	//ZoneScoped;

	D3D12_SUBRESOURCE_DATA SrcData = {};
	SrcData.pData = RawData;

	if (!IsBlockCompressedFormat((DXGI_FORMAT)TexData.Format))
	{
		UINT Components = ComponentCountFromFormat((DXGI_FORMAT)TexData.Format);
		SrcData.RowPitch = TexData.Width * Components;
		SrcData.SlicePitch = TexData.Width * TexData.Height * Components;
	}
	else
	{
		UINT BlockSize = BlockSizeFromFormat((DXGI_FORMAT)TexData.Format);
		SrcData.RowPitch = MAX(1, ((TexData.Width + 3) / 4)) * BlockSize;
		SrcData.SlicePitch = RawDataSize;
	}

	ID3D12Resource* Resource = GetTextureResource(TexData.ID);
	D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		Resource,
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);

	if (gDeviceCaps.UMA && gDeviceCaps.CacheCoherentUMA)
	{
		Resource->Map(0, nullptr, nullptr);
		Resource->WriteToSubresource(0, nullptr, RawData, (UINT)SrcData.RowPitch, RawDataSize);
		Resource->Unmap(0, nullptr);

		ScopedLock Lock(gUploadMutex);
		gUploadTransitions.push_back(Barrier);
		return;
	}
	PooledBuffer TextureUploadBuffer;

	uint64_t UploadBufferSize = GetRequiredIntermediateSize(Resource, 0, 1);
	GetTransientBuffer(TextureUploadBuffer, UploadBufferSize, BUFFER_UPLOAD);

	ScopedLock Lock(gUploadMutex);
	TracyD3D12Zone(gCopyProfilingCtx, gUploadCmdList.Get(), "Upload texture data");
	gUploadBuffers.push_back(TextureUploadBuffer);
	UpdateSubresources<1>(gUploadCmdList.Get(), Resource, TextureUploadBuffer.Get(), TextureUploadBuffer.Offset, 0, 1, &SrcData);
	gUploadTransitions.push_back(Barrier);
}

ComPtr<ID3D12Fence> CreateFence(uint64_t InitialValue, D3D12_FENCE_FLAGS Flags)
{
	ZoneScoped;
	ComPtr<ID3D12Fence> Result;

	VALIDATE(gDevice->CreateFence(InitialValue, Flags, IID_PPV_ARGS(&Result)));
	return Result;
}

void FlushUpload()
{
	ZoneScoped;
	ScopedLock Lock(gUploadMutex);
	if (gUploadTransitions.empty())
	{
		return;
	}

	Submit(gUploadCmdList);
	TicketGPU UploadDone = Signal(gUploadCmdList.Type);
	gUploadCmdList = GetCommandList(D3D12_COMMAND_LIST_TYPE_COPY, L"Upload cmdlist");

	EnqueueDelayedWork(
		[
			Transitions = MOVE(gUploadTransitions),
			Buffers = MOVE(gUploadBuffers)
		]() mutable {
		auto TransitionsCommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Resource transitions");
		TransitionsCommandList->ResourceBarrier((UINT)Transitions.size(), Transitions.data());
		Submit(TransitionsCommandList);
		for (auto& Buffer : Buffers)
		{
			DiscardTransientBuffer(Buffer);
		}
	}, UploadDone);
}

void UploadBufferData(ID3D12Resource* Destination, u64 Size, D3D12_RESOURCE_STATES TargetState, TFunction<void(void*, u64)> UploadFunction)
{
	D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		Destination,
		D3D12_RESOURCE_STATE_COMMON,
		TargetState
	);

	if (gDeviceCaps.UMA && gDeviceCaps.CacheCoherentUMA)
	{
		void* Address = nullptr;
		Destination->Map(0, nullptr, &Address);
		UploadFunction(Address, Size);
		Destination->Unmap(0, nullptr);

		ScopedLock Lock(gUploadMutex);
		gUploadTransitions.push_back(Barrier);
		return;
	}
	PooledBuffer UploadBuffer;
	GetTransientBuffer(UploadBuffer, Size, BUFFER_UPLOAD);
	u8* UploadBufferAddress = NULL;
	UploadBuffer->Map(0, nullptr, (void**)&UploadBufferAddress);
	UploadFunction(UploadBufferAddress + UploadBuffer.Offset, Size);
	UploadBuffer->Unmap(0, nullptr);
	{
		ScopedLock Lock(gUploadMutex);
		gUploadCmdList->CopyBufferRegion(Destination, 0, UploadBuffer.Get(), UploadBuffer.Offset, Size);
		gUploadTransitions.push_back(Barrier);
	}
}

void UploadBufferData(ID3D12Resource* Destination, const void* Data, uint64_t Size, D3D12_RESOURCE_STATES TargetState)
{
	UploadBufferData(Destination, Size, TargetState,
		[Data](void *GPUAddress, u64 Size) {
			memcpy(GPUAddress, Data, Size);
		}
	);
}

//TracyLockable(Mutex, gPresentLock);
void PresentCurrentBackBuffer()
{
	ZoneScoped;

	PIXScopedEvent(GetGPUQueue(), __LINE__, "Present");

	//ScopedLock Lock(gPresentLock);
	VALIDATE(gSwapChain->Present(gSyncInterval, gPresentFlags));

	ScopedLock Lock(gUploadMutex);
	TracyD3D12Collect(gCopyProfilingCtx);
	TracyD3D12NewFrame(gCopyProfilingCtx);
}

ComPtr<ID3D12PipelineState> CreatePSO(D3D12_COMPUTE_PIPELINE_STATE_DESC* PSODesc)
{
	ZoneScoped;
	ComPtr<ID3D12PipelineState> Result;
	VALIDATE(gDevice->CreateComputePipelineState(PSODesc, IID_PPV_ARGS(&Result)));
	return Result;
}

ComPtr<ID3D12PipelineState> CreatePSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* PSODesc)
{
	ZoneScoped;
	ComPtr<ID3D12PipelineState> Result;

	VALIDATE(gDevice->CreateGraphicsPipelineState(PSODesc, IID_PPV_ARGS(&Result)));
	return Result;
}

Shader CreateShaderCombinationGraphics(
	TArrayView<D3D12_INPUT_ELEMENT_DESC> PSOLayout,
	TArrayView<StringView> EntryPoints,
	StringView ShaderFile,
	TArrayView<DXGI_FORMAT> RenderTargetFormats,
	D3D12_CULL_MODE CullMode,
	DXGI_FORMAT DepthTargetFormat,
	TArrayView<D3D12_RENDER_TARGET_BLEND_DESC> BlendDescs
)
{
	TArray<ComPtr<IDxcBlob>> Shaders;
	
	UINT ShaderCounts[eShaderTypeCount] = {0};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
	for (StringView Entry : EntryPoints)
	{
		ShaderType Type = GetShaderTypeFromEntryPoint(Entry);
		ComPtr<IDxcBlob> ShaderBlob = CompileShader(ShaderFile, Entry);

		/*
		LPVOID Data = ShaderBlob->GetBufferPointer();
		SIZE_T Size = ShaderBlob->GetBufferSize();

		ComPtr<ID3D12ShaderReflection> Reflection;
		D3DReflect(Data, Size, IID_PPV_ARGS(&Reflection));

		D3D12_SHADER_DESC Desc;
		{
			Reflection->GetDesc(&Desc);
			D3D12_SHADER_VERSION_TYPE ShaderType = (D3D12_SHADER_VERSION_TYPE)D3D12_SHVER_GET_TYPE(Desc.Version);
			CHECK(Type == ShaderType, "Parsing error?");
		}

		for (UINT i = 0; i < Desc.ConstantBuffers; ++i)
		{
			ID3D12ShaderReflectionConstantBuffer *ConstantBufferReflection = Reflection->GetConstantBufferByIndex(i);

			D3D12_SHADER_BUFFER_DESC ConstantBufferDesc;
			ConstantBufferReflection->GetDesc(&ConstantBufferDesc);
			for (UINT j = 0; j < Desc.ConstantBuffers; ++j)
			{
				ID3D12ShaderReflectionVariable* VariableReflection = ConstantBufferReflection->GetVariableByIndex(j);
				D3D12_SHADER_VARIABLE_DESC VariableDesc;
				VariableReflection->GetDesc(&VariableDesc);
				continue;
			}
			continue;
		}

		for (UINT i = 0; i < Desc.BoundResources; ++i)
		{
			D3D12_SHADER_INPUT_BIND_DESC BindDesc;
			Reflection->GetResourceBindingDesc(i, &BindDesc);
			continue;
		}

		for (UINT i = 0; i < Desc.InputParameters; ++i)
		{
			D3D12_SIGNATURE_PARAMETER_DESC InputDesc;
			Reflection->GetInputParameterDesc(i, &InputDesc);
			continue;
		}

		for (UINT i = 0; i < Desc.OutputParameters; ++i)
		{
			D3D12_SIGNATURE_PARAMETER_DESC OutputDesc;
			Reflection->GetOutputParameterDesc(i, &OutputDesc);
			continue;
		}
		*/

		Shaders.push_back(ShaderBlob);

		switch (Type)
		{
			case eVertexShader:
				PSODesc.VS.BytecodeLength = ShaderBlob->GetBufferSize();
				PSODesc.VS.pShaderBytecode = ShaderBlob->GetBufferPointer();
				break;
			case ePixelShader:
				PSODesc.PS.BytecodeLength = ShaderBlob->GetBufferSize();
				PSODesc.PS.pShaderBytecode = ShaderBlob->GetBufferPointer();
				break;
			default:
				DEBUG_BREAK();
		}
		CHECK(++ShaderCounts[Type] == 1, "Multiple occurances of the same shader type");
	}

	PSODesc.NumRenderTargets = (UINT)RenderTargetFormats.size();

	CHECK(PSODesc.NumRenderTargets <= 8, "D3D12 Does not support more then 8 render targets.");
	for (UINT i = 0; i < PSODesc.NumRenderTargets; ++i)
	{
		PSODesc.RTVFormats[i] = RenderTargetFormats[i];
	}
	if (DepthTargetFormat != DXGI_FORMAT_UNKNOWN)
	{
		PSODesc.DSVFormat = DepthTargetFormat;
		PSODesc.DepthStencilState.DepthEnable = true;
		PSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		PSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
	}

	PSODesc.pRootSignature = gRootSignature.Get();
	PSODesc.InputLayout.NumElements = (UINT)PSOLayout.size();
	PSODesc.InputLayout.pInputElementDescs = PSOLayout.data();
	PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	PSODesc.RasterizerState.CullMode = CullMode;
	PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	PSODesc.SampleDesc.Count = 1;
	PSODesc.SampleMask = 0xFFFFFFFF;
	PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	for (int i = 0; i < BlendDescs.size(); ++i)
	{
		PSODesc.BlendState.RenderTarget[i] = BlendDescs[i];
	}

	Shader Result;
	Result.PSO = CreatePSO(&PSODesc);
	Result.RootSignature = PSODesc.pRootSignature;
	return Result;
}

Shader CreateShaderCombinationCompute(
	StringView EntryPoint,
	StringView ShaderFile
)
{
	ShaderType Type = GetShaderTypeFromEntryPoint(EntryPoint);
	CHECK(Type == ShaderType::eComputeShader, "?");

	ComPtr<IDxcBlob> ShaderBlob = CompileShader(ShaderFile, EntryPoint);

	/*
	LPVOID Data = ShaderBlob->GetBufferPointer();
	SIZE_T Size = ShaderBlob->GetBufferSize();

	ComPtr<ID3D12ShaderReflection> Reflection;
	D3DReflect(Data, Size, IID_PPV_ARGS(&Reflection));

	D3D12_SHADER_DESC Desc;
	{
		Reflection->GetDesc(&Desc);
		D3D12_SHADER_VERSION_TYPE ShaderType = (D3D12_SHADER_VERSION_TYPE)D3D12_SHVER_GET_TYPE(Desc.Version);
		CHECK(Type == ShaderType, "Parsing error?");
	}

	for (UINT i = 0; i < Desc.ConstantBuffers; ++i)
	{
		ID3D12ShaderReflectionConstantBuffer *ConstantBufferReflection = Reflection->GetConstantBufferByIndex(i);

		D3D12_SHADER_BUFFER_DESC ConstantBufferDesc;
		ConstantBufferReflection->GetDesc(&ConstantBufferDesc);
		for (UINT j = 0; j < Desc.ConstantBuffers; ++j)
		{
			ID3D12ShaderReflectionVariable* VariableReflection = ConstantBufferReflection->GetVariableByIndex(j);
			D3D12_SHADER_VARIABLE_DESC VariableDesc;
			VariableReflection->GetDesc(&VariableDesc);
			continue;
		}
		continue;
	}

	for (UINT i = 0; i < Desc.BoundResources; ++i)
	{
		D3D12_SHADER_INPUT_BIND_DESC BindDesc;
		Reflection->GetResourceBindingDesc(i, &BindDesc);
		continue;
	}

	for (UINT i = 0; i < Desc.InputParameters; ++i)
	{
		D3D12_SIGNATURE_PARAMETER_DESC InputDesc;
		Reflection->GetInputParameterDesc(i, &InputDesc);
		continue;
	}

	for (UINT i = 0; i < Desc.OutputParameters; ++i)
	{
		D3D12_SIGNATURE_PARAMETER_DESC OutputDesc;
		Reflection->GetOutputParameterDesc(i, &OutputDesc);
		continue;
	}
	*/

	D3D12_COMPUTE_PIPELINE_STATE_DESC PSODesc = {};
	PSODesc.CS.BytecodeLength = ShaderBlob->GetBufferSize();
	PSODesc.CS.pShaderBytecode = ShaderBlob->GetBufferPointer();
	PSODesc.pRootSignature = gComputeRootSignature.Get();

	Shader Result;
	Result.PSO = CreatePSO(&PSODesc);
	Result.RootSignature = PSODesc.pRootSignature;
	return Result;
}

ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE Type)
{
	ZoneScoped;
	ComPtr<ID3D12CommandAllocator> Result;

	VALIDATE(gDevice->CreateCommandAllocator(Type, IID_PPV_ARGS(&Result)));
	return Result;
}

ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ID3D12CommandAllocator* CommandAllocator, D3D12_COMMAND_LIST_TYPE Type)
{
	ZoneScoped;
	ComPtr<ID3D12GraphicsCommandList> Result;

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
		1, 1,
		1, 0,
		Flags
	);

	const D3D12_HEAP_PROPERTIES* HeapProps = &DefaultHeapProps;
	if (gDeviceCaps.UMA && gDeviceCaps.CacheCoherentUMA)
	{
		HeapProps = &UmaHeapProperties;
	}
	ComPtr<ID3D12Resource> Resource = CreateResource(&TextureDesc, HeapProps, InitialState, ClearValue);
	TexData.ID = StoreTexture(Resource.Get());
	TexData.Flags = (u8)Flags;
}

u64 GetGeneralHandleGPU(UINT Index)
{
	CHECK(Index != UINT_MAX, "Uninitialized index");
	CD3DX12_GPU_DESCRIPTOR_HANDLE rtv(gGeneralDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		Index, gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);

	return rtv.ptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle(uint32_t Index)
{
	CHECK(Index != UINT_MAX, "Uninitialized index");
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		Index, gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);

	return rtv;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle(uint32_t Index)
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(
		gDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		Index, gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]
	);

	return dsv;
}

TextureData& GetBackBuffer(uint32_t Index)
{
	return gBackBuffers[Index];
}

ID3D12Resource* GetBackBufferResource(UINT Index)
{
	return GetTextureResource(gBackBuffers[Index].ID);
}

void BindDescriptors(ID3D12GraphicsCommandList* CommandList, TextureData& Tex)
{
	//ZoneScoped;
	ID3D12DescriptorHeap* DescriptorHeaps[] = {
		gGeneralDescriptorHeap.Get()
	};

	CommandList->SetGraphicsRootSignature(gRootSignature.Get());
	CommandList->SetDescriptorHeaps(ArrayCount(DescriptorHeaps), DescriptorHeaps);

	D3D12_GPU_DESCRIPTOR_HANDLE Descriptor;
	Descriptor.ptr = GetGeneralHandleGPU(Tex.SRV);
	CommandList->SetGraphicsRootDescriptorTable(
		0,
		Descriptor
	);
}

void BindRenderTargets(ID3D12GraphicsCommandList* CommandList, TArrayView<uint32_t> RTVs, uint32_t DSV)
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
	CommandList->OMSetRenderTargets((UINT)RTVs.size(), RTVHandles, true, DSVHandlePtr);
}

void ClearRenderTarget(ID3D12GraphicsCommandList* CommandList, uint32_t RTV, float* clearColor)
{
	CommandList->ClearRenderTargetView(GetRTVHandle(RTV), clearColor, 0, nullptr);
}

void ClearDepth(ID3D12GraphicsCommandList* CommandList, uint32_t DSV, float depthValue)
{
	CommandList->ClearDepthStencilView(GetDSVHandle(DSV), D3D12_CLEAR_FLAG_DEPTH, depthValue, 0, 0, nullptr);
}

ComPtr<IDXGISwapChain2> CreateSwapChain(System::Window& Window, DXGI_SWAP_CHAIN_FLAG Flags)
{
	ZoneScoped;
	ComPtr<IDXGISwapChain2> Result;

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
		ComPtr<IDXGISwapChain1> Tmp;
		VALIDATE(gDXGIFactory->CreateSwapChainForHwnd(GetGPUQueue(), Window.mHwnd, &swapChainDesc, nullptr, nullptr, &Tmp));
		Tmp.As(&Result);
		CHECK(Result, "DXGI 1.3 not supported?");
	}

	return Result;
}

void UpdateRenderTargetViews(unsigned Width, unsigned Height)
{
	ZoneScoped;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
 
	for (int i = 0; i < BACK_BUFFER_COUNT; ++i)
	{
		ComPtr<ID3D12Resource> backBuffer;
		VALIDATE(gSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		gDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

		String Name = StringFromFormat("Back buffer %d", i);
		gBackBuffers[i].ID = StoreTexture(backBuffer.Get(), Name.c_str());
		gBackBuffers[i].RTV = (u16)i;
		gBackBuffers[i].Width  = (u16)Width;
		gBackBuffers[i].Height = (u16)Height;

		rtvHandle.Offset(gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);
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
	DiscardCommandList(CmdList);
}

void Submit(TArray<D3D12CmdList>& CmdLists)
{
	ZoneScopedN("Submit multiple command lists");

	TArray<ID3D12CommandList*> CommandLists;
	D3D12_COMMAND_LIST_TYPE Type = CmdLists[0].Type;
	for (auto& CmdList : CmdLists)
	{
		CmdList->Close();
		CommandLists.push_back(CmdList.Get());
		CHECK(CmdList.Type == Type, "Mixed command lists");
	}
	GetGPUQueue(Type)->ExecuteCommandLists((u32)CommandLists.size(), CommandLists.data());

	for (auto& CmdList : CmdLists)
	{
		DiscardCommandList(CmdList);
	}
}

TicketGPU Signal(D3D12_COMMAND_LIST_TYPE Type)
{
	TicketGPU Result;
	Result.Type = Type;
	Result.Value = ++gFenceValues[Type];
	gQueues[Type]->Signal(gFences[Type].Get(), Result.Value);

	return Result;
}

TicketGPU CurrentFrameTicket()
{
	TicketGPU Result;
	Result.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	Result.Value = gFenceValues[Result.Type] + 1;

	return Result;
}

bool WorkIsDone(TicketGPU& Ticket)
{
	return gFences[Ticket.Type]->GetCompletedValue() >= Ticket.Value;
}

void WaitForCompletion(TicketGPU& Ticket)
{
	if (!WorkIsDone(Ticket))
	{
		HANDLE Event = CreateEvent(0, 0, 0, 0);
		WaitForFenceValue(gFences[Ticket.Type].Get(), Ticket.Value, Event);
	}
}

void InsertWait(D3D12_COMMAND_LIST_TYPE Type, TicketGPU& Ticket)
{
	CHECK(Ticket.Type != Type, "You don't need to wait for the fence on the same queue");

	gQueues[Type]->Wait(gFences[Ticket.Type].Get(), Ticket.Value);
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

	//gSwapChain->SetFullscreenState(TRUE, NULL);
	UpdateRenderTargetViews(Window.mSize.x, Window.mSize.y);
}

bool IsSwapChainReady()
{
	return WaitForSingleObjectEx(gSwapChainWaitableObject, 0, true) == WAIT_OBJECT_0;
}

std::atomic<u16> gCurrentRenderTargetIndex = BACK_BUFFER_COUNT;
void CreateRTV(TextureData& TexData)
{
	u16 Index = gCurrentRenderTargetIndex++;
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

std::atomic<u16> gCurrentGeneralIndex = 1;
void CreateSRV(TextureData& TexData)
{
	u16 Index = gCurrentGeneralIndex++;
	CHECK(Index < GENERAL_HEAP_SIZE, "Too much SRV descriptors. Need new plan.");

	//ZoneScoped;
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = (DXGI_FORMAT)TexData.Format;
	SRVDesc.Texture2D.MipLevels = 1;

	D3D12_CPU_DESCRIPTOR_HANDLE Handle = gGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	Handle.ptr += Index * gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
	gDevice->CreateShaderResourceView(GetTextureResource(TexData.ID), &SRVDesc, Handle);

	TexData.SRV = Index;
}

// returns index in SRV heap

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

std::atomic<u16> gCurrentDSVIndex = 0;
void CreateDSV(TextureData& TexData)
{
	u16 Index = gCurrentDSVIndex++;
	CHECK(Index < DSV_HEAP_SIZE, "Too much DSV descriptors. Need new plan.");
	//ZoneScoped;

	D3D12_DEPTH_STENCIL_VIEW_DESC Desc = {};
	Desc.Format = (DXGI_FORMAT)TexData.Format;
	Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(gDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	gDevice->CreateDepthStencilView(GetTextureResource(TexData.ID), &Desc, dsvHandle);

	TexData.DSV = Index;
}
