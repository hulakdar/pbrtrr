#include "Render/Context.h"
#include "Render/Texture.h"
#include "Render/RenderDebug.h"
#include "Render/CommandListPool.h"

#include "Containers/Map.h"
#include "Util/Math.h"
#include "Util/Debug.h"
#include "Util/Util.h"
#include "external/d3dx12.h"
#include "Threading/Mutex.h"
#include "System/Window.h"

#include <imgui.h>
#include <Tracy.hpp>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <TracyD3D12.hpp>

#include "RenderContextHelpers.h"

static const UINT   UPLOAD_BUFFERS = 1;
static const UINT   RTV_HEAP_SIZE = 32;
static const UINT   DSV_HEAP_SIZE = 32;
static const UINT   GENERAL_HEAP_SIZE = 4096;
static const size_t UPLOAD_BUFFER_SIZE = 8_mb;

static const D3D12_HEAP_PROPERTIES DefaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
static const D3D12_HEAP_PROPERTIES UploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
static const D3D12_HEAP_PROPERTIES ReadbackHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);

TArray<ComPtr<ID3D12Resource>>	gUploadBuffers[UPLOAD_BUFFERS];
ComPtr<ID3D12CommandAllocator>	gUploadCommandAllocators[UPLOAD_BUFFERS];
ComPtr<ID3D12DescriptorHeap>    gRTVDescriptorHeap;
ComPtr<ID3D12DescriptorHeap>    gDSVDescriptorHeap;
ComPtr<ID3D12DescriptorHeap>    gGeneralDescriptorHeap;
ComPtr<ID3D12RootSignature>     gRootSignature;
ComPtr<ID3D12RootSignature>     gComputeRootSignature;
ComPtr<ID3D12CommandQueue>      gGraphicsQueue;
ComPtr<ID3D12CommandQueue>      gComputeQueue;
ComPtr<ID3D12CommandQueue>      gCopyQueue;
ComPtr<IDXGISwapChain2>         gSwapChain;
ComPtr<IDXGIFactory4>           gDXGIFactory;
ComPtr<ID3D12Device>            gDevice;
ComPtr<ID3D12Fence>             gUploadFences[UPLOAD_BUFFERS];
uint64_t                        gUploadFenceValues[UPLOAD_BUFFERS] = {};

HANDLE	gSwapChainWaitableObject = nullptr;

UINT gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

ComPtr<ID3D12GraphicsCommandList>	gUploadCommandList;
TArray<D3D12_RESOURCE_BARRIER>		gUploadTransitions;

TracyLockable(Mutex, gUploadMutex);

TextureData gBackBuffers[BACK_BUFFER_COUNT] = {};

uint64_t				mCurrentUploadFenceValue = 0;

ID3D12Device* GetGraphicsDevice()
{
	return gDevice.Get();
}

ID3D12CommandQueue* GetGraphicsQueue()
{
	return gGraphicsQueue.Get();
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
#ifdef DEBUG
		FactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
		VALIDATE(CreateDXGIFactory2(FactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

		return dxgiFactory;
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
		return eShaderTypeCount;
	}

	StringView GetTargetVersionFromType(ShaderType Type)
	{
		static StringView TargetVersions[] = {
			"ps_5_1",
			"vs_5_1",
			"",
			"",
			"",
			"",
			"cs_5_1",
		};
		if (Type != eShaderTypeCount)
		{
			return TargetVersions[Type];
		}
		DEBUG_BREAK();
		return "";
	}

	ComPtr<ID3DBlob> CompileShader(StringView FileName, StringView EntryPoint)
	{
		ComPtr<ID3DBlob> Result;

		ShaderType Type = GetShaderTypeFromEntryPoint(EntryPoint);
		StringView TargetVersion = GetTargetVersionFromType(Type);

		ComPtr<ID3DBlob> ErrorBlob;
		StringView Shader = LoadWholeFile(FileName);
		HRESULT HR = D3DCompile(
			Shader.data(), Shader.size(),
			FileName.data(),
			nullptr, nullptr,
			EntryPoint.data(), TargetVersion.data(),
			D3DCOMPILE_DEBUG, 0,
			&Result,
			&ErrorBlob
		);

		if (!SUCCEEDED(HR))
		{
			PrintBlob(ErrorBlob.Get());
			DEBUG_BREAK();
		}
		else if (ErrorBlob)
		{
			PrintBlob(ErrorBlob.Get());
		}

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

uint64_t Signal(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, uint64_t& FenceValue)
{
	uint64_t FenceValueForSignal = ++FenceValue;
	VALIDATE(CommandQueue->Signal(Fence, FenceValueForSignal));

	return FenceValueForSignal;
}

void FlushQueue(ID3D12CommandQueue* CommandQueue, ID3D12Fence* Fence, uint64_t& FenceValue, HANDLE FenceEvent)
{
	ZoneScoped;
	uint64_t fenceValueForSignal = Signal(CommandQueue, Fence, FenceValue);
	WaitForFenceValue(Fence, fenceValueForSignal, FenceEvent);
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

ComPtr<ID3D12Resource> CreateBuffer(uint64_t Size, bool bUploadBuffer, bool bStagingBuffer)
{
	D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(Size);

	const D3D12_HEAP_PROPERTIES* HeapProps = &DefaultHeapProps;

	D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_COPY_DEST;

	if (bUploadBuffer)
	{
		HeapProps = &UploadHeapProperties;
		InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	if (bStagingBuffer)
	{
		HeapProps = &ReadbackHeapProperties;
		InitialState = D3D12_RESOURCE_STATE_COPY_DEST;
	}

	return CreateResource(&BufferDesc, HeapProps, InitialState, nullptr);
}

ComPtr<ID3D12Device> CreateDevice()
{
	ComPtr<ID3D12Device> Result;

	SIZE_T MaxSize = 0;
	ComPtr<IDXGIAdapter1> Adapter;
#ifndef TEST_WARP
	for (uint32_t Idx = 0; DXGI_ERROR_NOT_FOUND != gDXGIFactory->EnumAdapters1(Idx, &Adapter); ++Idx)
	{
		DXGI_ADAPTER_DESC1 desc;
		Adapter->GetDesc1(&desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		if (desc.DedicatedVideoMemory > MaxSize && SUCCEEDED(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Result))))
		{
			Adapter->GetDesc1(&desc);
			Debug::Print("D3D12-capable hardware found:", desc.Description, desc.DedicatedVideoMemory >> 20);
			MaxSize = desc.DedicatedVideoMemory;
		}
	}
#endif

	if (Result.Get() == nullptr)
	{
		Debug::Print("Failed to find a hardware adapter.  Falling back to WARP.\n");
		VALIDATE(gDXGIFactory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)));
		VALIDATE(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Result)));
	}

//#ifdef _DEBUG
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
			D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
			D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
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
//#endif

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
uint8_t* gUploadBufferAddress;
uint64_t gUploadBufferOffset;
void InitUploadResources()
{
	ZoneScoped;

	for (int i = 0; i < UPLOAD_BUFFERS; ++i)
	{
		gUploadCommandAllocators[i] = CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT);
		VALIDATE(gUploadCommandAllocators[i]->Reset());
		gUploadBuffers[i].push_back(CreateBuffer(UPLOAD_BUFFER_SIZE, true));
		gUploadFences[i] = CreateFence();
	}

	gUploadCommandList = CreateCommandList(gUploadCommandAllocators[0].Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
	VALIDATE(gUploadCommandList->Reset(gUploadCommandAllocators[0].Get(), nullptr));

	VALIDATE(gUploadBuffers[0][0]->Map(0, nullptr, (void**)&gUploadBufferAddress));
}

uint32_t gPresentFlags = 0;
uint32_t gSyncInterval = 1;
bool gTearingSupported = false;
void InitRender(System::Window& Window)
{
	ZoneScoped;
	gDXGIFactory = CreateFactory();
	gDevice = CreateDevice();

	for (int i = 0; i < ArrayCount(gDescriptorSizes); ++i)
	{
		gDescriptorSizes[i] = gDevice->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}
	gGraphicsQueue = CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
	gComputeQueue = CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
	gCopyQueue = CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);

	gGeneralDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GENERAL_HEAP_SIZE, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
	gRTVDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_HEAP_SIZE, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
	gDSVDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
	CreateBackBufferResources(Window);

	if (gTearingSupported)
	{
		gPresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
		gSyncInterval = 0;
	}

	InitUploadResources();

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
	if (false)
	{
		CD3DX12_ROOT_PARAMETER Params[3] = {};

		{
			CD3DX12_DESCRIPTOR_RANGE Range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
			Params[0].InitAsDescriptorTable(1, &Range);
			Params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		}
		{
			CD3DX12_DESCRIPTOR_RANGE Range(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
			Params[1].InitAsDescriptorTable(1, &Range);
			Params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		}
		Params[2].InitAsConstants(16, 0);
		Params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		CD3DX12_STATIC_SAMPLER_DESC Samplers[2] = {};
		Samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
		Samplers[1].Init(1, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT);
		Samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		Samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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
} 

UINT gCurrentUploadBufferIndex = 0;
void UploadTextureData(TextureData& TexData, const uint8_t *RawData, uint32_t RawDataSize)
{
	ZoneScoped;

	uint64_t UploadBufferSize = GetRequiredIntermediateSize(GetTextureResource(TexData.ID), 0, 1);
	ComPtr<ID3D12Resource> TextureUploadBuffer = CreateBuffer(UploadBufferSize, true);

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

	D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		GetTextureResource(TexData.ID),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);

	ScopedLock Lock(gUploadMutex);
	gUploadBuffers[gCurrentUploadBufferIndex].push_back(TextureUploadBuffer);
	UpdateSubresources<1>(gUploadCommandList.Get(), GetTextureResource(TexData.ID), TextureUploadBuffer.Get(), 0, 0, 1, &SrcData);
	gUploadTransitions.push_back(Barrier);
}

ComPtr<ID3D12Fence> CreateFence(uint64_t InitialValue, D3D12_FENCE_FLAGS Flags)
{
	ZoneScoped;
	ComPtr<ID3D12Fence> Result;

	VALIDATE(gDevice->CreateFence(InitialValue, Flags, IID_PPV_ARGS(&Result)));
	return Result;
}

void WaitForUploadFinish()
{
	// wait if needed
	WaitForFenceValue(
		gUploadFences[gCurrentUploadBufferIndex].Get(),
		gUploadFenceValues[gCurrentUploadBufferIndex],
		gUploadWaitEvent
	);
}

void FlushUpload()
{
	ZoneScoped;
	ScopedLock Lock(gUploadMutex);
	if (gUploadTransitions.empty())
	{
		return;
	}

	gUploadCommandList->ResourceBarrier((UINT)gUploadTransitions.size(), gUploadTransitions.data());
	VALIDATE(gUploadCommandList->Close());

	ID3D12CommandList* CommandListsForSubmission[] = { gUploadCommandList.Get() };
	gGraphicsQueue->ExecuteCommandLists(ArrayCount(CommandListsForSubmission), CommandListsForSubmission);

	FlushQueue(gGraphicsQueue.Get(), gUploadFences[gCurrentUploadBufferIndex].Get(), mCurrentUploadFenceValue, gUploadWaitEvent);

	gUploadTransitions.clear();
	gUploadBuffers[gCurrentUploadBufferIndex].resize(1); // always leave the first upload buffer
														 //gUploadBufferAddress = NULL;
	gUploadBufferOffset = 0;

	VALIDATE(gUploadCommandAllocators[gCurrentUploadBufferIndex]->Reset());
	VALIDATE(gUploadCommandList->Reset(gUploadCommandAllocators[gCurrentUploadBufferIndex].Get(), nullptr));
}

void UploadBufferData(ID3D12Resource* Destination, const void* Data, uint64_t Size, D3D12_RESOURCE_STATES TargetState)
{
	ZoneScoped;
	CHECK(Size <= UPLOAD_BUFFER_SIZE, "Buffer data is too large for this.");
	if (gUploadBufferOffset + Size > UPLOAD_BUFFER_SIZE)
	{
		FlushUpload();
	}

	D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		Destination,
		D3D12_RESOURCE_STATE_COPY_DEST,
		TargetState
	);

	{
		ScopedLock Lock(gUploadMutex);
		::memcpy(gUploadBufferAddress + gUploadBufferOffset, Data, Size);
		gUploadCommandList->CopyBufferRegion(Destination, 0, gUploadBuffers[gCurrentUploadBufferIndex][0].Get(), gUploadBufferOffset, Size);
		gUploadBufferOffset += Size;

		gUploadTransitions.push_back(Barrier);
	}
}

TracyLockable(Mutex, gPresentLock);
void PresentCurrentBackBuffer()
{
	ZoneScoped;

	ScopedLock Lock(gPresentLock);
	VALIDATE(gSwapChain->Present(gSyncInterval, gPresentFlags));
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

ComPtr<ID3D12PipelineState> CreateShaderCombination(
	TArrayView<D3D12_INPUT_ELEMENT_DESC> PSOLayout,
	TArrayView<StringView> EntryPoints,
	StringView ShaderFile,
	TArrayView<DXGI_FORMAT> RenderTargetFormats,
	DXGI_FORMAT DepthTargetFormat,
	TArrayView<D3D12_RENDER_TARGET_BLEND_DESC> BlendDescs
)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};

	TArray<ComPtr<ID3DBlob>> Shaders;
	
	UINT ShaderCounts[eShaderTypeCount] = {0};
	for (StringView Entry : EntryPoints)
	{
		ShaderType Type = GetShaderTypeFromEntryPoint(Entry);
		ComPtr<ID3DBlob> Shader = CompileShader(ShaderFile, Entry);

		LPVOID Data = Shader->GetBufferPointer();
		SIZE_T Size = Shader->GetBufferSize();

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

		Shaders.push_back(Shader);

		switch (Type)
		{
			case eVertexShader:
				PSODesc.VS.BytecodeLength = Shader->GetBufferSize();
				PSODesc.VS.pShaderBytecode = Shader->GetBufferPointer();
				break;
			case ePixelShader:
				PSODesc.PS.BytecodeLength = Shader->GetBufferSize();
				PSODesc.PS.pShaderBytecode = Shader->GetBufferPointer();
				break;
			case eComputeShader:
				UINT x, y, z;
				Reflection->GetThreadGroupSize(&x, &y, &z);
				break;
			default:
				DEBUG_BREAK();
		}
		CHECK(++ShaderCounts[Type] == 1, "Multiple occurances of the same shader type");
	}

	PSODesc.NumRenderTargets = (UINT)RenderTargetFormats.size();

	CHECK(PSODesc.NumRenderTargets <= 8, "D3D12 Does not support more then 8 render targets.")
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
	PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	PSODesc.SampleDesc.Count = 1;
	PSODesc.SampleMask = 0xFFFFFFFF;
	PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	for (int i = 0; i < BlendDescs.size(); ++i)
	{
		PSODesc.BlendState.RenderTarget[i] = BlendDescs[i];
	}

	return CreatePSO(&PSODesc);
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

void CreateTexture(TextureData& TexData, D3D12_RESOURCE_FLAGS Flags, D3D12_RESOURCE_STATES InitialState, D3D12_CLEAR_VALUE* ClearValue)
{
	ZoneScoped;
	D3D12_RESOURCE_DESC TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		(DXGI_FORMAT)TexData.Format,
		TexData.Width, TexData.Height,
		1, 1,
		1, 0,
		Flags
	);

	ComPtr<ID3D12Resource> Resource = CreateResource(&TextureDesc, &DefaultHeapProps, InitialState, ClearValue);
	TexData.ID = StoreTexture(Resource.Get());
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGeneralHandleGPU(UINT Index)
{
	CHECK(Index != UINT_MAX, "Uninitialized index");
	CD3DX12_GPU_DESCRIPTOR_HANDLE rtv(gGeneralDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		Index, gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);

	return rtv;
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
	ZoneScoped;
	ID3D12DescriptorHeap* DescriptorHeaps[] = {
		gGeneralDescriptorHeap.Get()
	};

	CommandList->SetGraphicsRootSignature(gRootSignature.Get());
	CommandList->SetDescriptorHeaps(ArrayCount(DescriptorHeaps), DescriptorHeaps);
	CommandList->SetGraphicsRootDescriptorTable(
		0,
		GetGeneralHandleGPU(Tex.SRV)
	);
}

void BindRenderTargets(ID3D12GraphicsCommandList* CommandList, TArrayView<uint32_t> RTVs, uint32_t DSV)
{
	TArray<D3D12_CPU_DESCRIPTOR_HANDLE> RTVHandles;
	for (auto RTV : RTVs)
		RTVHandles.push_back(GetRTVHandle(RTV));
	D3D12_CPU_DESCRIPTOR_HANDLE DSVHandle = GetDSVHandle(DSV);
	D3D12_CPU_DESCRIPTOR_HANDLE* DSVHandlePtr = nullptr;
	if (DSV != -1)
		DSVHandlePtr = &DSVHandle;
	CommandList->OMSetRenderTargets((UINT)RTVHandles.size(), RTVHandles.data(), true, DSVHandlePtr);
}

void ClearRenderTarget(ID3D12GraphicsCommandList* CommandList, uint32_t RTV, float* clearColor)
{
	CommandList->ClearRenderTargetView(GetRTVHandle(RTV), clearColor, 0, nullptr);
}

void ClearDepth(ID3D12GraphicsCommandList* CommandList, uint32_t DSV, float depthValue)
{
	CommandList->ClearDepthStencilView(GetDSVHandle(DSV), D3D12_CLEAR_FLAG_DEPTH, depthValue, 0, 0, nullptr);
}

//[[nodiscard("Return value should be stored to later wait for it")]]
//uint64_t RenderContext::ExecuteCopy(ID3D12CommandList* CmdList, ID3D12Fence* Fence, uint64_t& CurrentFenceValue)
//{
//	ZoneScoped;
//	ID3D12CommandList* const CommandLists[] = { CmdList };
//	gCopyQueue->ExecuteCommandLists(ArrayCount(CommandLists), CommandLists);
//	return Signal(gCopyQueue, Fence, CurrentFenceValue);
//}
//
//[[nodiscard("Return value should be stored to later wait for it")]]
//uint64_t RenderContext::ExecuteCompute(ID3D12CommandList* CmdList, ID3D12Fence* Fence, uint64_t& CurrentFenceValue)
//{
//	ZoneScoped;
//	ID3D12CommandList* const CommandLists[] = { CmdList };
//	gComputeQueue->ExecuteCommandLists(ArrayCount(CommandLists), CommandLists);
//	return Signal(gComputeQueue, Fence, CurrentFenceValue);
//}

[[nodiscard("Return value should be stored to later wait for it")]]
uint64_t ExecuteGraphics(ID3D12CommandList* CmdList, ID3D12Fence* Fence, uint64_t& CurrentFenceValue)
{
	ZoneScoped;
	ID3D12CommandList* const CommandLists[] = { CmdList };
	gGraphicsQueue->ExecuteCommandLists(ArrayCount(CommandLists), CommandLists);
	return Signal(gGraphicsQueue.Get(), Fence, CurrentFenceValue);
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
		VALIDATE(gDXGIFactory->CreateSwapChainForHwnd(gGraphicsQueue.Get(), Window.mHwnd, &swapChainDesc, nullptr, nullptr, &Tmp));
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
		gBackBuffers[i].RTV = i;
		gBackBuffers[i].Width  = Width;
		gBackBuffers[i].Height = Height;

		rtvHandle.Offset(gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);
	}
}

void Submit(D3D12CmdList& CmdList, uint64_t CurrentFrameID)
{
	ID3D12CommandList* CommandLists[] = {
		CmdList.Get()
	};
	CmdList->Close();
	switch (CmdList.Type)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
	case D3D12_COMMAND_LIST_TYPE_BUNDLE:
		gGraphicsQueue->ExecuteCommandLists(1, CommandLists);
		break;
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		gComputeQueue->ExecuteCommandLists(1, CommandLists);
		break;
	case D3D12_COMMAND_LIST_TYPE_COPY:
		gCopyQueue->ExecuteCommandLists(1, CommandLists);
		break;
	default:
		DEBUG_BREAK();
	}
	DiscardCommandList(CmdList, CurrentFrameID);
}

void CreateBackBufferResources(System::Window& Window)
{
	DXGI_SWAP_CHAIN_FLAG SwapChainFlag = (DXGI_SWAP_CHAIN_FLAG)(DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);// | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

	gTearingSupported = CheckTearingSupport(gDXGIFactory);
	if (gTearingSupported)
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
			3,
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
		VALIDATE(gSwapChain->SetMaximumFrameLatency(1));
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

UINT gCurrentRenderTargetIndex = BACK_BUFFER_COUNT;
void CreateRTV(TextureData& TexData)
{
	CHECK(gCurrentRenderTargetIndex < RTV_HEAP_SIZE, "Too much RTV descriptors. Need new plan.");

	ZoneScoped;
	D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
	RTVDesc.Format = (DXGI_FORMAT)TexData.Format;
	RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	CD3DX12_CPU_DESCRIPTOR_HANDLE Handle(
		gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		gCurrentRenderTargetIndex,
		gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]
	);
	gDevice->CreateRenderTargetView(GetTextureResource(TexData.ID), &RTVDesc, Handle);

	TexData.RTV = gCurrentRenderTargetIndex++;
}

UINT gCurrentGeneralIndex = 0;
void CreateSRV(TextureData& TexData)
{
	CHECK(gCurrentGeneralIndex < GENERAL_HEAP_SIZE, "Too much SRV descriptors. Need new plan.");

	ZoneScoped;
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Format = (DXGI_FORMAT)TexData.Format;
	SRVDesc.Texture2D.MipLevels = 1;

	D3D12_CPU_DESCRIPTOR_HANDLE Handle = gGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	Handle.ptr += gCurrentGeneralIndex * gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
	gDevice->CreateShaderResourceView(GetTextureResource(TexData.ID), &SRVDesc, Handle);

	TexData.SRV = gCurrentGeneralIndex++;
}

// returns index in SRV heap

void CreateUAV(TextureData& TexData)
{
	CHECK(gCurrentGeneralIndex < GENERAL_HEAP_SIZE, "Too much SRV descriptors. Need new plan.");

	ZoneScoped;
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format = (DXGI_FORMAT)TexData.Format;
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE Handle = gGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	Handle.ptr += gCurrentGeneralIndex * gDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
	gDevice->CreateUnorderedAccessView(GetTextureResource(TexData.ID), NULL, &UAVDesc, Handle);

	TexData.UAV= gCurrentGeneralIndex++;
}

UINT gCurrentDSVIndex = 0;
void CreateDSV(TextureData& TexData)
{
	CHECK(gCurrentDSVIndex < DSV_HEAP_SIZE, "Too much DSV descriptors. Need new plan.");
	ZoneScoped;

	D3D12_DEPTH_STENCIL_VIEW_DESC Desc = {};
	Desc.Format = (DXGI_FORMAT)TexData.Format;
	Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(gDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	gDevice->CreateDepthStencilView(GetTextureResource(TexData.ID), &Desc, dsvHandle);

	TexData.DSV = gCurrentDSVIndex++;
}
