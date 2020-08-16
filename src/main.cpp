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
#include <assimp/postprocess.h>
#include <Containers/Queue.h>
#include <eathread/eathread.h>
#include <thread>
#include <tbb/parallel_for.h>
#include <tbb/task_group.h>
#include <atomic>

struct MeshData
{
	ComPtr<ID3D12Resource>		VertexBuffer;
	ComPtr<ID3D12Resource>		IndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW	VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW		IndexBufferView;
};

MeshData AllocateMeshData(aiMesh* Mesh, Render::Context& RenderContext)
{
	MeshData Result;
	CHECK_RETURN(Mesh->HasFaces(), "Mesh without faces?", Result);

	UINT64 VertexSize = sizeof(aiVector3D);
	UINT64 UVSets = 0;
	UINT64 VertexColors = 0;

	if (Mesh->HasNormals())
	{
		VertexSize += sizeof(aiVector3D);
	}

	while (Mesh->HasVertexColors(VertexColors))
	{
		VertexSize += 4;
		VertexColors++;
	}

	while (Mesh->HasTextureCoords(UVSets))
	{
		VertexSize += sizeof(float) * Mesh->mNumUVComponents[UVSets];
		UVSets++;
	}

	UINT64 VertexBufferSize = VertexSize * Mesh->mNumVertices;
	UINT IndexBufferSize = Mesh->mNumFaces * 3 * sizeof(unsigned int);

	CHECK(VertexBufferSize != 0, "??");
	CHECK(IndexBufferSize != 0, "??");

	Result.VertexBuffer = RenderContext.CreateBuffer(VertexBufferSize);
	Result.IndexBuffer = RenderContext.CreateBuffer(IndexBufferSize);

	Result.VertexBufferView.BufferLocation = Result.VertexBuffer->GetGPUVirtualAddress();
	Result.VertexBufferView.SizeInBytes = VertexBufferSize;
	Result.VertexBufferView.StrideInBytes = VertexSize;

	Result.IndexBufferView.BufferLocation = Result.IndexBuffer->GetGPUVirtualAddress();
	Result.IndexBufferView.SizeInBytes = IndexBufferSize;
	Result.IndexBufferView.Format = DXGI_FORMAT_R32_UINT;

	return Result;
}

void UploadMeshData(unsigned char* CpuPtr, aiMesh* Mesh)
{
	for (int i = 0; i < Mesh->mNumVertices; ++i)
	{
		unsigned char* Start = CpuPtr;
		{
			aiVector3D* Position = (aiVector3D*)CpuPtr;
			*Position = Mesh->mVertices[i];
			CpuPtr += sizeof(aiVector3D);
		}

		if (Mesh->mNormals)
		{
			aiVector3D* Normal = (aiVector3D*)CpuPtr;
			*Normal = Mesh->mNormals[i];
			CpuPtr += sizeof(aiVector3D);
		}

		for (int j = 0; Mesh->mColors[j]; ++j)
		{
			aiColor4D* Color = (aiColor4D*)CpuPtr;
			*Color = Mesh->mColors[j][i];
			CpuPtr += sizeof(aiColor4D);
		}

		for (int j = 0; Mesh->mTextureCoords[j]; ++j)
		{
			if (Mesh->mNumUVComponents[j] == 2)
			{
				aiVector2D* UV = (aiVector2D*)CpuPtr;
				*UV = aiVector2D(Mesh->mTextureCoords[j][i].x, Mesh->mTextureCoords[j][i].y);
				CpuPtr += sizeof(aiVector2D);
			}
			else if (Mesh->mNumUVComponents[j] == 3)
			{
				aiVector3D* UV = (aiVector3D*)CpuPtr;
				*UV = Mesh->mTextureCoords[j][i];
				CpuPtr += sizeof(aiVector3D);
			}
		}
	}

	for (int i = 0; i < Mesh->mNumFaces; ++i)
	{
		::memcpy(CpuPtr, Mesh->mFaces[i].mIndices, 3 * sizeof(unsigned int));
		CpuPtr += 3 * sizeof(unsigned int);
	}
}

int main(void)
{
	Debug::Scope DebugScopeObject;

	System::Window Window;
	Window.Init();

	Render::Context RenderContext;
	RenderContext.Init(Window);

	System::GUI Gui;
	Gui.Init(Window, RenderContext);

	Assimp::Importer Importer;


	struct Mesh
	{
		Matrix	Transform;
		TArray<UINT> MeshIDs;
	};
	TArray<MeshData> MeshDatas;
	TArray<Mesh> Meshes;

	std::atomic<UINT> Counter(0);

	tbb::task_group TaskGroup;
	TaskGroup.run([&]()
	{
		const aiScene* Scene = nullptr;
		{
			ZoneScopedN("Scene file parsing");

			Scene = Importer.ReadFile(
				"content/SunTemple/SunTemple.fbx",
				aiProcess_ConvertToLeftHanded | aiProcessPreset_TargetRealtime_MaxQuality
			);
			CHECK_RETURN(Scene != nullptr, "Load failed", 0);
		}

		{
			TArray<UINT> UploadOffsets;
			ComPtr<ID3D12Resource> UploadBuffer;
			{
				ZoneScopedN("Upload to GPU-visible memory");
				UploadOffsets.resize(Scene->mNumMeshes);
				MeshDatas.resize(Scene->mNumMeshes);
				UINT UploadBufferSize = 0;
				for (unsigned int i = 0; i < Scene->mNumMeshes; ++i)
				{
					aiMesh* Mesh = Scene->mMeshes[i];
					MeshData Tmp = AllocateMeshData(Mesh, RenderContext);
					UploadOffsets[i] = UploadBufferSize;
					UploadBufferSize += Tmp.IndexBufferView.SizeInBytes + Tmp.VertexBufferView.SizeInBytes;
					MeshDatas[i] = Tmp;
				}

				UploadBuffer = RenderContext.CreateBuffer(UploadBufferSize, true);

				unsigned char* CpuPtr = NULL;
				UploadBuffer->Map(0, NULL, (void**)&CpuPtr);

				using namespace tbb;

				parallel_for(
					blocked_range(0U, Scene->mNumMeshes),
					[Scene, CpuPtr, &UploadOffsets](blocked_range<unsigned int>& range)
					{
						ZoneScopedN("Upload parallel for")
						for (unsigned int i = range.begin(); i < range.end(); ++i)
						{
							aiMesh* Mesh = Scene->mMeshes[i];
							UploadMeshData(CpuPtr + UploadOffsets[i], Mesh);
						}
					}
				);

				UploadBuffer->Unmap(0, NULL);
			}

			TQueue<aiNode*> ProcessingQueue;
			ProcessingQueue.push(Scene->mRootNode);

			while (!ProcessingQueue.empty())
			{
				ZoneScopedN("Pump mesh queue")
				aiNode* Current = ProcessingQueue.front();
				ProcessingQueue.pop();
				for (int i = 0; i < Current->mNumChildren; ++i)
				{
					ProcessingQueue.push(Current->mChildren[i]);
				}

				TArray<UINT> MeshIDs;
				MeshIDs.reserve(Current->mNumMeshes);
				for (int i = 0; i < Current->mNumMeshes; ++i)
				{
					unsigned int MeshID = Current->mMeshes[i];
					MeshIDs.push_back(MeshID);
				}

				Meshes.push_back(
					Mesh
					{
						Matrix(Current->mTransformation[0]),
						MOVE(MeshIDs)
					}
				);
			}

			ComPtr<ID3D12CommandAllocator> WorkerCommandAllocator = RenderContext.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY);
			ComPtr<ID3D12GraphicsCommandList> WorkerCommandList = RenderContext.CreateCommandList(WorkerCommandAllocator, D3D12_COMMAND_LIST_TYPE_COPY);
			WorkerCommandList->Reset(WorkerCommandAllocator.Get(), nullptr);

			{
				TracyD3D12Zone(RenderContext.mCopyProfilingCtx, WorkerCommandList.Get(), "Copy Mesh Data from to GPU");
				{
					ZoneScopedN("Fill command list for upload")
					for (int i = 0; i < Scene->mNumMeshes; ++i)
					{
						MeshData& Data = MeshDatas[i];
						WorkerCommandList->CopyBufferRegion(Data.VertexBuffer.Get(), 0, UploadBuffer.Get(), UploadOffsets[i], Data.VertexBufferView.SizeInBytes);
						WorkerCommandList->CopyBufferRegion(Data.IndexBuffer.Get(), 0, UploadBuffer.Get(), UploadOffsets[i] + Data.VertexBufferView.SizeInBytes, Data.IndexBufferView.SizeInBytes);
					}
				}
			}

			WorkerCommandList->Close();
			RenderContext.ExecuteCopy(WorkerCommandList.Get());

			ComPtr<ID3D12Fence> WorkerFrameFence = RenderContext.CreateFence();
			VALIDATE(RenderContext.mCopyQueue->Signal(WorkerFrameFence.Get(), 1));
			HANDLE WorkerWaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

			WaitForFenceValue(WorkerFrameFence, 1, WorkerWaitEvent);
			CloseHandle(WorkerWaitEvent);
			Counter.fetch_add(1);
		}
		return 0;
	});

	ComPtr<ID3D12PipelineState> PSO;
	Render::TextureData Lena;

	TaskGroup.run([&]()
	{
		ZoneScopedN("Create PSO")

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
			PSODesc.pRootSignature = RenderContext.mRootSignature.Get();
			PSODesc.NumRenderTargets = 1;
			PSODesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
			PSODesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			PSODesc.InputLayout.NumElements = ArraySize(PSOLayout);
			PSODesc.InputLayout.pInputElementDescs = PSOLayout;
			PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			PSODesc.SampleDesc.Count = 1;
			PSODesc.DepthStencilState.DepthEnable = true;
			PSODesc.DepthStencilState.StencilEnable = false;
			PSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			PSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
			PSODesc.SampleMask = 0xFFFFFFFF;
			PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

			PSO = RenderContext.CreatePSO(&PSODesc);
		}

		{
			unsigned char *Data = stbi_load("lena_std.tga", &Lena.Size.x, &Lena.Size.y, nullptr, 4);
			Lena.Data = StringView((char *)Data, Lena.Size.x * Lena.Size.y * 4);
			Lena.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

			RenderContext.CreateTexture(Lena, 4);
			RenderContext.CreateSRV(Lena);
			stbi_image_free((stbi_uc*)Lena.Data.data());
		}
		Counter.fetch_add(1);
		return 0;
	});

	RenderContext.InitGUIResources();
	RenderContext.FlushUpload();

	UINT64 CurrentFenceValue = 0;
	UINT64 FrameFenceValues[3] = {};

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

	while (!glfwWindowShouldClose(Window.mHandle))
	{
		FrameMark;

		if (Counter == 2)
		{
			Counter.fetch_add(1);
			TaskGroup.run(
				[&RenderContext, &Counter]()
				{
					RenderContext.FlushUpload();
					Counter.fetch_add(1);
				}
			);
		}

		ImGui::NewFrame();

		static bool ShowDemo = true;
		ImGui::ShowDemoWindow(&ShowDemo);

		ComPtr<ID3D12Resource>& BackBuffer = RenderContext.mBackBuffers[RenderContext.mCurrentBackBufferIndex];
		ComPtr<ID3D12CommandAllocator>& CommandAllocator = CommandAllocators[RenderContext.mCurrentBackBufferIndex];
		ComPtr<ID3D12Fence>& Fence = FrameFences[RenderContext.mCurrentBackBufferIndex];

		WaitForFenceValue(Fence, FrameFenceValues[RenderContext.mCurrentBackBufferIndex], WaitEvent);

		VALIDATE(CommandAllocator->Reset());
		VALIDATE(CommandList->Reset(CommandAllocator.Get(), nullptr));

		TracyD3D12NewFrame(RenderContext.mGraphicsProfilingCtx);

		Window.Update();

		// Clear the render target.
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				BackBuffer.Get(),
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

			CommandList->RSSetViewports(1, &Window.mViewport);
			CommandList->RSSetScissorRects(1, &Window.mScissorRect);

			D3D12_CPU_DESCRIPTOR_HANDLE rtv = RenderContext.GetRTVHandleForBackBuffer();
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = RenderContext.GetDSVHandle();
			CommandList->ResourceBarrier(1, &barrier);
			CommandList->OMSetRenderTargets(1, &rtv, true, &dsv);
			{
				FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
				CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
			}

			CommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}

		// Mesh
		if (Counter == 4)
		{
			TracyD3D12Zone(RenderContext.mGraphicsProfilingCtx, CommandList.Get(), "Render Meshes");

			ZoneScopedN("Drawing meshes");

			ImGui::Text("Draws mesh");
			CommandList->SetGraphicsRootSignature(RenderContext.mRootSignature.Get());

			RenderContext.BindDescriptors(CommandList);
			CommandList->SetPipelineState(PSO.Get());
			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			Matrix World = Matrix::CreateLookAt(Vector3(100, 100, 100), Vector3(0,0,0), Vector3(0,0,1));
			for (auto& Mesh : Meshes)
			{
				Matrix Combined = Matrix::CreatePerspective(Window.mSize.x, Window.mSize.y, 1, 10000) * World * Mesh.Transform;
				CommandList->SetGraphicsRoot32BitConstants(1, 2, &Combined, 0);
				for (UINT ID : Mesh.MeshIDs)
				{
					auto& MeshData = MeshDatas[ID];

					auto Desc = MeshData.VertexBuffer->GetDesc();
					CommandList->IASetVertexBuffers(0, 1, &MeshData.VertexBufferView);
					CommandList->IASetIndexBuffer(&MeshData.IndexBufferView);

					CommandList->DrawIndexedInstanced(MeshData.IndexBufferView.SizeInBytes / 4, 1, 0, 0, 0);
				}
			}
		}

		RenderContext.RenderGUI(CommandList, Window, Gui.FontTexData);

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

	TaskGroup.wait();
	Flush(RenderContext.mGraphicsQueue, FrameFences[RenderContext.mCurrentBackBufferIndex], CurrentFenceValue, WaitEvent);
	CloseHandle(WaitEvent);
}

