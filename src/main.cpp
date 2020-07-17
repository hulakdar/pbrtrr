#include "Util/Debug.h"
#include "Util/Util.h"
#include "Containers/ComPtr.h"
#include "System/Window.h"
#include "Render/Context.h"

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb/stb_image.h"

#include <d3dcompiler.h>
#include <dxgi1_6.h>

const char ShaderCode[] = R"(
cbuffer PerFrameConstants : register (b0)
{
	float4 Scale;
}

struct VertexShaderOutput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};

VertexShaderOutput VS_main(
	float4 position : POSITION,
	float2 uv : TEXCOORD)
{
	VertexShaderOutput output;

	output.position = position;
	output.position.xy *= Scale.x;
	output.uv = uv;

	return output;
}

Texture2D<float4>    LenaStd : register(t0);
SamplerState BilinearSampler : register(s0);

float4 PS_main (float4 position : SV_POSITION,
				float2 uv : TEXCOORD) : SV_TARGET
{
	return LenaStd.Sample(BilinearSampler, uv);
}
)";

//#define TEST_WARP

void PrintError(ID3DBlob* ErrorBlob)
{
	if (ErrorBlob)
	{
		std::string_view Str((char *)ErrorBlob->GetBufferPointer(), ErrorBlob->GetBufferSize());
		Debug::Print(Str);
		DEBUG_BREAK();
	}
}

uint64_t Signal(ComPtr<ID3D12CommandQueue>& CommandQueue, ComPtr<ID3D12Fence>& Fence, uint64_t& FenceValue)
{
    uint64_t FenceValueForSignal = ++FenceValue;
    VALIDATE(CommandQueue->Signal(Fence.Get(), FenceValueForSignal));
 
    return FenceValueForSignal;
}

void WaitForFenceValue(ComPtr<ID3D12Fence>& Fence, uint64_t FenceValue, HANDLE Event)
{
    if (Fence->GetCompletedValue() < FenceValue)
    {
        VALIDATE(Fence->SetEventOnCompletion(FenceValue, Event));
		::WaitForSingleObject(Event, MAXDWORD);
    }
}

void Flush(ComPtr<ID3D12CommandQueue>& CommandQueue, ComPtr<ID3D12Fence>& Fence, uint64_t& FenceValue, HANDLE FenceEvent )
{
    uint64_t fenceValueForSignal = Signal(CommandQueue, Fence, FenceValue);
    WaitForFenceValue(Fence, fenceValueForSignal, FenceEvent);
}

int main(void)
{
	Debug::Scope DebugScopeObject;

	System::Window Window;
	Window.Init();

	Render::Context RenderContext;
	RenderContext.Init(Window);

	HANDLE WaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	ComPtr<ID3D12DescriptorHeap> SRVDescriptorHeap = RenderContext.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

	ComPtr<ID3D12CommandAllocator> CommandAllocators[3] = {};
	ComPtr<ID3D12GraphicsCommandList> CommandLists[3] = {};
	ComPtr<ID3D12Fence> FrameFences[3] = {};
	for (int i = 0; i < 3; ++i)
	{
		CommandAllocators[i] = RenderContext.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT);
		CommandLists[i] = RenderContext.CreateCommandList(CommandAllocators[i], D3D12_COMMAND_LIST_TYPE_DIRECT);
		FrameFences[i] = RenderContext.CreateFence();
	}

	ComPtr<ID3D12RootSignature> RootSignature;
	{
		CD3DX12_ROOT_PARAMETER Params[2];
		CD3DX12_DESCRIPTOR_RANGE Range{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 };
		Params[0].InitAsDescriptorTable(1, &Range);
		Params[1].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

		CD3DX12_STATIC_SAMPLER_DESC Samplers[1];
		Samplers[0].Init(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT);

		CD3DX12_ROOT_SIGNATURE_DESC DescRootSignature;

		DescRootSignature.Init(
			2, Params,
			1, Samplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);

		ComPtr<ID3DBlob> RootBlob;
		ComPtr<ID3DBlob> ErrorBlob;
		VALIDATE(
			D3D12SerializeRootSignature(
				&DescRootSignature,
				D3D_ROOT_SIGNATURE_VERSION_1,
				&RootBlob,
				&ErrorBlob
			)
		);
		PrintError(ErrorBlob.Get());

		RootSignature = RenderContext.CreateRootSignature(RootBlob);
	}

	ComPtr<ID3D12Resource> ConstantBuffers[3];
	{
		struct ConstantBuffer
		{
			float x, y, z, w;
		};

		ConstantBuffer cb = {};

		for (int i = 0; i < 3; ++i)
		{
			D3D12_HEAP_PROPERTIES UploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			D3D12_RESOURCE_DESC ConstantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ConstantBuffer));

			RenderContext.mDevice->CreateCommittedResource(
				&UploadHeapProperties,
				D3D12_HEAP_FLAG_NONE,
				&ConstantBufferDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&ConstantBuffers[i])
				);

			ConstantBuffer* P;
			ConstantBuffers[i]->Map(0, nullptr, (void**)&P);
			*P = cb;
			ConstantBuffers[i]->Unmap(0, nullptr);
		}
	}

	ComPtr<ID3D12PipelineState> PSO;
	{
		D3D12_INPUT_ELEMENT_DESC PSOLayout[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};

		ComPtr<ID3DBlob> VertexShader;
		ComPtr<ID3DBlob> PixelShader;
		{
			ComPtr<ID3DBlob> ErrorBlob;
			VALIDATE(D3DCompile(ShaderCode, sizeof(ShaderCode), "", nullptr, nullptr, "VS_main", "vs_5_0", 0, 0, &VertexShader, &ErrorBlob));
			PrintError(ErrorBlob.Get());
			VALIDATE(D3DCompile(ShaderCode, sizeof(ShaderCode), "", nullptr, nullptr, "PS_main", "ps_5_0", 0, 0, &PixelShader, &ErrorBlob));
			PrintError(ErrorBlob.Get());
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.VS.BytecodeLength = VertexShader->GetBufferSize();
		PSODesc.VS.pShaderBytecode = VertexShader->GetBufferPointer();
		PSODesc.PS.BytecodeLength = PixelShader->GetBufferSize();
		PSODesc.PS.pShaderBytecode = PixelShader->GetBufferPointer();
		PSODesc.pRootSignature = RootSignature.Get();
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
		PSODesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		PSODesc.InputLayout.NumElements = ArraySize(PSOLayout);
		PSODesc.InputLayout.pInputElementDescs = PSOLayout;
		PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		PSODesc.SampleDesc.Count = 1;
		PSODesc.DepthStencilState.DepthEnable = false;
		PSODesc.DepthStencilState.StencilEnable = false;
		PSODesc.SampleMask = 0xFFFFFFFF;
		PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		VALIDATE(RenderContext.mDevice->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&PSO)));
	}

	ComPtr<ID3D12Resource> VertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
	ComPtr<ID3D12Resource> IndexBuffer;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView;
	ComPtr<ID3D12Resource> Texture;
	{
		static const float Vertices[] = {
			-1.f,  1.f, 0.f, 0.f, 0.f,
			 1.f,  1.f, 0.f, 1.f, 0.f,
			 1.f, -1.f, 0.f, 1.f, 1.f,
			-1.f, -1.f, 0.f, 0.f, 1.f,
		};

		static const int Indices[] = {
			0, 1, 2, 2, 3, 0
		};

		int Width, Height;
		unsigned char* ImageData = stbi_load("lena_std.tga", &Width, &Height, nullptr, 4);

		D3D12_RESOURCE_DESC TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			Width, Height, 1, 1
		);

		uint32_t UploadBufferSize = sizeof(Vertices) + sizeof(Indices);
		D3D12_RESOURCE_DESC UploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(UploadBufferSize);
		D3D12_HEAP_PROPERTIES UploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

		ComPtr<ID3D12Resource> UploadBuffer;
		VALIDATE(RenderContext.mDevice->CreateCommittedResource(
			&UploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&UploadBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&UploadBuffer)
		));

		D3D12_HEAP_PROPERTIES DefaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

		D3D12_RESOURCE_DESC VertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(Vertices));
		VALIDATE(RenderContext.mDevice->CreateCommittedResource(
			&DefaultHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&VertexBufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&VertexBuffer)
		));

		D3D12_RESOURCE_DESC IndexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(Indices));
		VALIDATE(RenderContext.mDevice->CreateCommittedResource(
			&DefaultHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&IndexBufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&IndexBuffer)
		));

		VALIDATE(RenderContext.mDevice->CreateCommittedResource(
			&DefaultHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&TextureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&Texture)
		));

		VertexBufferView.BufferLocation = VertexBuffer->GetGPUVirtualAddress();
		VertexBufferView.SizeInBytes = sizeof(Vertices);
		VertexBufferView.StrideInBytes = sizeof(float) * 5;

		IndexBufferView.BufferLocation = IndexBuffer->GetGPUVirtualAddress();
		IndexBufferView.SizeInBytes = sizeof(Indices);
		IndexBufferView.Format = DXGI_FORMAT_R32_UINT;

		unsigned char* P;
		VALIDATE(UploadBuffer->Map(0, nullptr, (void**)&P));
		::memcpy(P, Vertices, sizeof(Vertices));
		P += sizeof(Vertices);
		::memcpy(P, Indices, sizeof(Indices));
		UploadBuffer->Unmap(0, nullptr);

		ComPtr<ID3D12Resource> TextureUploadBuffer;
		{
			UINT64 TextureUploadBufferSize = GetRequiredIntermediateSize(Texture.Get(), 0, 1);
			D3D12_RESOURCE_DESC TextureUploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(TextureUploadBufferSize);

			VALIDATE(RenderContext.mDevice->CreateCommittedResource(
				&UploadHeapProperties,
				D3D12_HEAP_FLAG_NONE,
				&TextureUploadBufferDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&TextureUploadBuffer)
			));
		}

		ComPtr<ID3D12Fence> UploadFence;
		ComPtr<ID3D12CommandAllocator> UploadCommandAllocator;
		ComPtr<ID3D12GraphicsCommandList> UploadCommandList;
		{
			VALIDATE(RenderContext.mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&UploadFence)));
			UploadCommandAllocator = RenderContext.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT);
			VALIDATE(RenderContext.mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, UploadCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&UploadCommandList)));
		}

		D3D12_SUBRESOURCE_DATA SrcData = {};
		SrcData.pData = ImageData;
		SrcData.RowPitch = Width * 4;
		SrcData.SlicePitch = Width * Height * 4;
		UpdateSubresources<1>(UploadCommandList.Get(), Texture.Get(), TextureUploadBuffer.Get(), 0, 0, 1, &SrcData);

		UploadCommandList->CopyBufferRegion(VertexBuffer.Get(), 0, UploadBuffer.Get(), 0, sizeof(Vertices));
		UploadCommandList->CopyBufferRegion(IndexBuffer.Get(), 0, UploadBuffer.Get(), sizeof(Vertices), sizeof(Indices));

		const CD3DX12_RESOURCE_BARRIER Barriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(Texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(VertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
			CD3DX12_RESOURCE_BARRIER::Transition(IndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER)
		};

		UploadCommandList->ResourceBarrier(ArraySize(Barriers), Barriers);

		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		SRVDesc.Texture2D.MipLevels = 1;

		RenderContext.mDevice->CreateShaderResourceView(Texture.Get(), &SRVDesc, SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		VALIDATE(UploadCommandList->Close());
		ID3D12CommandList* CommandListsForSubmission[] = { UploadCommandList.Get() };
		RenderContext.mGraphicsQueue->ExecuteCommandLists(ArraySize(CommandListsForSubmission), CommandListsForSubmission);
		VALIDATE(RenderContext.mGraphicsQueue->Signal(UploadFence.Get(), 1));

		WaitForFenceValue(UploadFence, 1, WaitEvent);
		VALIDATE(UploadCommandAllocator->Reset());
	}

	UINT CurrentBackBufferIndex = 0;
	UINT64 CurrentFenceValue = 0;

	UINT64 FrameFenceValues[3] = {};
    //Main message loop
	while (!glfwWindowShouldClose(Window.mHandle))
	{
		ComPtr<ID3D12Resource>& BackBuffer = RenderContext.BackBuffers[CurrentBackBufferIndex];
		ComPtr<ID3D12CommandAllocator>& CommandAllocator = CommandAllocators[CurrentBackBufferIndex];
		ComPtr<ID3D12GraphicsCommandList>& CommandList = CommandLists[CurrentBackBufferIndex];
		ComPtr<ID3D12Fence>& Fence = FrameFences[CurrentBackBufferIndex];

		VALIDATE(CommandAllocator->Reset());
		VALIDATE(CommandList->Reset(CommandAllocator.Get(), nullptr));

		Flush(RenderContext.mGraphicsQueue, Fence, CurrentFenceValue, WaitEvent);
		Window.Update();

		// Clear the render target.
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				BackBuffer.Get(),
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

			CommandList->ResourceBarrier(1, &barrier);


			D3D12_CPU_DESCRIPTOR_HANDLE rtv = RenderContext.GetRTVHandleForBackBuffer(CurrentBackBufferIndex);

			CommandList->OMSetRenderTargets(1, &rtv, true, nullptr);
			CommandList->RSSetViewports(1, &Window.mViewport);
			CommandList->RSSetScissorRects(1, &Window.mScissorRect);
			{
				FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
				CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
			}
		}

		// QUAD
		{
			CommandList->SetGraphicsRootSignature(RootSignature.Get());
			// UPDATE CB AND ROOT SIGNATURE
			{
				float* P;
				ConstantBuffers[CurrentBackBufferIndex]->Map(0, nullptr, (void**)&P);
				*P = (float)std::abs(std::sin(glfwGetTime()));
				ConstantBuffers[CurrentBackBufferIndex]->Unmap(0, nullptr);

				ID3D12DescriptorHeap* DescriptorHeaps[] = {
					SRVDescriptorHeap.Get()
				};

				CommandList->SetDescriptorHeaps(ArraySize(DescriptorHeaps), DescriptorHeaps);
				CommandList->SetGraphicsRootDescriptorTable(
					0,
					SRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
				);
				CommandList->SetGraphicsRootConstantBufferView(
					1,
					ConstantBuffers[CurrentBackBufferIndex]->GetGPUVirtualAddress()
				);
			}

			CommandList->SetPipelineState(PSO.Get());
			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);
			CommandList->IASetIndexBuffer(&IndexBufferView);

			CommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
		}

		// Present
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				BackBuffer.Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			CommandList->ResourceBarrier(1, &barrier);

			VALIDATE(CommandList->Close());
			RenderContext.Execute(CommandList.Get());
			RenderContext.Present();
			FrameFenceValues[CurrentBackBufferIndex] = Signal(RenderContext.mGraphicsQueue, Fence, CurrentFenceValue);
		}
		CurrentBackBufferIndex = (CurrentBackBufferIndex + 1) % 3;
	}

	Flush(RenderContext.mGraphicsQueue, FrameFences[CurrentBackBufferIndex], FrameFenceValues[CurrentBackBufferIndex], WaitEvent);

	CloseHandle(WaitEvent);

	glfwTerminate();
}

