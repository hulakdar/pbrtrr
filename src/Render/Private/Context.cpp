#include "Render/Context.h"

namespace {
	#include "RenderContextHelpers.inl"

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
			"vs_5_1",
			"ps_5_1",
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

		{
			StringView Shader = LoadWholeFile(FileName);
			ShaderType Type = GetShaderTypeFromEntryPoint(EntryPoint);
			StringView TargetVersion = GetTargetVersionFromType(Type);

			ComPtr<ID3DBlob> ErrorBlob;
			HRESULT HR = D3DCompile(
				Shader.data(), Shader.size(),
				FileName.data(),
				nullptr, nullptr,
				EntryPoint.data(), TargetVersion.data(),
				0, 0,
				&Result,
				&ErrorBlob);
			if (!SUCCEEDED(HR))
			{
				PrintBlob(ErrorBlob.Get());
				DEBUG_BREAK();
			}
		}

		return Result;
	}

}


void RenderContext::Init(System::Window& Window)
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
	mDSVDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
	CreateBackBufferResources(Window);

	if (mTearingSupported)
	{
		mPresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
		mSyncInterval = 0;
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

		mRootSignature = CreateRootSignature(RootBlob);
	}

	// compute
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
			ArraySize(Params), Params,
			ArraySize(Samplers), Samplers,
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

		mComputeRootSignature = CreateRootSignature(RootBlob);
	}
} 

void RenderContext::UploadTextureData(TextureData& TexData, uint8_t *RawData)
{
	ZoneScoped;

	UINT64 UploadBufferSize = GetRequiredIntermediateSize(TexData.Resource.Get(), 0, 1);
	ComPtr<ID3D12Resource> TextureUploadBuffer = CreateBuffer(UploadBufferSize, true);
	UINT Components = ComponentCountFromFormat(TexData.Format);

	IVector2 Size = TexData.Size;
	D3D12_SUBRESOURCE_DATA SrcData = {};
	SrcData.pData = RawData;
	SrcData.RowPitch = Size.x * Components;
	SrcData.SlicePitch = Size.x * Size.y * Components;

	D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		TexData.Resource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);

	ScopedLock Lock(mUploadMutex);
	mUploadBuffers[mCurrentUploadBufferIndex].push_back(TextureUploadBuffer);
	UpdateSubresources<1>(mUploadCommandList.Get(), TexData.Resource.Get(), TextureUploadBuffer.Get(), 0, 0, 1, &SrcData);
	mUploadTransitions.push_back(Barrier);
}

ComPtr<ID3D12PipelineState> RenderContext::CreateShaderCombination(
	TArrayView<D3D12_INPUT_ELEMENT_DESC> PSOLayout,
	TArrayView<StringView> EntryPoints,
	StringView ShaderFile,
	TArrayView<DXGI_FORMAT> RenderTargetFormats,
	DXGI_FORMAT DepthTargetFormat
)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};

	TArray<ComPtr<ID3DBlob>> Shaders;
	
	UINT ShaderCounts[eShaderTypeCount] = {0};
	for (StringView Entry : EntryPoints)
	{
		ComPtr<ID3DBlob> Shader = CompileShader(ShaderFile, Entry);
		Shaders.push_back(Shader);

		ShaderType Type = GetShaderTypeFromEntryPoint(Entry);
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
			default:
				DEBUG_BREAK();
		}
		CHECK(++ShaderCounts[Type] == 1, "Multiple occurances of the same shader type");
	}

	PSODesc.NumRenderTargets = (UINT)RenderTargetFormats.size();

	CHECK(PSODesc.NumRenderTargets < 8, "D3D12 Does not support more then 8 render targets.")
	for (UINT i = 0; i < PSODesc.NumRenderTargets; ++i)
	{
		PSODesc.RTVFormats[i] = RenderTargetFormats[i];
	}
	if (DepthTargetFormat != DXGI_FORMAT_UNKNOWN)
	{
		PSODesc.DSVFormat = DepthTargetFormat;
		PSODesc.DepthStencilState.DepthEnable = true;
		PSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		PSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	}

	PSODesc.pRootSignature = mRootSignature.Get();
	PSODesc.InputLayout.NumElements = (UINT)PSOLayout.size();
	PSODesc.InputLayout.pInputElementDescs = PSOLayout.data();
	PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	PSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	PSODesc.SampleDesc.Count = 1;
	PSODesc.SampleMask = 0xFFFFFFFF;
	PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	return CreatePSO(&PSODesc);
}

ComPtr<ID3D12Device> RenderContext::CreateDevice()
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

ComPtr<IDXGISwapChain2> RenderContext::CreateSwapChain(System::Window& Window, DXGI_SWAP_CHAIN_FLAG Flags)
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

ComPtr<ID3D12CommandQueue> RenderContext::CreateCommandQueue(D3D12_COMMAND_LIST_TYPE Type)
{
	ComPtr<ID3D12CommandQueue> Result;

	D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
	QueueDesc.Type = Type;
	QueueDesc.NodeMask = 1;

	mDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Result));
	return Result;
}

void RenderContext::CreateBackBufferResources(System::Window& Window)
{
	DXGI_SWAP_CHAIN_FLAG SwapChainFlag = (DXGI_SWAP_CHAIN_FLAG)(DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

	mTearingSupported = CheckTearingSupport(mDXGIFactory);
	if (mTearingSupported)
	{
		SwapChainFlag = (DXGI_SWAP_CHAIN_FLAG)(SwapChainFlag | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
	}

	for (int i = 0; i < BUFFER_COUNT; ++i)
	{
		mBackBuffers[i].Resource.Reset();
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

	//mSwapChain->SetFullscreenState(TRUE, NULL);
	UpdateRenderTargetViews(Window.mSize);
}

void RenderContext::UpdateRenderTargetViews(IVector2 Size)
{
	ZoneScoped;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
 
	for (int i = 0; i < BUFFER_COUNT; ++i)
	{
		ComPtr<ID3D12Resource> backBuffer;
		VALIDATE(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		mDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

		SetD3DName(backBuffer, L"Back buffer %d", i);
		mBackBuffers[i].Name = StringFromFormat("Back buffer %d", i);
		mBackBuffers[i].Resource = backBuffer;
		mBackBuffers[i].RTVIndex = i;
		mBackBuffers[i].Size = Size;

		rtvHandle.Offset(mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);
	}
}

bool  RenderContext::IsSwapChainReady()
{
	return WaitForSingleObjectEx(mSwapChainWaitableObject, 1, true) == WAIT_OBJECT_0;
}
