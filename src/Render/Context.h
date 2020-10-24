
#include "Containers/ComPtr.h"
#include "Containers/Map.h"
#include "System/Window.h"
#include "Util/Debug.h"
#include "Util/Util.h"
#include "external/d3dx12.h"
#include "Threading/Mutex.h"

#include <dxgi1_6.h>
#include <imgui.h>
#include <d3dcompiler.h>
#include <Tracy.hpp>
#include <TracyD3D12.hpp>

//#define TEST_WARP

namespace Render {

inline void PrintBlob(ID3DBlob* ErrorBlob)
{
	std::string_view Str((char *)ErrorBlob->GetBufferPointer(), ErrorBlob->GetBufferSize());
	Debug::Print(Str);
	DEBUG_BREAK();
}

#define VALIDATE_D3D_WITH_BLOB(x, blob) if (!SUCCEEDED(x)) {Render::PrintBlob(blob.Get());}

inline ComPtr<ID3DBlob> CompileShader(const char *FileName, const char *EntryPoint, const char *TargetVersion)
{
	StringView Shader = LoadWholeFile(FileName);

	ComPtr<ID3DBlob> Result;

	ComPtr<ID3DBlob> ErrorBlob;
	VALIDATE_D3D_WITH_BLOB(
		D3DCompile(
			Shader.data(), Shader.size(),
			FileName,
			nullptr, nullptr,
			EntryPoint, TargetVersion,
			0, 0,
			&Result,
			&ErrorBlob),
		ErrorBlob
	);
	return Result;
}

inline UINT ComponentCountFromFormat(DXGI_FORMAT Format)
{
	switch (Format)
	{
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return 4;
	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
		return 3;
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
		return 2;
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_R1_UNORM:
		return 1;
	default:
		break;
	}
	CHECK(false, "Some weird format.");
	return 0;
}

struct TextureData
{
	ComPtr<ID3D12Resource>	Resource;

	StringView	Data = {};
	String		Name = {};
	IVector2	Size = {};
	DXGI_FORMAT	Format = DXGI_FORMAT_UNKNOWN;
	UINT		SRVIndex = UINT_MAX;
	UINT		UAVIndex = UINT_MAX;
	UINT		RTVIndex = UINT_MAX;
};

class Context
{
public:
	inline static D3D12_HEAP_PROPERTIES DefaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	inline static D3D12_HEAP_PROPERTIES UploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	inline static const UINT BUFFER_COUNT = 3;
	inline static const UINT GENERAL_HEAP_SIZE = 4096;
	inline static const UINT RTV_HEAP_SIZE = 32;
	inline static const DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R10G10B10A2_UNORM;
	inline static const DXGI_FORMAT SCENE_COLOR_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;

	ComPtr<ID3D12Device>		mDevice;
	ComPtr<ID3D12CommandQueue>	mGraphicsQueue;
	ComPtr<ID3D12CommandQueue>	mComputeQueue;
	ComPtr<ID3D12CommandQueue>	mCopyQueue;
	ComPtr<ID3D12RootSignature> mRootSignature;
	ComPtr<ID3D12RootSignature> mComputeRootSignature;
	TextureData		mSceneColor = {};
	TextureData		mSceneColorReadback = {};

	ComPtr<ID3D12Resource>		mBackBuffers[BUFFER_COUNT] = {};

	TracyD3D12Ctx	mGraphicsProfilingCtx;
	TracyD3D12Ctx	mComputeProfilingCtx;
	TracyD3D12Ctx	mCopyProfilingCtx;

	UINT mCurrentBackBufferIndex = 0;
	UINT mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

	void CreateBackBufferResources(System::Window& Window)
	{
		DXGI_SWAP_CHAIN_FLAG SwapChainFlag = (DXGI_SWAP_CHAIN_FLAG)(DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

		mTearingSupported = false;// CheckTearingSupport(mDXGIFactory);
		if (mTearingSupported)
		{
			SwapChainFlag = (DXGI_SWAP_CHAIN_FLAG)(SwapChainFlag | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
		}

		for (int i = 0; i < BUFFER_COUNT; ++i)
		{
			mBackBuffers[i].Reset();
		}

		if (mSwapChain)
		{
			mSwapChain->ResizeBuffers(
				3,
				Window.mSize.x, Window.mSize.y,
				BACK_BUFFER_FORMAT,
				SwapChainFlag
			);
		}
		else
		{
			mSwapChain = CreateSwapChain(Window, SwapChainFlag);
		}

		mSwapChain->SetMaximumFrameLatency(2);

		mSwapChainWaitableObject = mSwapChain->GetFrameLatencyWaitableObject();

		{
			D3D12_RESOURCE_DESC TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_D24_UNORM_S8_UINT,
				Window.mSize.x, Window.mSize.y,
				1, 1, // ArraySize, MipLevels
				1, 0, // SampleCount, SampleQuality
				D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL	
			);
			D3D12_CLEAR_VALUE ClearValue = {};
			ClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			ClearValue.DepthStencil.Depth = 1.0f;

			mDepthBuffer = CreateResource(&TextureDesc, &Render::Context::DefaultHeapProps, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ClearValue);
		}

		{
			mSceneColor.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			mSceneColor.Size = IVector2(Window.mSize.x, Window.mSize.y);
			CreateTexture(mSceneColor,
				D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			);
			CreateSRV(mSceneColor);
			CreateRTV(mSceneColor);
		}

		{
			mSceneColorReadback.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			mSceneColorReadback.Size = IVector2(960, 540);
			CreateTexture(mSceneColorReadback,
				D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			);
			CreateUAV(mSceneColorReadback);
		}

		mDSVDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
		UpdateRenderTargetViews(Window.mSize);

		mCurrentBackBufferIndex = 0;
	}

	void Init(System::Window& Window)
	{
		ZoneScoped;
		mDXGIFactory = CreateFactory();
		mDevice = CreateDevice();

		for (int i = 0; i < ArraySize(mDescriptorSizes); ++i)
		{
			mDescriptorSizes[i] = mDevice->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)i);
		}
		mGraphicsQueue = CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		mGraphicsProfilingCtx = TracyD3D12Context(mDevice.Get(), mGraphicsQueue.Get());
		mComputeQueue = CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		mComputeProfilingCtx = TracyD3D12Context(mDevice.Get(), mComputeQueue.Get());
		mCopyQueue = CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
		mCopyProfilingCtx = TracyD3D12Context(mDevice.Get(), mCopyQueue.Get());

		mGeneralDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GENERAL_HEAP_SIZE, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
		mRTVDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_HEAP_SIZE);
		CreateBackBufferResources(Window);

		if (mTearingSupported)
		{
			mPresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
			mSyncInterval = !mTearingSupported;
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
				ArraySize(Params), Params,
				ArraySize(Samplers), Samplers,
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
			);

			ComPtr<ID3DBlob> RootBlob;
			ComPtr<ID3DBlob> ErrorBlob;
			VALIDATE_D3D_WITH_BLOB(
				D3D12SerializeRootSignature(
					&DescRootSignature,
					D3D_ROOT_SIGNATURE_VERSION_1,
					&RootBlob,
					&ErrorBlob),
				ErrorBlob
			);

			mRootSignature = CreateRootSignature(RootBlob);
		}

		// compute
		{
			CD3DX12_ROOT_PARAMETER Params[3] = {};

			{
				CD3DX12_DESCRIPTOR_RANGE Range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
				Params[0].InitAsDescriptorTable(1, &Range);
			}
			{
				CD3DX12_DESCRIPTOR_RANGE Range(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
				Params[1].InitAsDescriptorTable(1, &Range);
			}
			Params[2].InitAsConstants(16, 0);

			CD3DX12_STATIC_SAMPLER_DESC Samplers[2] = {};
			Samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
			Samplers[1].Init(1, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT);

			CD3DX12_ROOT_SIGNATURE_DESC DescRootSignature;

			DescRootSignature.Init(
				ArraySize(Params), Params,
				ArraySize(Samplers), Samplers,
				D3D12_ROOT_SIGNATURE_FLAG_NONE	
			);

			ComPtr<ID3DBlob> RootBlob;
			ComPtr<ID3DBlob> ErrorBlob;
			VALIDATE_D3D_WITH_BLOB(
				D3D12SerializeRootSignature(
					&DescRootSignature,
					D3D_ROOT_SIGNATURE_VERSION_1,
					&RootBlob,
					&ErrorBlob),
				ErrorBlob
			);

			mComputeRootSignature = CreateRootSignature(RootBlob);
		}
	} 

	void Deinit()
	{
		TracyD3D12Destroy(mGraphicsProfilingCtx);
		TracyD3D12Destroy(mComputeProfilingCtx);
		TracyD3D12Destroy(mCopyProfilingCtx);
	}

	bool  IsSwapChainReady()
	{
		return WaitForSingleObjectEx(mSwapChainWaitableObject, 1, true) == WAIT_OBJECT_0;
	}

	UINT mCurrentRenderTargetIndex = BUFFER_COUNT;
	void CreateRTV(TextureData& TexData)
	{
		CHECK(mCurrentRenderTargetIndex < RTV_HEAP_SIZE, "Too much RTV descriptors. Need new plan.");

		ZoneScoped;
		D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
		RTVDesc.Format = TexData.Format;
		RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

		CD3DX12_CPU_DESCRIPTOR_HANDLE Handle(
			mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			mCurrentRenderTargetIndex,
			mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]
		);
		mDevice->CreateRenderTargetView(TexData.Resource.Get(), &RTVDesc, Handle);

		TexData.RTVIndex = mCurrentRenderTargetIndex++;
	}

	UINT mCurrentGeneralIndex = 0;

	void CreateSRV(TextureData& TexData)
	{
		CHECK(mCurrentGeneralIndex < GENERAL_HEAP_SIZE, "Too much SRV descriptors. Need new plan.");

		ZoneScoped;
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.Format = TexData.Format;
		SRVDesc.Texture2D.MipLevels = 1;

		D3D12_CPU_DESCRIPTOR_HANDLE Handle = mGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		Handle.ptr += mCurrentGeneralIndex * mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
		mDevice->CreateShaderResourceView(TexData.Resource.Get(), &SRVDesc, Handle);

		TexData.SRVIndex = mCurrentGeneralIndex++;
	}

	// returns index in SRV heap
	void CreateUAV(TextureData& TexData)
	{
		CHECK(mCurrentGeneralIndex < GENERAL_HEAP_SIZE, "Too much SRV descriptors. Need new plan.");

		ZoneScoped;
		D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
		UAVDesc.Format = TexData.Format;
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

		D3D12_CPU_DESCRIPTOR_HANDLE Handle = mGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		Handle.ptr += mCurrentGeneralIndex * mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
		mDevice->CreateUnorderedAccessView(TexData.Resource.Get(), NULL, &UAVDesc, Handle);

		TexData.UAVIndex = mCurrentGeneralIndex++;
	}

	inline static const UINT64	UPLOAD_BUFFER_SIZE = 8_mb;
	inline static const UINT	UPLOAD_BUFFERS = 3;

	ComPtr<ID3D12CommandAllocator>	mUploadCommandAllocators[UPLOAD_BUFFERS];
	UINT64							mUploadFenceValues[UPLOAD_BUFFERS];
	TArray<ComPtr<ID3D12Resource>>	mUploadBuffers[UPLOAD_BUFFERS];
	ComPtr<ID3D12Fence>				mUploadFences[UPLOAD_BUFFERS];

	ComPtr<ID3D12GraphicsCommandList>	mUploadCommandList;
	TArray<D3D12_RESOURCE_BARRIER>		mUploadTransitions;
	UINT mCurrentUploadBufferIndex = 0;

	unsigned char			*mUploadBufferAddress = NULL;
	UINT64					mUploadBufferOffset = NULL;

	UINT64					mCurrentUploadFenceValue = 0;
	HANDLE					mUploadWaitEvent;

	void InitUploadResources()
	{
		ZoneScoped;
		mUploadWaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		for (int i = 0; i < UPLOAD_BUFFERS; ++i)
		{
			mUploadCommandAllocators[i] = CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY);
			VALIDATE(mUploadCommandAllocators[i]->Reset());
			mUploadBuffers[i].push_back(CreateBuffer(UPLOAD_BUFFER_SIZE, true));
			mUploadFences[i] = CreateFence();
		}

		mUploadCommandList = CreateCommandList(mUploadCommandAllocators[0], D3D12_COMMAND_LIST_TYPE_COPY);
		VALIDATE(mUploadCommandList->Reset(mUploadCommandAllocators[0].Get(), nullptr));

		VALIDATE(mUploadBuffers[0][0]->Map(0, nullptr, (void**)&mUploadBufferAddress));
		mUploadBufferOffset = 0;
	}

	void FlushUpload()
	{
		ZoneScoped;
		ScopedLock Lock(mUploadMutex);
		if (mUploadTransitions.empty())
		{
			return;
		}

		//mUploadCommandList->ResourceBarrier((UINT)mUploadTransitions.size(), mUploadTransitions.data());
		VALIDATE(mUploadCommandList->Close());

		ID3D12CommandList* CommandListsForSubmission[] = { mUploadCommandList.Get() };
		mCopyQueue->ExecuteCommandLists(ArraySize(CommandListsForSubmission), CommandListsForSubmission);

		mUploadFenceValues[mCurrentUploadBufferIndex] = Signal(
			mCopyQueue,
			mUploadFences[mCurrentUploadBufferIndex],
			mCurrentUploadFenceValue
		);

		// go to next buffers
		mCurrentUploadFenceValue++;

		// wait if needed
		WaitForFenceValue(
			mUploadFences[mCurrentUploadBufferIndex],
			mUploadFenceValues[mCurrentUploadBufferIndex],
			mUploadWaitEvent
		);

		mUploadTransitions.clear();
		mUploadBuffers[mCurrentUploadBufferIndex].resize(1); // always leave the first upload buffer
		mUploadBufferAddress = NULL;
		mUploadBufferOffset = 0;

		VALIDATE(mUploadCommandAllocators[mCurrentUploadBufferIndex]->Reset());
		VALIDATE(mUploadCommandList->Reset(mUploadCommandAllocators[mCurrentUploadBufferIndex].Get(), nullptr));
	}

	void UploadBufferData(ComPtr<ID3D12Resource>& Destination, const void* Data, UINT64 Size, D3D12_RESOURCE_STATES TargetState)
	{
		ZoneScoped;
		CHECK(Size <= UPLOAD_BUFFER_SIZE, "Buffer data is too large for this.");
		if (mUploadBufferOffset + Size > UPLOAD_BUFFER_SIZE)
		{
			FlushUpload();
		}

		D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
											Destination.Get(),
											D3D12_RESOURCE_STATE_COPY_DEST,
											TargetState
										);

		{
			ScopedLock Lock(mUploadMutex);
			::memcpy(mUploadBufferAddress + mUploadBufferOffset, Data, Size);
			mUploadCommandList->CopyBufferRegion(Destination.Get(), 0, mUploadBuffers[mCurrentUploadBufferIndex][0].Get(), mUploadBufferOffset, Size);
			mUploadBufferOffset += Size;

			mUploadTransitions.push_back(Barrier);
		}
	}

	void Present()
	{
		ZoneScoped;
		VALIDATE(mSwapChain->Present(mSyncInterval, mPresentFlags));
		TracyD3D12Collect(mGraphicsProfilingCtx);
		TracyD3D12Collect(mCopyProfilingCtx);
	}

	ComPtr<ID3D12RootSignature> CreateRootSignature(ComPtr<ID3DBlob> RootBlob)
	{
		ZoneScoped;
		ComPtr<ID3D12RootSignature> Result;
		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(
				mDevice->CreateRootSignature(
					0,
					RootBlob->GetBufferPointer(),
					RootBlob->GetBufferSize(),
					IID_PPV_ARGS(&Result)
				)
			);
		}
		return Result;
	}

	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32_t NumDescriptors, D3D12_DESCRIPTOR_HEAP_FLAGS Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE	)
	{
		ZoneScoped;
		ComPtr<ID3D12DescriptorHeap> Result;
	 
		D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
		Desc.NumDescriptors = NumDescriptors;
		Desc.Type = Type;
		Desc.Flags = Flags;
	 
		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Result)));
		}
	 
		return Result;
	}

	ComPtr<ID3D12PipelineState> CreatePSO(D3D12_COMPUTE_PIPELINE_STATE_DESC* PSODesc)
	{
		ZoneScoped;
		ComPtr<ID3D12PipelineState> Result;

		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateComputePipelineState(PSODesc, IID_PPV_ARGS(&Result)));
		}
		return Result;
	}

	ComPtr<ID3D12PipelineState> CreatePSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* PSODesc)
	{
		ZoneScoped;
		ComPtr<ID3D12PipelineState> Result;

		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateGraphicsPipelineState(PSODesc, IID_PPV_ARGS(&Result)));
		}
		return Result;
	}

	ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_DIRECT)
	{
		ZoneScoped;
		ComPtr<ID3D12CommandAllocator> Result;

		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateCommandAllocator(Type, IID_PPV_ARGS(&Result)));
		}
		return Result;
	}

	ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12CommandAllocator>& CommandAllocator, D3D12_COMMAND_LIST_TYPE Type)
	{
		ZoneScoped;
		ComPtr<ID3D12GraphicsCommandList> Result;

		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateCommandList(0, Type, CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&Result)));
		}

		VALIDATE(Result->Close());
		
		return Result;
	}

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

	ComPtr<ID3D12Resource> CreateResource(D3D12_RESOURCE_DESC *ResourceDescription, D3D12_HEAP_PROPERTIES *HeapProperties, D3D12_RESOURCE_STATES InitialState, D3D12_CLEAR_VALUE *ClearValue = NULL)
	{
		ZoneScoped;
		ComPtr<ID3D12Resource> Result;

		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateCommittedResource(
				HeapProperties,
				D3D12_HEAP_FLAG_NONE,
				ResourceDescription,
				InitialState,
				ClearValue,
				IID_PPV_ARGS(&Result)
			));
		}
		return Result;
	}

	void CreateTexture(
		TextureData& TexData,
		D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_COPY_DEST)
	{
		ZoneScoped;
		IVector2 Size = TexData.Size;
		D3D12_RESOURCE_DESC TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			TexData.Format,
			Size.x, Size.y,
			1, 1,
			1, 0,
			Flags
		);

		TexData.Resource = CreateResource(&TextureDesc, &Render::Context::DefaultHeapProps, InitialState);

		if (TexData.Data.data())
		{
			UINT64 UploadBufferSize = GetRequiredIntermediateSize(TexData.Resource.Get(), 0, 1);
			ComPtr<ID3D12Resource> TextureUploadBuffer = CreateBuffer(UploadBufferSize, true);
			UINT Components = ComponentCountFromFormat(TexData.Format);

			D3D12_SUBRESOURCE_DATA SrcData = {};
			SrcData.pData = TexData.Data.data();
			SrcData.RowPitch = Size.x * Components;
			SrcData.SlicePitch = Size.x * Size.y * Components;

			D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				TexData.Resource.Get(),
				InitialState,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			);

			ScopedLock Lock(mUploadMutex);
			mUploadBuffers[mCurrentUploadBufferIndex].push_back(TextureUploadBuffer);
			UpdateSubresources<1>(mUploadCommandList.Get(), TexData.Resource.Get(), TextureUploadBuffer.Get(), 0, 0, 1, &SrcData);
			mUploadTransitions.push_back(Barrier);
		}
	}

	ComPtr<ID3D12Fence> CreateFence(UINT64 InitialValue = 0, D3D12_FENCE_FLAGS Flags = D3D12_FENCE_FLAG_NONE)
	{
		ZoneScoped;
		ComPtr<ID3D12Fence> Result;
	 
		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateFence(InitialValue, Flags, IID_PPV_ARGS(&Result)));
		}
		return Result;
	}

	ComPtr<ID3D12Resource> CreateBuffer(UINT64 Size, bool bUploadBuffer = false)
	{
		D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(Size);

		D3D12_HEAP_PROPERTIES *HeapProps = &DefaultHeapProps;

		D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_COPY_DEST;

		if (bUploadBuffer)
		{
			HeapProps = &UploadHeapProperties;
			InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
		}

		return CreateResource(&BufferDesc, HeapProps, InitialState);
	}

	D3D12_VERTEX_BUFFER_VIEW	CreateVertexBuffer(const void *Data, UINT64 Size, UINT64 Stride)
	{
		ZoneScoped;
		ComPtr<ID3D12Resource> Buffer = CreateBuffer(Size);

		D3D12_VERTEX_BUFFER_VIEW Result;
		Result.BufferLocation = Buffer->GetGPUVirtualAddress();
		Result.SizeInBytes = UINT(Size);
		Result.StrideInBytes = UINT(Stride);

		UploadBufferData(Buffer, Data, Size, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		return Result;
	}

	D3D12_INDEX_BUFFER_VIEW		CreateIndexBuffer(const void *Data, UINT64 Size, DXGI_FORMAT Format)
	{
		ZoneScoped;
		ComPtr<ID3D12Resource> Buffer = CreateBuffer(Size);

		D3D12_INDEX_BUFFER_VIEW Result;
		Result.BufferLocation = Buffer->GetGPUVirtualAddress();
		Result.SizeInBytes = UINT(Size);
		Result.Format = Format;

		UploadBufferData(Buffer, Data, Size, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		return Result;
	}

	ComPtr<ID3D12PipelineState> GuiPSO;
	void InitGUIResources()
	{
		// PSO
		D3D12_INPUT_ELEMENT_DESC PSOLayout[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"COLOR", 	 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};

		ComPtr<ID3DBlob> VertexShader = CompileShader(
			"content/shaders/GUI.hlsl",
			"MainVS", "vs_5_1"
		);

		ComPtr<ID3DBlob> PixelShader = CompileShader(
			"content/shaders/GUI.hlsl",
			"MainPS", "ps_5_1"
		);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.VS.BytecodeLength = VertexShader->GetBufferSize();
		PSODesc.VS.pShaderBytecode = VertexShader->GetBufferPointer();
		PSODesc.PS.BytecodeLength = PixelShader->GetBufferSize();
		PSODesc.PS.pShaderBytecode = PixelShader->GetBufferPointer();
		PSODesc.pRootSignature = mRootSignature.Get();
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = BACK_BUFFER_FORMAT;
		PSODesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		PSODesc.InputLayout.NumElements = ArraySize(PSOLayout);
		PSODesc.InputLayout.pInputElementDescs = PSOLayout;
		PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(
			D3D12_FILL_MODE_SOLID,
			D3D12_CULL_MODE_NONE,
			FALSE, 0, 0.f, 0.f, FALSE, FALSE, FALSE, FALSE,
			D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
		);

		PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        const D3D12_RENDER_TARGET_BLEND_DESC GuiBlendDesc =
        {
            TRUE, FALSE,
            D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ZERO, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL,
        };
		PSODesc.BlendState.RenderTarget[0] = GuiBlendDesc;

		PSODesc.SampleDesc.Count = 1;
		PSODesc.DepthStencilState.DepthEnable = false;
		PSODesc.DepthStencilState.StencilEnable = false;
		PSODesc.SampleMask = 0xFFFFFFFF;
		PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&GuiPSO)));
		}
	}

	UINT64 ImVertexHighwatermarks[BUFFER_COUNT] = {};
	UINT64 ImIndexHighwatermarks[BUFFER_COUNT] = {};
	ComPtr<ID3D12Resource> ImGuiVertexBuffers[BUFFER_COUNT];
	ComPtr<ID3D12Resource> ImGuiIndexBuffers[BUFFER_COUNT];
	void RenderGUI(ComPtr<ID3D12GraphicsCommandList>& CommandList, System::Window& Window, TextureData& Font)
	{
		ZoneScoped;

		ImGui::EndFrame();
		ImGui::Render();

		ComPtr<ID3D12Resource>& ImGuiVertexBuffer = ImGuiVertexBuffers[mCurrentBackBufferIndex];
		ComPtr<ID3D12Resource>& ImGuiIndexBuffer = ImGuiIndexBuffers[mCurrentBackBufferIndex];
		UINT64& WatermarkVertex = ImVertexHighwatermarks[mCurrentBackBufferIndex];
		UINT64& WatermarkIndex = ImIndexHighwatermarks[mCurrentBackBufferIndex];

		ImDrawData *DrawData = ImGui::GetDrawData();
		if (!DrawData || DrawData->TotalVtxCount == 0)
		{
			return;
		}

		UINT64 VertexBufferSize = DrawData->TotalVtxCount * sizeof(ImDrawVert);
		if (VertexBufferSize > WatermarkVertex)
		{
			ImGuiVertexBuffer = CreateBuffer(VertexBufferSize, true);
			WatermarkVertex = VertexBufferSize;
		}

		UINT64 IndexBufferSize = DrawData->TotalIdxCount * sizeof(ImDrawIdx);
		if (IndexBufferSize > WatermarkIndex)
		{
			WatermarkIndex = IndexBufferSize;
			ImGuiIndexBuffer = CreateBuffer(IndexBufferSize, true);
		}

		UINT64 VtxOffset = 0;
		UINT64 IdxOffset = 0;

		unsigned char* VtxP = NULL;
		unsigned char* IdxP = NULL;
		ImGuiVertexBuffer->Map(0, nullptr, (void**)&VtxP);
		ImGuiIndexBuffer->Map(0, nullptr, (void**)&IdxP);
		for (int i = 0; i < DrawData->CmdListsCount; ++i)
		{
			ImDrawList* ImGuiCmdList = DrawData->CmdLists[i];

			memcpy(VtxP + VtxOffset, ImGuiCmdList->VtxBuffer.Data, ImGuiCmdList->VtxBuffer.Size * sizeof(ImDrawVert));
			VtxOffset += ImGuiCmdList->VtxBuffer.Size * sizeof(ImDrawVert);
			memcpy(IdxP + IdxOffset, ImGuiCmdList->IdxBuffer.Data, ImGuiCmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
			IdxOffset += ImGuiCmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
		}
		ImGuiVertexBuffer->Unmap(0, nullptr);
		ImGuiIndexBuffer->Unmap(0, nullptr);

		CHECK(VtxOffset == VertexBufferSize && IdxOffset == IndexBufferSize, "Make sure that we upload everything.");

		TracyD3D12Zone(mGraphicsProfilingCtx, CommandList.Get(), "Render GUI");

		CommandList->SetGraphicsRootSignature(mRootSignature.Get());
		{
			CD3DX12_GPU_DESCRIPTOR_HANDLE  GpuHandle(
				mGeneralDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
				Font.SRVIndex,
				mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]
			);

			ID3D12DescriptorHeap* DescriptorHeaps[] = {
				mGeneralDescriptorHeap.Get()
			};
			CommandList->SetDescriptorHeaps(ArraySize(DescriptorHeaps), DescriptorHeaps);
			CommandList->SetGraphicsRootDescriptorTable(
				0,
				GpuHandle
			);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetRTVHandleForBackBuffer();
		CommandList->OMSetRenderTargets(1, &rtv, true, nullptr);
		CommandList->SetPipelineState(GuiPSO.Get());
		CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// WindowSize
		CommandList->SetGraphicsRoot32BitConstants(1, 2, &Window.mSize, 0);

		D3D12_INDEX_BUFFER_VIEW ImGuiIndexBufferView;
		ImGuiIndexBufferView.BufferLocation = ImGuiIndexBuffer->GetGPUVirtualAddress();
		ImGuiIndexBufferView.SizeInBytes = UINT(IndexBufferSize);
		ImGuiIndexBufferView.Format = DXGI_FORMAT_R16_UINT;
		CommandList->IASetIndexBuffer(&ImGuiIndexBufferView);

		UINT64 VertexOffset = 0;
		UINT  IndexOffset = 0;
		for (int i = 0; i < DrawData->CmdListsCount; ++i)
		{
			ImDrawList* ImGuiCmdList = DrawData->CmdLists[i];

			D3D12_VERTEX_BUFFER_VIEW ImGuiVertexBufferView;
			ImGuiVertexBufferView.BufferLocation = ImGuiVertexBuffer->GetGPUVirtualAddress() + VertexOffset;
			ImGuiVertexBufferView.SizeInBytes = ImGuiCmdList->VtxBuffer.Size * sizeof(ImDrawVert);
			ImGuiVertexBufferView.StrideInBytes = sizeof(ImDrawVert);

			CommandList->IASetVertexBuffers(0, 1, &ImGuiVertexBufferView);
			for (auto& ImGuiCmd : ImGuiCmdList->CmdBuffer)
			{
				D3D12_RECT Rect{
					LONG(ImGuiCmd.ClipRect.x),
					LONG(ImGuiCmd.ClipRect.y),
					LONG(ImGuiCmd.ClipRect.z),
					LONG(ImGuiCmd.ClipRect.w),
				};
				CommandList->RSSetScissorRects(1, &Rect);

				CommandList->DrawIndexedInstanced(ImGuiCmd.ElemCount, 1, IndexOffset, 0, 0);
				IndexOffset += ImGuiCmd.ElemCount;
			}
			VertexOffset += ImGuiVertexBufferView.SizeInBytes;
		}
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetGeneralHandleGPU(UINT Index)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE rtv(mGeneralDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			Index, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);

		return rtv;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetGeneralHandle(UINT Index)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			Index, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);

		return rtv;
	}


	D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle(UINT Index)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			Index, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);

		return rtv;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandleForBackBuffer()
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			mCurrentBackBufferIndex, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);

		return rtv;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle()
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
			mDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			0, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]
		);

		return rtv;
	}

	void BindDescriptors(ComPtr<ID3D12GraphicsCommandList>& CommandList, TextureData& Tex)
	{
		ZoneScoped;
		ID3D12DescriptorHeap* DescriptorHeaps[] = {
			mGeneralDescriptorHeap.Get()
		};

		CD3DX12_GPU_DESCRIPTOR_HANDLE  GpuHandle(
			mGeneralDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			Tex.SRVIndex,
			mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]
		);

		CommandList->SetGraphicsRootSignature(mRootSignature.Get());
		CommandList->SetDescriptorHeaps(ArraySize(DescriptorHeaps), DescriptorHeaps);
		CommandList->SetGraphicsRootDescriptorTable(
			0,
			GpuHandle
		);
	}

	[[nodiscard ("Return value should be stored to later wait for it")]]
	UINT64 ExecuteCopy(ID3D12CommandList *CmdList, ComPtr<ID3D12Fence>& Fence, UINT64& CurrentFenceValue)
	{
		ZoneScoped;
		ID3D12CommandList* const CommandLists[] = { CmdList };
		mCopyQueue->ExecuteCommandLists(ArraySize(CommandLists), CommandLists);
		return Signal(mCopyQueue, Fence, CurrentFenceValue);
	}

	[[nodiscard ("Return value should be stored to later wait for it")]]
	UINT64 ExecuteCompute(ID3D12CommandList *CmdList, ComPtr<ID3D12Fence>& Fence, UINT64& CurrentFenceValue)
	{
		ZoneScoped;
		ID3D12CommandList* const CommandLists[] = { CmdList };
		mComputeQueue->ExecuteCommandLists(ArraySize(CommandLists), CommandLists);
		return Signal(mComputeQueue, Fence, CurrentFenceValue);
	}

	[[nodiscard ("Return value should be stored to later wait for it")]]
	UINT64 ExecuteGraphics(ID3D12CommandList *CmdList, ComPtr<ID3D12Fence>& Fence, UINT64& CurrentFenceValue)
	{
		ZoneScoped;
		ID3D12CommandList* const CommandLists[] = { CmdList };
		mGraphicsQueue->ExecuteCommandLists(ArraySize(CommandLists), CommandLists);
		return Signal(mGraphicsQueue, Fence, CurrentFenceValue);
	}

	ComPtr<ID3D12DescriptorHeap> mRTVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> mDSVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> mGeneralDescriptorHeap;

private:
	TracyLockable(Mutex, mDeviceMutex);
	TracyLockable(Mutex, mUploadMutex);

	bool mTearingSupported = false;
	UINT mSyncInterval = 1;
	UINT mPresentFlags = 0;
	ComPtr<IDXGISwapChain2>		mSwapChain;
	ComPtr<ID3D12Resource>		mDepthBuffer;
	HANDLE	mSwapChainWaitableObject = NULL;

	ComPtr<IDXGIFactory4> mDXGIFactory;

	ComPtr<IDXGISwapChain2> CreateSwapChain(System::Window& Window, DXGI_SWAP_CHAIN_FLAG Flags)
	{
		ZoneScoped;
		ComPtr<IDXGISwapChain2> Result;

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = (UINT)Window.mSize.x;
		swapChainDesc.Height = (UINT)Window.mSize.y;
		swapChainDesc.Format = BACK_BUFFER_FORMAT;
		swapChainDesc.Scaling = DXGI_SCALING_NONE;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = BUFFER_COUNT;
		swapChainDesc.Flags = Flags;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

		{
			ComPtr<IDXGISwapChain1> Tmp;
			VALIDATE(mDXGIFactory->CreateSwapChainForHwnd(mGraphicsQueue.Get(), Window.mHwnd, &swapChainDesc, nullptr, nullptr, &Tmp));
			Tmp.As(&Result);
			CHECK(Result, "DXGI 1.3 not supported?");
		}

		return Result;
	}

	void UpdateRenderTargetViews(Math::Vector2 Size)
	{
		ZoneScoped;
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(mDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	 
		mDevice->CreateDepthStencilView(mDepthBuffer.Get(), nullptr, dsvHandle);
		for (int i = 0; i < BUFFER_COUNT; ++i)
		{
			ComPtr<ID3D12Resource> backBuffer;
			VALIDATE(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
	 
			{
				ScopedLock Lock(mDeviceMutex);
				mDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
			}
			
			SetD3DName(backBuffer, L"Back buffer %d", i);
	 
			mBackBuffers[i] = backBuffer;
	 
			rtvHandle.Offset(mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);
		}
	}

	ComPtr<ID3D12CommandQueue> CreateCommandQueue(D3D12_COMMAND_LIST_TYPE Type)
	{
		ComPtr<ID3D12CommandQueue> Result;

		D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
		QueueDesc.Type = Type;
		QueueDesc.NodeMask = 1;

		{
			ScopedLock Lock(mDeviceMutex);
			mDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Result));
		}

		return Result;
	}

	ComPtr<ID3D12Device> CreateDevice()
	{
		ComPtr<ID3D12Device> Result;

		SIZE_T MaxSize = 0;
		ComPtr<IDXGIAdapter1> Adapter;
	#ifndef TEST_WARP
		for (uint32_t Idx = 0; DXGI_ERROR_NOT_FOUND != mDXGIFactory->EnumAdapters1(Idx, &Adapter); ++Idx)
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
			VALIDATE(mDXGIFactory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)));
			VALIDATE(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Result)));
		}

		#ifdef _DEBUG
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
		#endif

		return Result;
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
	 
		return allowTearing == TRUE;
	}

};

}

