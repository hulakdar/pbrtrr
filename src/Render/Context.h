#pragma once

#include "Containers/ComPtr.h"
#include "Containers/Map.h"
#include "System/Window.h"
#include "Util/Debug.h"
#include "Util/Util.h"
#include "external/d3dx12.h"
#include "Threading/Mutex.h"
#include "Texture.h"

#include <dxgi1_6.h>
#include <imgui.h>
#include <d3dcompiler.h>
#include <Tracy.hpp>
#include <TracyD3D12.hpp>

enum ShaderType
{
	eVertexShader,
	ePixelShader,
	eShaderTypeCount,
};

class RenderContext
{
public:
	void Init(System::Window& Window);
	void UploadTextureData(TextureData& TexData, const uint8_t* RawData, uint32_t RawDataSize = 0);
	void CreateBackBufferResources(System::Window& Window);

	bool IsSwapChainReady();

	void Deinit();

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

	UINT mCurrentDSVIndex = 0;
	void CreateDSV(TextureData& TexData)
	{
		CHECK(mCurrentDSVIndex < DSV_HEAP_SIZE, "Too much DSV descriptors. Need new plan.");
		ZoneScoped;

		D3D12_DEPTH_STENCIL_VIEW_DESC Desc = {};
		Desc.Format = TexData.Format;
		Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(mDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		mDevice->CreateDepthStencilView(TexData.Resource.Get(), &Desc, dsvHandle);

		TexData.DSVIndex = mCurrentDSVIndex++;
	}

	inline static const UINT64	UPLOAD_BUFFER_SIZE = 8_mb;
	inline static const UINT	UPLOAD_BUFFERS = 3;

	ComPtr<ID3D12CommandAllocator>	mUploadCommandAllocators[UPLOAD_BUFFERS];
	UINT64							mUploadFenceValues[UPLOAD_BUFFERS] = {};
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

	void WaitForUploadFinish()
	{
		if (mCurrentUploadBufferIndex == 0)
		{
			mCurrentUploadBufferIndex  = UPLOAD_BUFFERS;
		}
		UINT PreviousUploadBufferIndex = mCurrentUploadBufferIndex - 1;

		// wait if needed
		WaitForFenceValue(
			mUploadFences[PreviousUploadBufferIndex ],
			mUploadFenceValues[PreviousUploadBufferIndex],
			mUploadWaitEvent
		);
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
		//mCurrentUploadBufferIndex = (mCurrentUploadBufferIndex + 1) % UPLOAD_BUFFERS;

		// wait if needed
		WaitForFenceValue(
			mUploadFences[mCurrentUploadBufferIndex],
			mUploadFenceValues[mCurrentUploadBufferIndex],
			mUploadWaitEvent
		);

		mUploadTransitions.clear();
		mUploadBuffers[mCurrentUploadBufferIndex].resize(1); // always leave the first upload buffer
		//mUploadBufferAddress = NULL;
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
	 
		VALIDATE(mDevice->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Result)));
	 
		return Result;
	}

	ComPtr<ID3D12PipelineState> CreateShaderCombination(
		TArrayView<D3D12_INPUT_ELEMENT_DESC> PSOLayout,
		TArrayView<StringView> EntryPoints,
		StringView ShaderFile,
		TArrayView<DXGI_FORMAT> RenderTargetFormats,
		DXGI_FORMAT DepthTargetFormat = DXGI_FORMAT_UNKNOWN
	);

	ComPtr<ID3D12PipelineState> CreatePSO(D3D12_COMPUTE_PIPELINE_STATE_DESC* PSODesc)
	{
		ZoneScoped;
		ComPtr<ID3D12PipelineState> Result;
		VALIDATE(mDevice->CreateComputePipelineState(PSODesc, IID_PPV_ARGS(&Result)));
		return Result;
	}

	ComPtr<ID3D12PipelineState> CreatePSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* PSODesc)
	{
		ZoneScoped;
		ComPtr<ID3D12PipelineState> Result;

		VALIDATE(mDevice->CreateGraphicsPipelineState(PSODesc, IID_PPV_ARGS(&Result)));
		return Result;
	}

	ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_DIRECT)
	{
		ZoneScoped;
		ComPtr<ID3D12CommandAllocator> Result;

		VALIDATE(mDevice->CreateCommandAllocator(Type, IID_PPV_ARGS(&Result)));
		return Result;
	}

	ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12CommandAllocator>& CommandAllocator, D3D12_COMMAND_LIST_TYPE Type)
	{
		ZoneScoped;
		ComPtr<ID3D12GraphicsCommandList> Result;

		VALIDATE(mDevice->CreateCommandList(0, Type, CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&Result)));
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

		VALIDATE(mDevice->CreateCommittedResource(
			HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			ResourceDescription,
			InitialState,
			ClearValue,
			IID_PPV_ARGS(&Result)
		));
		return Result;
	}

	void CreateTexture(
		TextureData& TexData,
		D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_CLEAR_VALUE *ClearValue = nullptr
	)
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

		TexData.Resource = CreateResource(&TextureDesc, &DefaultHeapProps, InitialState, ClearValue);
	}


	ComPtr<ID3D12Fence> CreateFence(UINT64 InitialValue = 0, D3D12_FENCE_FLAGS Flags = D3D12_FENCE_FLAG_NONE)
	{
		ZoneScoped;
		ComPtr<ID3D12Fence> Result;
	 
		VALIDATE(mDevice->CreateFence(InitialValue, Flags, IID_PPV_ARGS(&Result)));
		return Result;
	}

	ComPtr<ID3D12Resource> CreateBuffer(UINT64 Size, bool bUploadBuffer = false, bool bStagingBuffer = false)
	{
		D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(Size);

		D3D12_HEAP_PROPERTIES *HeapProps = &DefaultHeapProps;

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

		return CreateResource(&BufferDesc, HeapProps, InitialState);
	}

	D3D12_VERTEX_BUFFER_VIEW	CreateVertexBufferView(ComPtr<ID3D12Resource>& Buffer,const void *Data, UINT64 Size, UINT64 Stride)
	{
		ZoneScoped;

		D3D12_VERTEX_BUFFER_VIEW Result;
		Result.BufferLocation = Buffer->GetGPUVirtualAddress();
		Result.SizeInBytes = UINT(Size);
		Result.StrideInBytes = UINT(Stride);

		UploadBufferData(Buffer, Data, Size, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		return Result;
	}

	D3D12_INDEX_BUFFER_VIEW		CreateIndexBufferView(ComPtr<ID3D12Resource>& Buffer,const void *Data, UINT64 Size, DXGI_FORMAT Format)
	{
		ZoneScoped;

		D3D12_INDEX_BUFFER_VIEW Result;
		Result.BufferLocation = Buffer->GetGPUVirtualAddress();
		Result.SizeInBytes = UINT(Size);
		Result.Format = Format;

		UploadBufferData(Buffer, Data, Size, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		return Result;
	}



	D3D12_GPU_DESCRIPTOR_HANDLE GetGeneralHandleGPU(UINT Index)
	{
		CHECK(Index != UINT_MAX, "Uninitialized index");
		CD3DX12_GPU_DESCRIPTOR_HANDLE rtv(mGeneralDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			Index, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);

		return rtv;
	}

	//D3D12_CPU_DESCRIPTOR_HANDLE GetGeneralHandle(UINT Index)
	//{
		//CHECK(Index != UINT_MAX, "Uninitialized index");
		//CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mGeneralDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			//Index, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);
		//return rtv;
	//}

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle(TextureData& Texture)
	{
		UINT Index = Texture.RTVIndex;

		CHECK(Index != UINT_MAX, "Uninitialized index");
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			Index, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);

		return rtv;
	}

	TextureData& GetBackBuffer(UINT Index)
	{
		return mBackBuffers[Index];
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle(TextureData& Texture)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(
			mDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			Texture.DSVIndex, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]
		);

		return dsv;
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

	ComPtr<ID3D12CommandQueue>	mGraphicsQueue;

	UINT64 mCurrentFenceValue = 0;
	UINT64 FrameFenceValues[3] = {};

	TracyD3D12Ctx	mGraphicsProfilingCtx;
	TracyD3D12Ctx	mComputeProfilingCtx;
	TracyD3D12Ctx	mCopyProfilingCtx;

	inline static const DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R10G10B10A2_UNORM;
	inline static const DXGI_FORMAT SCENE_COLOR_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;
	inline static const DXGI_FORMAT READBACK_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

	ComPtr<ID3D12Device>		mDevice;
	inline static D3D12_HEAP_PROPERTIES DefaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	inline static D3D12_HEAP_PROPERTIES UploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	inline static D3D12_HEAP_PROPERTIES ReadbackHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
	inline static const UINT BUFFER_COUNT = 3;

private:
	ComPtr<ID3D12DescriptorHeap> mRTVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> mDSVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> mGeneralDescriptorHeap;

	ComPtr<ID3D12Device> CreateDevice();
	ComPtr<IDXGISwapChain2> CreateSwapChain(System::Window& Window, DXGI_SWAP_CHAIN_FLAG Flags);

	inline static const UINT GENERAL_HEAP_SIZE = 4096;
	inline static const UINT DSV_HEAP_SIZE = 32;
	inline static const UINT RTV_HEAP_SIZE = 32;


	ComPtr<ID3D12CommandQueue>	mComputeQueue;
	ComPtr<ID3D12CommandQueue>	mCopyQueue;
	ComPtr<ID3D12RootSignature> mRootSignature;
	ComPtr<ID3D12RootSignature> mComputeRootSignature;
	TextureData		mBackBuffers[BUFFER_COUNT] = {};


	UINT mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

	TracyLockable(Mutex, mUploadMutex);

	bool mTearingSupported = false;
	UINT mSyncInterval = 1;
	UINT mPresentFlags = 0;
	ComPtr<IDXGISwapChain2>		mSwapChain;
	HANDLE	mSwapChainWaitableObject = NULL;

	ComPtr<IDXGIFactory4> mDXGIFactory;

	ComPtr<ID3D12CommandQueue> CreateCommandQueue(D3D12_COMMAND_LIST_TYPE Type);
	void UpdateRenderTargetViews(IVector2 Size);
};

