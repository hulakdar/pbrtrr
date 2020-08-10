#include "Util/Debug.h"
#include "Util/Util.h"
#include "Containers/ComPtr.h"
#include "Util/Allocator.h"
#include "Containers/String.h"
#include "System/Window.h"
#include "Render/Context.h"

#include "external/stb/stb_image.h"

#include <System/GUI.h>
#include <assimp/Importer.hpp>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <Containers/Queue.h>
#include <eathread/eathread.h>
#include <thread>

int main(void)
{
	TArray<Render::TextureData> ImageDatas;

	Debug::Scope DebugScopeObject;

	System::Window Window;
	Window.Init();

	Render::Context RenderContext;
	RenderContext.Init(Window);

	System::GUI Gui;
	Gui.Init(Window, RenderContext);

	HANDLE WaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	ComPtr<ID3D12CommandAllocator> CommandAllocators[3] = {};
	ComPtr<ID3D12Fence> FrameFences[3] = {};
	for (int i = 0; i < 3; ++i)
	{
		CommandAllocators[i] = RenderContext.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT);
		FrameFences[i] = RenderContext.CreateFence();

		SetD3DName(CommandAllocators[i], L"Command allocator %d", i);
		SetD3DName(FrameFences[i], L"Fence %d", i);
	}
	ComPtr<ID3D12GraphicsCommandList> CommandList = RenderContext.CreateCommandList(CommandAllocators[0], D3D12_COMMAND_LIST_TYPE_DIRECT);
	SetD3DName(CommandList, L"Command list");

	Assimp::Importer Importer;

	const aiScene *Scene = Importer.ReadFile("content/DamagedHelmet.glb", 0);
	CHECK(Scene != nullptr, "Load failed");

	struct Mesh
	{
		StringView	Name;
		Matrix		Transform;

		TArray<unsigned int> MeshIDs;
	};

	int ThreadCount = EA::Thread::GetProcessorCount();

	auto Worker = [Scene, ThreadCount](int Start) {
		for (int i = Start; i < Scene->mNumMeshes; i += ThreadCount)
		{
			aiMesh* Mesh = Scene->mMeshes[i];
		}
	};

	TArray<std::thread> Workers;

	for (int i = 0; i < ThreadCount; ++i)
	{
		Workers.emplace_back(Worker, i);
	}

	TArray<Mesh> Meshes;
	Meshes.reserve(Scene->mNumMeshes);

	TQueue<aiNode*> ProcessingQueue;
	ProcessingQueue.push(Scene->mRootNode);
	while (!ProcessingQueue.empty())
	{
		aiNode* Current = ProcessingQueue.front();
		ProcessingQueue.pop();
		for (int i = 0; i < Current->mNumChildren; ++i)
		{
			ProcessingQueue.push(Current->mChildren[i]);
		}

		TArray<unsigned int> MeshIDs;
		MeshIDs.reserve(Current->mNumMeshes);
		for (int i = 0; i < Current->mNumMeshes; ++i)
		{
			unsigned int MeshID = Current->mMeshes[i];
			MeshIDs.push_back(MeshID);
		}

		Meshes.push_back(
			Mesh {
				StringView(Current->mName.C_Str()),
				Matrix(Current->mTransformation[0]),
				MOVE(MeshIDs)
			}
		);
	}

	eastl::for_each(Workers.begin(), Workers.end(), [](std::thread& Worker) {
		Worker.join();
	});
	Workers.clear();

	ComPtr<ID3D12RootSignature> RootSignature;
	{
		CD3DX12_ROOT_PARAMETER Params[2] = {};

		CD3DX12_DESCRIPTOR_RANGE Range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);
		Params[0].InitAsDescriptorTable(1, &Range);

		Params[1].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

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

		RootSignature = RenderContext.CreateRootSignature(RootBlob);
		RootSignature->SetName(L"RootSignature");
	}

	struct ConstantBuffer
	{
		float Scale;
	};

	ComPtr<ID3D12Resource> ConstantBuffers[3];
	for (int i = 0; i < 3; ++i)
	{
		ConstantBuffers[i] = RenderContext.CreateConstantBuffer<ConstantBuffer>();
	}

	ComPtr<ID3D12PipelineState> PSO;
	{
		D3D12_INPUT_ELEMENT_DESC PSOLayout[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};

		ComPtr<ID3DBlob> VertexShader = Render::CompileShader(
			"content/shaders/Simple.hlsl",
			"MainVS", "vs_5_1"
		);

		ComPtr<ID3DBlob> PixelShader = Render::CompileShader(
			"content/shaders/Simple.hlsl",
			"MainPS", "ps_5_1"
		);

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

	D3D12_VERTEX_BUFFER_VIEW	VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW		IndexBufferView;
	ComPtr<ID3D12Resource>	Lena;
	ComPtr<ID3D12DescriptorHeap> SRVDescriptorHeap = RenderContext.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
	{
		static const float Vertices[] = {
			-1.f,  1.f, 0.f, 0.f, 0.f,
			 1.f,  1.f, 0.f, 1.f, 0.f,
			 1.f, -1.f, 0.f, 1.f, 1.f,
			-1.f, -1.f, 0.f, 0.f, 1.f,
		};

		static const short Indices[] = {
			0, 1, 2, 0, 2, 3
		};

		VertexBufferView = RenderContext.CreateVertexBuffer(Vertices, sizeof(Vertices), sizeof(float) * 5);
		IndexBufferView = RenderContext.CreateIndexBuffer(Indices, sizeof(Indices), DXGI_FORMAT_R16_UINT);

		Render::TextureData LenaTexData;
		unsigned char *Data = stbi_load("lena_std.tga", &LenaTexData.Size.x, &LenaTexData.Size.y, nullptr, 4);
		LenaTexData.Data = StringView((char *)Data, LenaTexData.Size.x * LenaTexData.Size.y * 4);
		LenaTexData.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

		Lena = RenderContext.CreateTexture(&LenaTexData, 4);
		stbi_image_free((stbi_uc*)LenaTexData.Data.data());

		{
			D3D12_CPU_DESCRIPTOR_HANDLE Handle = SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

			{
				D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
				SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				SRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
				SRVDesc.Texture2D.MipLevels = 1;
				RenderContext.mDevice->CreateShaderResourceView(Lena.Get(), &SRVDesc, Handle);
			}
		}

		RenderContext.InitGUIResources(Gui.FontTexData.Texture);
	}
	RenderContext.FlushUpload();


	UINT64 CurrentFenceValue = 0;

	UINT64 FrameFenceValues[3] = {};

    //Main message loop
	while (!glfwWindowShouldClose(Window.mHandle))
	{
		ImGui::NewFrame();

		static bool ShowDemo = true;
		ImGui::ShowDemoWindow(&ShowDemo);

		ComPtr<ID3D12Resource>& BackBuffer = RenderContext.mBackBuffers[RenderContext.mCurrentBackBufferIndex];
		ComPtr<ID3D12CommandAllocator>& CommandAllocator = CommandAllocators[RenderContext.mCurrentBackBufferIndex];
		ComPtr<ID3D12Fence>& Fence = FrameFences[RenderContext.mCurrentBackBufferIndex];

		WaitForFenceValue(Fence, FrameFenceValues[RenderContext.mCurrentBackBufferIndex], WaitEvent);
		VALIDATE(CommandAllocator->Reset());
		VALIDATE(CommandList->Reset(CommandAllocator.Get(), nullptr));

		Window.Update();

		// Clear the render target.
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				BackBuffer.Get(),
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

			CommandList->RSSetViewports(1, &Window.mViewport);
			CommandList->RSSetScissorRects(1, &Window.mScissorRect);

			D3D12_CPU_DESCRIPTOR_HANDLE rtv = RenderContext.GetRTVHandleForBackBuffer();
			CommandList->ResourceBarrier(1, &barrier);
			CommandList->OMSetRenderTargets(1, &rtv, true, nullptr);
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
				ConstantBuffer* CB;
				ConstantBuffers[RenderContext.mCurrentBackBufferIndex]->Map(0, nullptr, (void**)&CB);
				CB->Scale = (float)std::sin(glfwGetTime());
				ConstantBuffers[RenderContext.mCurrentBackBufferIndex]->Unmap(0, nullptr);

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
					ConstantBuffers[RenderContext.mCurrentBackBufferIndex]->GetGPUVirtualAddress()
				);
			}

			CommandList->SetPipelineState(PSO.Get());
			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);
			CommandList->IASetIndexBuffer(&IndexBufferView);

			CommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
		}

		RenderContext.RenderGUI(CommandList, Window);

		// Present
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				BackBuffer.Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			CommandList->ResourceBarrier(1, &barrier);

			VALIDATE(CommandList->Close());
			RenderContext.Execute(CommandList.Get());
			RenderContext.Present();
			FrameFenceValues[RenderContext.mCurrentBackBufferIndex] = Signal(RenderContext.mGraphicsQueue, Fence, CurrentFenceValue);
		}
		RenderContext.mCurrentBackBufferIndex = (RenderContext.mCurrentBackBufferIndex + 1) % 3;
	}

	CloseHandle(WaitEvent);

	glfwTerminate();
}

