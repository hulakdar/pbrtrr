
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

//#define TEST_WARP

namespace Render {

inline void PrintError(ID3DBlob* ErrorBlob)
{
	std::string_view Str((char *)ErrorBlob->GetBufferPointer(), ErrorBlob->GetBufferSize());
	Debug::Print(Str);
	DEBUG_BREAK();
}

#define VALIDATE_D3D_WITH_BLOB(x, blob) if (!SUCCEEDED(x)) {Render::PrintError(blob.Get());}

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

struct TextureData
{
	ComPtr<ID3D12Resource>	Texture;

	StringView	Data;
	IVector2	Size;
	DXGI_FORMAT	Format;
	UINT		SRVHeapIndex;
};

inline bool CheckTearingSupport(ComPtr<IDXGIFactory4> dxgiFactory)
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

class Context
{
public:
	inline static D3D12_HEAP_PROPERTIES DefaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	inline static D3D12_HEAP_PROPERTIES UploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	inline static const UINT BUFFER_COUNT = 3;

	ComPtr<ID3D12Device>		mDevice;
	ComPtr<ID3D12CommandQueue>	mGraphicsQueue;
	ComPtr<ID3D12Resource>		mBackBuffers[BUFFER_COUNT];

	UINT mCurrentBackBufferIndex = 0;
	UINT mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

	bool Init(System::Window& Window)
	{
		mDXGIFactory = CreateFactory();
		mDevice = CreateDevice();

		for (int i = 0; i < ArraySize(mDescriptorSizes); ++i)
		{
			mDescriptorSizes[i] = mDevice->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)i);
		}
		mGraphicsQueue = CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		UploadWaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		DXGI_SWAP_CHAIN_FLAG SwapChainFlag = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		mTearingSupported = false;// CheckTearingSupport(mDXGIFactory);
		if (mTearingSupported)
			SwapChainFlag = (DXGI_SWAP_CHAIN_FLAG)(SwapChainFlag | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
		mSwapChain = CreateSwapChain(Window, SwapChainFlag);

		mRTVDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, BUFFER_COUNT);
		mSRVDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
		UpdateRenderTargetViews();

		if (mTearingSupported)
			mPresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
		mSyncInterval = 2;// !mTearingSupported;

		InitUploadResources();

		return true;
	} 

	// returns index in SRV heap
	UINT CreateSRV(TextureData& Texture)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.Format = Texture.Format;
		SRVDesc.Texture2D.MipLevels = 1;

		D3D12_CPU_DESCRIPTOR_HANDLE Handle = mSRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		Handle.ptr += mCurrentSRVIndex * mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
		mDevice->CreateShaderResourceView(Texture.Texture.Get(), &SRVDesc, Handle);

		return mCurrentSRVIndex++;
	}

	void InitUploadResources()
	{
		UploadFence = CreateFence();
		UploadCommandAllocator = CreateCommandAllocator();
		UploadCommandList = CreateCommandList(UploadCommandAllocator, D3D12_COMMAND_LIST_TYPE_DIRECT);
		VALIDATE(UploadCommandAllocator->Reset());
		VALIDATE(UploadCommandList->Reset(UploadCommandAllocator.Get(), nullptr));
		UploadBuffer = CreateBuffer(UPLOAD_BUFFER_SIZE , true);
		VALIDATE(UploadBuffer->Map(0, nullptr, (void**)&mUploadBufferAddress));
		UploadBufferOffset = 0;
	}

	void Present()
	{
		VALIDATE(mSwapChain->Present(mSyncInterval, mPresentFlags));
	}

	ComPtr<ID3D12RootSignature> CreateRootSignature(ComPtr<ID3DBlob> RootBlob)
	{
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

	ComPtr<ID3D12DescriptorHeap> GuiDescriptorHeap;

	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32_t NumDescriptors, D3D12_DESCRIPTOR_HEAP_FLAGS Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE	)
	{
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


	ComPtr<ID3D12PipelineState> CreatePSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* PSODesc)
	{
		ComPtr<ID3D12PipelineState> Result;

		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateGraphicsPipelineState(PSODesc, IID_PPV_ARGS(&Result)));
		}
		return Result;
	}

	ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_DIRECT)
	{
		ComPtr<ID3D12CommandAllocator> Result;

		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateCommandAllocator(Type, IID_PPV_ARGS(&Result)));
		}
		return Result;
	}

	ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12CommandAllocator>& CommandAllocator, D3D12_COMMAND_LIST_TYPE Type)
	{
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
		D3D12_RESOURCE_DESC ConstantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ConstantBufferType));

		ComPtr<ID3D12Resource> Result = CreateResource(&ConstantBufferDesc, &UploadHeapProperties, D3D12_RESOURCE_STATE_GENERIC_READ);

		ConstantBufferType* CB;
		Result->Map(0, nullptr, (void**)&CB);
		*CB = ConstantBufferType();
		Result->Unmap(0, nullptr);
		return Result;
	}

	ComPtr<ID3D12Resource> CreateResource(D3D12_RESOURCE_DESC *ResourceDescription, D3D12_HEAP_PROPERTIES *HeapProperties, D3D12_RESOURCE_STATES InitialState)
	{
		ComPtr<ID3D12Resource> Result;

		{
			ScopedLock Lock(mDeviceMutex);
			VALIDATE(mDevice->CreateCommittedResource(
				HeapProperties,
				D3D12_HEAP_FLAG_NONE,
				ResourceDescription,
				InitialState,
				nullptr,
				IID_PPV_ARGS(&Result)
			));
		}
		return Result;
	}

	ComPtr<ID3D12Resource> CreateTexture(TextureData *TexData, UINT32 Components)
	{
		IVector2 Size = TexData->Size;
		D3D12_RESOURCE_DESC TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			TexData->Format,
			Size.x, Size.y, 1, 1
		);

		ComPtr<ID3D12Resource> Result = CreateResource(&TextureDesc, &Render::Context::DefaultHeapProps, D3D12_RESOURCE_STATE_COPY_DEST);

		UINT64 UploadBufferSize = GetRequiredIntermediateSize(Result.Get(), 0, 1);
		ComPtr<ID3D12Resource> TextureUploadBuffer = CreateBuffer(UploadBufferSize, true);
		mUploadBuffers.push_back(TextureUploadBuffer);

		D3D12_SUBRESOURCE_DATA SrcData = {};
		SrcData.pData = TexData->Data.data();
		SrcData.RowPitch = Size.x * Components;
		SrcData.SlicePitch = Size.x * Size.y * Components;

		UpdateSubresources<1>(UploadCommandList.Get(), Result.Get(), TextureUploadBuffer.Get(), 0, 0, 1, &SrcData);

		D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
											Result.Get(),
											D3D12_RESOURCE_STATE_COPY_DEST,
											D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
										);
		mUploadTransitions.push_back(Barrier);

		return Result;
	}

	ComPtr<ID3D12Fence> CreateFence(UINT64 InitialValue = 0, D3D12_FENCE_FLAGS Flags = D3D12_FENCE_FLAG_NONE)
	{
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

	void FlushUpload()
	{
		if (mUploadTransitions.empty())
		{
			return;
		}

		UploadCommandList->ResourceBarrier((UINT)mUploadTransitions.size(), mUploadTransitions.data());
		VALIDATE(UploadCommandList->Close());

		ID3D12CommandList* CommandListsForSubmission[] = { UploadCommandList.Get() };
		mGraphicsQueue->ExecuteCommandLists(ArraySize(CommandListsForSubmission), CommandListsForSubmission);
		VALIDATE(mGraphicsQueue->Signal(UploadFence.Get(), 1));

		WaitForFenceValue(UploadFence, 1, UploadWaitEvent);

		mUploadTransitions.clear();
		mUploadBuffers.clear();
		mUploadBufferAddress = NULL;
		UploadBufferOffset = 0;

		VALIDATE(UploadCommandAllocator->Reset());
		VALIDATE(UploadCommandList->Reset(UploadCommandAllocator.Get(), nullptr));
	}

	void UploadBufferData(ComPtr<ID3D12Resource>& Destination, const void* Data, UINT64 Size, D3D12_RESOURCE_STATES TargetState)
	{
		CHECK(Size <= UPLOAD_BUFFER_SIZE, "Buffer data is too large for this.");
		if (UploadBufferOffset + Size > UPLOAD_BUFFER_SIZE)
		{
			FlushUpload();
		}

		::memcpy(mUploadBufferAddress + UploadBufferOffset, Data, Size);
		UploadCommandList->CopyBufferRegion(Destination.Get(), 0, UploadBuffer.Get(), UploadBufferOffset, Size);
		UploadBufferOffset += Size;

		D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
											Destination.Get(),
											D3D12_RESOURCE_STATE_COPY_DEST,
											TargetState
										);
		mUploadTransitions.push_back(Barrier);

		Buffers[Destination->GetGPUVirtualAddress()] = MOVE(Destination);
	}

	D3D12_VERTEX_BUFFER_VIEW	CreateVertexBuffer(const void *Data, UINT64 Size, UINT64 Stride)
	{
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
		ComPtr<ID3D12Resource> Buffer = CreateBuffer(Size);

		D3D12_INDEX_BUFFER_VIEW Result;
		Result.BufferLocation = Buffer->GetGPUVirtualAddress();
		Result.SizeInBytes = UINT(Size);
		Result.Format = Format;

		UploadBufferData(Buffer, Data, Size, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		return Result;
	}

	ComPtr<ID3D12RootSignature> GuiRootSignature;
	ComPtr<ID3D12PipelineState> GuiPSO;
	void InitGUIResources(ComPtr<ID3D12Resource>& FontAtlas)
	{
		// Root signature
		CD3DX12_ROOT_PARAMETER Params[2] = {};
		CD3DX12_DESCRIPTOR_RANGE Range{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 };
		Params[0].InitAsDescriptorTable(1, &Range);
		Params[1].InitAsConstants(2, 0);

		CD3DX12_STATIC_SAMPLER_DESC Samplers[1] = {};
		Samplers[0].Init(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT);

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

		GuiRootSignature = CreateRootSignature(RootBlob);
		GuiRootSignature->SetName(L"GUI RootSignature");

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
		PSODesc.pRootSignature = GuiRootSignature.Get();
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
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
		GuiDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

		D3D12_CPU_DESCRIPTOR_HANDLE Handle = GuiDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.Format = DXGI_FORMAT_R8_UNORM;
		SRVDesc.Texture2D.MipLevels = 1;

		{
			ScopedLock Lock(mDeviceMutex);
			mDevice->CreateShaderResourceView(FontAtlas.Get(), &SRVDesc, Handle);
		}
	}

	UINT64 ImVertexHighwatermarks[BUFFER_COUNT] = {};
	UINT64 ImIndexHighwatermarks[BUFFER_COUNT] = {};
	ComPtr<ID3D12Resource> ImGuiVertexBuffers[BUFFER_COUNT];
	ComPtr<ID3D12Resource> ImGuiIndexBuffers[BUFFER_COUNT];
	void RenderGUI(ComPtr<ID3D12GraphicsCommandList> CommandList, System::Window& Window)
	{
		ImGui::EndFrame();
		ImGui::Render();

		ComPtr<ID3D12Resource>& ImGuiVertexBuffer = ImGuiVertexBuffers[mCurrentBackBufferIndex];
		ComPtr<ID3D12Resource>& ImGuiIndexBuffer = ImGuiIndexBuffers[mCurrentBackBufferIndex];
		UINT64& WatermarkVertex = ImVertexHighwatermarks[mCurrentBackBufferIndex];
		UINT64& WatermarkIndex = ImIndexHighwatermarks[mCurrentBackBufferIndex];

		ImDrawData *DrawData = ImGui::GetDrawData();

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

		CHECK(VtxOffset == VertexBufferSize, "");
		CHECK(IdxOffset == IndexBufferSize, "");

		ID3D12DescriptorHeap* DescriptorHeaps[] = {
			GuiDescriptorHeap.Get()
		};

		CommandList->SetDescriptorHeaps(ArraySize(DescriptorHeaps), DescriptorHeaps);

		CommandList->SetGraphicsRootSignature(GuiRootSignature.Get());
		CommandList->SetPipelineState(GuiPSO.Get());
		CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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

			CommandList->SetGraphicsRootDescriptorTable(0,
				GuiDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
			);
			CommandList->SetGraphicsRoot32BitConstants(1, 2, &Window.mSize, 0);
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

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandleForBackBuffer()
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			mCurrentBackBufferIndex, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);

		return rtv;
	}

	void BindDescriptors(ComPtr<ID3D12GraphicsCommandList>& CommandList)
	{
		ID3D12DescriptorHeap* DescriptorHeaps[] = {
			mSRVDescriptorHeap.Get()
		};

		CommandList->SetDescriptorHeaps(ArraySize(DescriptorHeaps), DescriptorHeaps);
		CommandList->SetGraphicsRootDescriptorTable(
			0,
			mSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
		);
	}

	void Execute(ID3D12CommandList *CmdList)
	{
		ID3D12CommandList* const CommandLists[] = { CmdList };
		mGraphicsQueue->ExecuteCommandLists(ArraySize(CommandLists), CommandLists);
	}

private:
	Mutex mDeviceMutex;

	TArray<ComPtr<ID3D12Resource>> mUploadBuffers;
	TArray<D3D12_RESOURCE_BARRIER> mUploadTransitions;

	inline static const UINT64 UPLOAD_BUFFER_SIZE = 8_mb;

	unsigned char			*mUploadBufferAddress = NULL;
	UINT64					UploadBufferOffset = NULL;
	ComPtr<ID3D12Resource>	UploadBuffer;
	ComPtr<ID3D12Fence>		UploadFence;
	HANDLE					UploadWaitEvent;

	bool mTearingSupported = false;
	UINT mSyncInterval = 1;
	UINT mPresentFlags = 0;
	ComPtr<IDXGISwapChain1>		mSwapChain;

	TMap<UINT64, ComPtr<ID3D12Resource>> Buffers;

	ComPtr<ID3D12CommandAllocator>		UploadCommandAllocator;
	ComPtr<ID3D12GraphicsCommandList>	UploadCommandList;
	ComPtr<IDXGIFactory4> mDXGIFactory;
	ComPtr<ID3D12DescriptorHeap> mRTVDescriptorHeap;

	ComPtr<ID3D12DescriptorHeap> mSRVDescriptorHeap;
	UINT mCurrentSRVIndex = 0;

	ComPtr<IDXGISwapChain1> CreateSwapChain(System::Window Window, DXGI_SWAP_CHAIN_FLAG Flags)
	{
		ComPtr<IDXGISwapChain1> Result;

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = (UINT)Window.mViewport.Width;
		swapChainDesc.Height = (UINT)Window.mViewport.Height;
		swapChainDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
		swapChainDesc.Scaling = DXGI_SCALING_NONE;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = BUFFER_COUNT;
		swapChainDesc.Flags = Flags;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

		VALIDATE(mDXGIFactory->CreateSwapChainForHwnd(mGraphicsQueue.Get(), Window.mHwnd, &swapChainDesc, nullptr, nullptr, &Result));

		return Result;
	}

	void UpdateRenderTargetViews()
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	 
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
};

}

