#include "Util/Debug.h"
#include "Util/Util.h"
#include "Containers/ComPtr.h"
#include "Util/Allocator.h"
#include "Containers/String.h"
#include "Containers/Queue.h"
#include "System/Window.h"
#include "System/Thread.h"
#include "Render/Context.h"
#include "Threading/Worker.h"

#include "external/stb/stb_image.h"

#include <System/GUI.h>
#include <assimp/Importer.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <atomic>
#include <condition_variable>

struct Camera
{
	float Fov = FLT_MAX;
	float Near = FLT_MAX;
	float Far = FLT_MAX;
	Vector3 Eye;
	Vector2 Angles;
};

struct MeshData
{
	ComPtr<ID3D12Resource>	VertexBuffer;
	ComPtr<ID3D12Resource>	IndexBuffer;
	uint32_t				VertexBufferSize = UINT_MAX;
	uint32_t				IndexBufferSize = UINT_MAX;
	uint32_t				VertexSize = UINT_MAX;
};

struct Material
{
	String Name;
	TArray<uint32_t> DiffuseTextures;
};

TextureData ParseTexture(aiTexture* Texture)
{
	TextureData Result;
	uint8_t* Data = nullptr;

	if (Texture->mHeight == 0)
	{
		int Channels = 0;
		Data = stbi_load_from_memory(
			(uint8_t*)Texture->pcData,
			Texture->mWidth,
			&Result.Size.x, &Result.Size.y,
			&Channels, 4);
		Result.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	}
	else
	{
		DEBUG_BREAK();
	}

	Result.RawData = Data;
	return Result;
}

void AllocateMeshData(aiMesh* Mesh, MeshData& Result)
{
	CHECK(Mesh->HasFaces(), "Mesh without faces?");

	UINT VertexSize = sizeof(aiVector3D);

	if (Mesh->HasNormals())
	{
		VertexSize += sizeof(aiVector3D);
	}

	UINT VertexColors = 0;
	while (Mesh->HasVertexColors(VertexColors))
	{
		VertexSize += 4;
		VertexColors++;
	}

	UINT UVSets = 0;
	while (Mesh->HasTextureCoords(UVSets))
	{
		VertexSize += sizeof(float) * Mesh->mNumUVComponents[UVSets];
		UVSets++;
	}

	UINT VertexBufferSize = VertexSize * Mesh->mNumVertices;
	UINT IndexBufferSize = Mesh->mNumFaces * 3 * sizeof(unsigned int);

	CHECK(VertexBufferSize != 0, "??");
	CHECK(IndexBufferSize != 0, "??");

	Result.VertexBuffer = GetRenderContext().CreateBuffer(VertexBufferSize);
	Result.IndexBuffer = GetRenderContext().CreateBuffer(IndexBufferSize);
	Result.VertexBufferSize = VertexBufferSize;
	Result.IndexBufferSize = IndexBufferSize;
	Result.VertexSize = VertexSize;
}

void UploadMeshData(unsigned char* CpuPtr, aiMesh* Mesh)
{
	for (UINT i = 0; i < Mesh->mNumVertices; ++i)
	{
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

	for (UINT i = 0; i < Mesh->mNumFaces; ++i)
	{
		CHECK(Mesh->mFaces[i].mNumIndices == 3, "Not triangles?");
		::memcpy(CpuPtr, Mesh->mFaces[i].mIndices, 3 * sizeof(unsigned int));
		CpuPtr += 3 * sizeof(unsigned int);
	}
}

class ProgressBarHelper : public Assimp::ProgressHandler
{
public:
	std::atomic<bool>	ShouldProceed = true;
	std::atomic<float>	Progress = 0;

	// Inherited via ProgressHandler
	virtual bool Update(float percentage = (-1.0F))
	{
		Progress = percentage;
		return ShouldProceed;
	}
};

#include "Render/RenderThread.h"

#include <assimp/version.h>

int main(void)
{
	StartDebugSystem();

	gMainThreadID = CurrentThreadID();
	StartWorkerThreads();

	System::Window Window;
	Window.Init();

	StartRenderThread(Window);

	System::GUI Gui;
	Gui.Init(Window);

	Assimp::Importer Importer;
	ProgressBarHelper Helper;
	Importer.SetProgressHandler(&Helper);

	struct Mesh
	{
		String Name;
		Matrix4 Transform;
		TArray<UINT> MeshIDs;
	};

	Camera MainCamera;
	MainCamera.Fov = 60;
	MainCamera.Near = 0.1;
	MainCamera.Far = 1000000;
	MainCamera.Eye = Vector3{ 0, 0, 2 };
	MainCamera.Angles = Vector2{ 0, 0 };

	TArray<Mesh> Meshes;
	TArray<Material> Materials;
	TArray<MeshData> MeshDatas;
	TArray<TextureData> Textures;

	{
		const aiScene* Scene = nullptr;
		{
			ZoneScopedN("Scene file parsing");

			Scene = Importer.ReadFile(
#if 0
				"content/DamagedHelmet.glb",
#else
				"content/Bistro/BistroExterior.fbx",
#endif
				aiProcess_FlipWindingOrder | aiProcessPreset_TargetRealtime_Quality
			);
			if (Helper.ShouldProceed == false)
			{
				return 0;
			}
			CHECK_RETURN(Scene != nullptr, "Load failed", 0);
		}

		if (false && Scene->HasMaterials())
		{
			Materials.resize(Scene->mNumMaterials);
			for (UINT i = 0; i < Scene->mNumMaterials; ++i)
			{
				aiMaterial* MaterialPtr = Scene->mMaterials[i];
				Material& Tmp = Materials[i];
				Tmp.Name = String(MaterialPtr->GetName().C_Str());
				for (UINT j = 0; j < MaterialPtr->GetTextureCount(aiTextureType_DIFFUSE); ++j)
				{
					aiString TexPath;
					MaterialPtr->GetTexture(aiTextureType_DIFFUSE, j, &TexPath);
					if (TexPath.C_Str()[0] == '*')
					{
						uint32_t Index = atoi(TexPath.C_Str() + 1);
						Tmp.DiffuseTextures.push_back(Index);
					}
					else
					{
						//DEBUG_BREAK();
					}
				}
			}
			if (Scene->HasTextures())
			{
				{
					for (UINT i = 0; i < Scene->mNumTextures; ++i)
					{
						aiTexture* Texture = Scene->mTextures[i];
						Textures.push_back(ParseTexture(Texture));
					}
				}
			}
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
					MeshData& Tmp = MeshDatas[i];
					AllocateMeshData(Mesh, Tmp);
					//Tmp.MaterialIndex = Mesh->mMaterialIndex;

					UploadOffsets[i] = UploadBufferSize;
					UploadBufferSize += Tmp.IndexBufferSize + Tmp.VertexBufferSize;
				}

				UploadBuffer = GetRenderContext().CreateBuffer(UploadBufferSize, true);

				{
					ZoneScopedN("Upload")

					unsigned char* CpuPtr = NULL;
					UploadBuffer->Map(0, NULL, (void**)&CpuPtr);
					for (UINT i = 0; i < Scene->mNumMeshes; ++i)
					{
						aiMesh* Mesh = Scene->mMeshes[i];
						UploadMeshData(CpuPtr + UploadOffsets[i], Mesh);
					}
					UploadBuffer->Unmap(0, NULL);
				}

			}

			{
				using namespace std;

				TQueue<pair<aiNode*, aiMatrix4x4>> ProcessingQueue;
				ProcessingQueue.emplace(Scene->mRootNode, aiMatrix4x4());

				Meshes.reserve(Scene->mNumMeshes);
				while (!ProcessingQueue.empty())
				{
					ZoneScopedN("Pump mesh queue")
					aiNode* Current = ProcessingQueue.front().first;
					aiMatrix4x4 ParentTransform = ProcessingQueue.front().second;
					ProcessingQueue.pop();

					aiMatrix4x4 CurrentTransform = ParentTransform * Current->mTransformation;
					for (UINT i = 0; i < Current->mNumChildren; ++i)
					{
						ProcessingQueue.emplace(Current->mChildren[i], CurrentTransform);
					}

					if (Current->mNumMeshes > 0)
					{
						TArray<UINT> MeshIDs;
						MeshIDs.reserve(Current->mNumMeshes);
						for (UINT i = 0; i < Current->mNumMeshes; ++i)
						{
							unsigned int MeshID = Current->mMeshes[i];
							MeshIDs.push_back(MeshID);
						}

						CurrentTransform.Transpose();
						Meshes.push_back(
							Mesh
							{
								String(Current->mName.C_Str()),
								Matrix4(CurrentTransform[0]),
								MOVE(MeshIDs)
							}
						);
					}
					else if (strcmp(Current->mName.C_Str(), "Camera") == 0)
					{
						aiVector3D Location, Scale, Rotation;
						CurrentTransform.Decompose(Scale, Rotation, Location);
						MainCamera.Eye = Vector3{ Location.x, Location.y, Location.z };
					}
				}
			}

			ComPtr<ID3D12CommandAllocator> WorkerCommandAllocator = GetRenderContext().CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY);
			ComPtr<ID3D12GraphicsCommandList> WorkerCommandList = GetRenderContext().CreateCommandList(WorkerCommandAllocator, D3D12_COMMAND_LIST_TYPE_COPY);
			WorkerCommandList->Reset(WorkerCommandAllocator.Get(), nullptr);

			{
				TracyD3D12Zone(GetRenderContext().mCopyProfilingCtx, WorkerCommandList.Get(), "Copy Mesh Data to GPU");
				{
					ZoneScopedN("Fill command list for upload")
					for (UINT i = 0; i < Scene->mNumMeshes; ++i)
					{
						MeshData& Data = MeshDatas[i];
						WorkerCommandList->CopyBufferRegion(Data.VertexBuffer.Get(), 0, UploadBuffer.Get(), UploadOffsets[i], Data.VertexBufferSize);
						WorkerCommandList->CopyBufferRegion(Data.IndexBuffer.Get(), 0, UploadBuffer.Get(), UploadOffsets[i] + Data.VertexBufferSize, Data.IndexBufferSize);
					}
				}
			}

			WorkerCommandList->Close();
			UINT64 CurrentFenceValue = 0;

			ComPtr<ID3D12Fence> WorkerFrameFence = GetRenderContext().CreateFence();
			CurrentFenceValue = GetRenderContext().ExecuteCopy(WorkerCommandList.Get(), WorkerFrameFence, CurrentFenceValue);
			HANDLE WorkerWaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

			WaitForFenceValue(WorkerFrameFence, 1, WorkerWaitEvent);
			CloseHandle(WorkerWaitEvent);
		}
	}

	RenderContext& Context = GetRenderContext();

	ComPtr<ID3D12PipelineState> SimplePSO;
	{
		TArray<D3D12_INPUT_ELEMENT_DESC> PSOLayout;

		D3D12_INPUT_ELEMENT_DESC Tmp = {};

		Tmp.SemanticName = "POSITION";
		Tmp.SemanticIndex = 0;
		Tmp.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		Tmp.InputSlot = 0;
		Tmp.AlignedByteOffset = 0;
		PSOLayout.push_back(Tmp);

		Tmp.SemanticName = "TEXCOORD";
		Tmp.SemanticIndex = 0;
		Tmp.Format = DXGI_FORMAT_R32G32_FLOAT;
		Tmp.InputSlot = 0;
		Tmp.AlignedByteOffset = 12;
		PSOLayout.push_back(Tmp);

		DXGI_FORMAT RenderTargetFormat = RenderContext::SCENE_COLOR_FORMAT;
		TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
		SimplePSO = Context.CreateShaderCombination(
			PSOLayout,
			EntryPoints,
			"content/shaders/Simple.hlsl",
			&RenderTargetFormat,
			DXGI_FORMAT_D24_UNORM_S8_UINT
		);
	}

	ComPtr<ID3D12PipelineState> BlitPSO;
	{
		D3D12_INPUT_ELEMENT_DESC InputDesc = {};
		InputDesc.SemanticName = "POSITION";
		InputDesc.SemanticIndex = 0;
		InputDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		InputDesc.InputSlot = 0;
		InputDesc.AlignedByteOffset = 0;

		DXGI_FORMAT RenderTargetFormat = RenderContext::BACK_BUFFER_FORMAT;
		TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
		BlitPSO = Context.CreateShaderCombination(&InputDesc, EntryPoints, "content/shaders/Blit.hlsl", &RenderTargetFormat);
	}

	ComPtr<ID3D12PipelineState> DownsampleRasterPSO;
	{
		D3D12_INPUT_ELEMENT_DESC InputDesc = {};

		InputDesc.SemanticName = "POSITION";
		InputDesc.SemanticIndex = 0;
		InputDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		InputDesc.InputSlot = 0;
		InputDesc.AlignedByteOffset = 0;

		TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
		DXGI_FORMAT RenderTargetFormat = RenderContext::READBACK_FORMAT;
		DownsampleRasterPSO = Context.CreateShaderCombination(
			&InputDesc, 
			EntryPoints,
			"content/shaders/Blit.hlsl",
			&RenderTargetFormat
		);
	}

	TextureData DefaultTexture;
	{
		uint8_t *Data = stbi_load("content/uvcheck.jpg", &DefaultTexture.Size.x, &DefaultTexture.Size.y, nullptr, 4);
		DefaultTexture.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

		Context.CreateTexture(DefaultTexture);
		Context.UploadTextureData(DefaultTexture, Data);
		Context.CreateSRV(DefaultTexture);
		stbi_image_free(Data);
	}

	MeshData Quad;
	{
		uint16_t IndexData[] = {
			0, 1, 2,
		};

		Vector2 VertexData[] = {
			{-1, -3},
			{-1, 1},
			{3, 1},
		};

		Quad.IndexBuffer = Context.CreateBuffer(
			sizeof(IndexData)
		);
		Quad.IndexBufferSize = sizeof(IndexData);
		SetD3DName(Quad.IndexBuffer, L"Quad IndexBuffer");

		Context.CreateIndexBufferView(
			Quad.IndexBuffer,
			IndexData,
			sizeof(IndexData),
			DXGI_FORMAT_R16_UINT
		);

		Quad.VertexBuffer = Context.CreateBuffer(
			sizeof(VertexData)
		);
		Quad.VertexBufferSize = sizeof(VertexData);

		Quad.VertexSize = sizeof(Vector2);
		SetD3DName(Quad.VertexBuffer, L"Quad VertexBuffer");
		Context.CreateVertexBufferView(
			Quad.VertexBuffer,
			VertexData,
			sizeof(VertexData),
			sizeof(Vector2)
		);
	}

	Context.FlushUpload();

	HANDLE WaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	ComPtr<ID3D12CommandAllocator> CommandAllocators[3] = {};
	ComPtr<ID3D12Fence> FrameFences[3] = {};
	for (int i = 0; i < 3; ++i)
	{
		CommandAllocators[i] = Context.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT);
		FrameFences[i] = Context.CreateFence();

		SetD3DName(CommandAllocators[i], L"Command allocator %d", i);
		SetD3DName(FrameFences[i], L"Fence %d", i);
	}
	ComPtr<ID3D12GraphicsCommandList> CommandList = Context.CreateCommandList(CommandAllocators[0], D3D12_COMMAND_LIST_TYPE_DIRECT);
	SetD3DName(CommandList, L"Command list");

	std::atomic<bool> ReadbackInflight;
	std::atomic<bool> WorkerInProgress;
	UINT64  ReadBackReadyFence = 0;

	TextureData SceneColor = {};
	{
		SceneColor.Format = RenderContext::SCENE_COLOR_FORMAT;
		SceneColor.Size = Window.mSize;

		D3D12_CLEAR_VALUE ClearValue = {};
		FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
		memcpy(ClearValue.Color, clearColor, sizeof(clearColor));
		ClearValue.Format = RenderContext::SCENE_COLOR_FORMAT;

		Context.CreateTexture(SceneColor,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&ClearValue
		);
		Context.CreateSRV(SceneColor);
		Context.CreateRTV(SceneColor);
	}

	TextureData DepthBuffer;
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

		DepthBuffer.Format = TextureDesc.Format;
		DepthBuffer.Size = Window.mSize;

		DepthBuffer.Resource = Context.CreateResource(&TextureDesc, &RenderContext::DefaultHeapProps, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ClearValue);
		Context.CreateDSV(DepthBuffer);
	}

	TextureData SceneColorSmall = {};
	{
		SceneColorSmall.Format = RenderContext::READBACK_FORMAT;
		SceneColorSmall.Size = IVector2{ 960, 540 };
		Context.CreateTexture(SceneColorSmall,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			nullptr
		);
		Context.CreateRTV(SceneColorSmall);
	}

	ComPtr<ID3D12Resource>	SceneColorStagingBuffer = Context.CreateBuffer(SceneColorSmall.Size.x * SceneColorSmall.Size.y * 4, false, true);

	UINT64 ImVertexHighwatermarks[RenderContext::BUFFER_COUNT] = {};
	UINT64 ImIndexHighwatermarks[RenderContext::BUFFER_COUNT] = {};
	ComPtr<ID3D12Resource> ImGuiVertexBuffers[RenderContext::BUFFER_COUNT];
	ComPtr<ID3D12Resource> ImGuiIndexBuffers[RenderContext::BUFFER_COUNT];
	UINT CurrentBackBufferIndex = 0;

	ComPtr<ID3D12PipelineState> GuiPSO;
	{
		TArray<D3D12_INPUT_ELEMENT_DESC> PSOLayout =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"COLOR", 	 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
		DXGI_FORMAT RenderTargetFormat = RenderContext::BACK_BUFFER_FORMAT;
		GuiPSO = Context.CreateShaderCombination(PSOLayout, EntryPoints, "content/shaders/GUI.hlsl", &RenderTargetFormat);

		//PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(
			//D3D12_FILL_MODE_SOLID,
			//D3D12_CULL_MODE_NONE,
			//FALSE, 0, 0.f, 0.f, FALSE, FALSE, FALSE, FALSE,
			//D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
		//);

		//PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		//const D3D12_RENDER_TARGET_BLEND_DESC GuiBlendDesc =
		//{
			//TRUE, FALSE,
			//D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
			//D3D12_BLEND_ZERO, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			//D3D12_LOGIC_OP_NOOP,
			//D3D12_COLOR_WRITE_ENABLE_ALL,
		//};
		//PSODesc.BlendState.RenderTarget[0] = GuiBlendDesc;
	}
	Context.FlushUpload();

	std::atomic<UINT64> LastCompletedFence = 0;
	while (!glfwWindowShouldClose(Window.mHandle))
	{
		FrameMark;

		Window.Update();
		if (Window.mWindowStateDirty)
		{
			ZoneScopedN("Window state dirty");

			// Wait for render thread AND gpu
			TracyLockable(Mutex, WakeUpMainLock);
			std::condition_variable_any WakeUpMain;
			std::atomic_bool Done = false;
			EnqueueRenderThreadWork([&FrameFence = FrameFences[CurrentBackBufferIndex], WaitEvent, &Done, &WakeUpMain](RenderContext& Context) {
				Flush(Context.mGraphicsQueue, FrameFence, Context.mCurrentFenceValue, WaitEvent);
				Done = true;
				WakeUpMain.notify_one();
			});
			WakeUpMain.wait(WakeUpMainLock, [&Done]() { return Done.load(); });

			EnqueueRenderThreadWork([&Window](RenderContext& Context) {
				Context.CreateBackBufferResources(Window);
			});
			Window.mWindowStateDirty = false;
			CurrentBackBufferIndex = 0;
		}

		Gui.Update(Window);

		ImGui::NewFrame();

		if (Helper.Progress < 1.f)
		{
			ImGui::Begin("Meshes");
			ImGui::Text("Loading Meshes");
			ImGui::ProgressBar(Helper.Progress);
			ImGui::End();
		}

		if (!WorkerInProgress && ReadbackInflight && ReadBackReadyFence < LastCompletedFence)
		{
			ZoneScopedN("Readback screen capture");

			int x = SceneColorSmall.Size.x;
			int y = SceneColorSmall.Size.y;

			ComPtr<ID3D12Resource>& Resource = SceneColorStagingBuffer;

			UINT   RowCount;
			UINT64 RowPitch;
			UINT64 ResourceSize;
			D3D12_RESOURCE_DESC Desc = Resource->GetDesc();
			Context.mDevice->GetCopyableFootprints(&Desc, 0, 1, 0, NULL, &RowCount, &RowPitch, &ResourceSize);

			WorkerInProgress = true;
			EnqueueToWorker(
				[
					&ReadbackInflight,
					&WorkerInProgress,
					x, y,
					ResourcePtr = Resource.Get(),
					ResourceSize = RowCount * RowPitch
				]
				() {
					ZoneScopedN("Uploading screenshot to Tracy");
					D3D12_RANGE Range;
					Range.Begin = 0;
					Range.End = (UINT64)ResourceSize;
					void* Data;
					ResourcePtr->Map(0, &Range, &Data);
					FrameImage(Data, (uint16_t)x, (uint16_t)y, 3, false);
					ResourcePtr->Unmap(0, NULL);

					WorkerInProgress = false;
					ReadbackInflight = false;
				}
			);
		}

		{
			ZoneScopedN("Wait for free backbuffer in swapchain");
			do
			{
				// Do more useful work?
				Window.Update();
				Gui.Update(Window);
				Context.FlushUpload();
			} while (!Context.IsSwapChainReady());
		}

		EnqueueRenderThreadWork(
			[
				&LastCompletedFence,
				&CommandAllocators,
				CommandList,
				&SceneColor,
				&DepthBuffer,
				&FrameFences,
				&WaitEvent,
				CurrentBackBufferIndex
			](RenderContext& Context) {

				{
					ZoneScopedN("Wait for fence");
					WaitForFenceValue(FrameFences[CurrentBackBufferIndex], Context.FrameFenceValues[CurrentBackBufferIndex], WaitEvent);
				}
				LastCompletedFence = Context.FrameFenceValues[CurrentBackBufferIndex];

				ZoneScopedN("Reset all and get ready for the frame");

				ComPtr<ID3D12CommandAllocator>& CommandAllocator = CommandAllocators[CurrentBackBufferIndex];
				VALIDATE(CommandAllocator->Reset());
				VALIDATE(CommandList->Reset(CommandAllocator.Get(), nullptr));

				TracyD3D12NewFrame(Context.mGraphicsProfilingCtx);

				{
					TextureData& BackBuffer = Context.GetBackBuffer(CurrentBackBufferIndex);

					CD3DX12_RESOURCE_BARRIER barriers[] = {
						// backbuffer(present -> render)
						CD3DX12_RESOURCE_BARRIER::Transition(
							BackBuffer.Resource.Get(),
							D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET,
							D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
						),

						// SceneColor(srv -> render)
						CD3DX12_RESOURCE_BARRIER::Transition(
							SceneColor.Resource.Get(),
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_RENDER_TARGET
						),
					};
					CommandList->ResourceBarrier(ArraySize(barriers), barriers);
				}

				{
					IVector2 Size = SceneColor.Size;
					CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)Size.x, (float)Size.y);
					CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

					CommandList->RSSetViewports(1, &SceneColorViewport );
					CommandList->RSSetScissorRects(1, &SceneColorScissor);

					// Clear scene color and depth textures
					D3D12_CPU_DESCRIPTOR_HANDLE rtv = Context.GetRTVHandle(SceneColor);
					D3D12_CPU_DESCRIPTOR_HANDLE dsv = Context.GetDSVHandle(DepthBuffer);
					CommandList->OMSetRenderTargets(1, &rtv, true, &dsv);
					{
						FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
						CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
					}

					CommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
				}
			}
		);

		// Mesh
		{
			ImGui::SliderFloat("FOV", &MainCamera.Fov, 5, 160);
			ImGui::SliderFloat("Near", &MainCamera.Near, 0.01, 3);
			ImGui::SliderFloat("Far", &MainCamera.Far, 10000, 1000000);
			ImGui::DragFloat3("Eye", &MainCamera.Eye.x);
			ImGui::DragFloat2("Target", &MainCamera.Angles.x, 0.05);

			EnqueueRenderThreadWork(
				[MainCamera, &CommandList, &DefaultTexture, &SimplePSO, &Window, &Meshes, &MeshDatas](RenderContext&Context)
				{
					TracyD3D12Zone(Context.mGraphicsProfilingCtx, CommandList.Get(), "Render Meshes");
					ZoneScopedN("Drawing meshes");

					Context.BindDescriptors(CommandList, DefaultTexture);

					float Fov = MainCamera.Fov;
					float Near = MainCamera.Near;
					float Far = MainCamera.Far;
					Vector2 Angles = MainCamera.Angles;
					Vector3 Eye = MainCamera.Eye;

					CommandList->SetPipelineState(SimplePSO.Get());
					CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					Matrix4 Projection = CreatePerspectiveMatrix(DegreesToRadians(Fov), (float)Window.mSize.x / (float)Window.mSize.y, Near, Far);
					Matrix4 View = CreateViewMatrix(-Eye, Angles);
					Matrix4 VP = View * Projection;

					for (auto& Mesh : Meshes)
					{
						Matrix4 Combined = Mesh.Transform;

						Combined *= VP;
						CommandList->SetGraphicsRoot32BitConstants(1, 16, &Combined, 0);
						for (UINT ID : Mesh.MeshIDs)
						{
							MeshData& MeshData = MeshDatas[ID];

							D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
							VertexBufferView.BufferLocation = MeshData.VertexBuffer->GetGPUVirtualAddress();
							VertexBufferView.SizeInBytes = MeshData.VertexBufferSize;
							VertexBufferView.StrideInBytes = MeshData.VertexSize;
							CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);

							D3D12_INDEX_BUFFER_VIEW IndexBufferView;
							IndexBufferView.BufferLocation = MeshData.IndexBuffer->GetGPUVirtualAddress();
							IndexBufferView.SizeInBytes = MeshData.IndexBufferSize;
							IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
							CommandList->IASetIndexBuffer(&IndexBufferView);

							CommandList->DrawIndexedInstanced(MeshData.IndexBufferSize / 4, 1, 0, 0, 0);
						}
					}
				}
			);
		}

		EnqueueRenderThreadWork(
			[
				&CommandList,
				&SceneColor,
				&BlitPSO,
				&Quad,
				&Window,
				CurrentBackBufferIndex
			]
			(RenderContext& Context) {
				// SceneColor(render -> srv)
				{
					CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						SceneColor.Resource.Get(),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

					CommandList->ResourceBarrier(1, &barrier);
				}
				{
					TracyD3D12Zone(Context.mGraphicsProfilingCtx, CommandList.Get(), "Blit scenecolor to backbuffer");

					Context.BindDescriptors(CommandList, SceneColor);

					CommandList->SetPipelineState(BlitPSO.Get());
					CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

					D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
					VertexBufferView.BufferLocation = Quad.VertexBuffer->GetGPUVirtualAddress();
					VertexBufferView.SizeInBytes = Quad.VertexBufferSize;
					VertexBufferView.StrideInBytes = Quad.VertexSize;
					CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);

					D3D12_INDEX_BUFFER_VIEW IndexBufferView;
					IndexBufferView.BufferLocation = Quad.IndexBuffer->GetGPUVirtualAddress();
					IndexBufferView.SizeInBytes = Quad.IndexBufferSize;
					IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
					CommandList->IASetIndexBuffer(&IndexBufferView);

					TextureData& BackBuffer = Context.GetBackBuffer(CurrentBackBufferIndex);
					D3D12_CPU_DESCRIPTOR_HANDLE rtv = Context.GetRTVHandle(BackBuffer);
					CommandList->OMSetRenderTargets(1, &rtv, true, nullptr);

					CD3DX12_VIEWPORT	Viewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)Window.mSize.x, (float)Window.mSize.y);
					CD3DX12_RECT		ScissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
					CommandList->RSSetViewports(1, &Viewport);
					CommandList->RSSetScissorRects(1, &ScissorRect);

					CommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);
				}
			}
		);

		ImGui::EndFrame();
		ImGui::Render();

		ComPtr<ID3D12Resource>& ImGuiVertexBuffer = ImGuiVertexBuffers[CurrentBackBufferIndex];
		ComPtr<ID3D12Resource>& ImGuiIndexBuffer = ImGuiIndexBuffers[CurrentBackBufferIndex];
		UINT64& WatermarkVertex = ImVertexHighwatermarks[CurrentBackBufferIndex];
		UINT64& WatermarkIndex = ImIndexHighwatermarks[CurrentBackBufferIndex];

		ImDrawData *DrawData = ImGui::GetDrawData();

		if (DrawData && DrawData->TotalVtxCount != 0)
		{
			UINT64 VertexBufferSize = DrawData->TotalVtxCount * sizeof(ImDrawVert);
			if (VertexBufferSize > WatermarkVertex)
			{
				ImGuiVertexBuffer = Context.CreateBuffer(VertexBufferSize, true);
				WatermarkVertex = VertexBufferSize;
			}

			UINT64 IndexBufferSize = DrawData->TotalIdxCount * sizeof(ImDrawIdx);
			if (IndexBufferSize > WatermarkIndex)
			{
				ImGuiIndexBuffer = Context.CreateBuffer(IndexBufferSize, true);
				WatermarkIndex = IndexBufferSize;
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

			TArray<ImDrawList*> DrawLists;
			for (int i = 0; i < DrawData->CmdListsCount; ++i)
			{
				ImDrawList* ImGuiCmdList = DrawData->CmdLists[i];
				DrawLists.push_back(ImGuiCmdList->CloneOutput());
			}

			EnqueueRenderThreadWork(
				[
					DL = MOVE(DrawLists),
					&CommandList,
					&GuiPSO,
					&Font = Gui.FontTexData,
					&ImGuiIndexBuffer,
					&ImGuiVertexBuffer,
					IndexBufferSize,
					VertexBufferSize,
					CurrentBackBufferIndex
				](RenderContext& Context) {
				TracyD3D12Zone(Context.mGraphicsProfilingCtx, CommandList.Get(), "Render GUI");

				Context.BindDescriptors(CommandList, Font);

				TextureData& RenderTarget = Context.GetBackBuffer(CurrentBackBufferIndex);
				D3D12_CPU_DESCRIPTOR_HANDLE rtv = Context.GetRTVHandle(RenderTarget);
				CommandList->OMSetRenderTargets(1, &rtv, true, nullptr);
				CommandList->SetPipelineState(GuiPSO.Get());
				CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				Vector2 RenderTargetSize{ (float)RenderTarget.Size.x, (float)RenderTarget.Size.y };
				CommandList->SetGraphicsRoot32BitConstants(1, 2, &RenderTargetSize, 0);

				D3D12_INDEX_BUFFER_VIEW ImGuiIndexBufferView;
				ImGuiIndexBufferView.BufferLocation = ImGuiIndexBuffer->GetGPUVirtualAddress();
				ImGuiIndexBufferView.SizeInBytes = UINT(IndexBufferSize);
				ImGuiIndexBufferView.Format = DXGI_FORMAT_R16_UINT;
				CommandList->IASetIndexBuffer(&ImGuiIndexBufferView);

				UINT64 VertexOffset = 0;
				UINT  IndexOffset = 0;
				for (int i = 0; i < DL.size(); ++i)
				{
					ImDrawList* ImGuiCmdList = DL[i];

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
				for (ImDrawList*List : DL)
				{
					IM_DELETE(List);
				}
			});
		}

#if TRACY_ENABLE // Send screenshot to Tracy
		if (TracyIsConnected && !ReadbackInflight && !WorkerInProgress)
		{
			ReadbackInflight = true;

			EnqueueRenderThreadWork(
				[
					&CommandList,
					&SceneColorSmall,
					&SceneColor,
					&Quad,
					&DownsampleRasterPSO,
					&SceneColorStagingBuffer,
					&ReadBackReadyFence,
					CurrentBackBufferIndex
				]
			(RenderContext& Context) {
				TracyD3D12Zone(Context.mGraphicsProfilingCtx, CommandList.Get(), "Downsample scenecolor to readback");

				TextureData& Small = SceneColorSmall;
				IVector2 Size = Small.Size;

				// readback(copy -> render)
				{
					CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						Small.Resource.Get(),
						D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
					CommandList->ResourceBarrier(1, &barrier);
				}

				{
					CD3DX12_VIEWPORT	SmallViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)Size.x, (float)Size.y);
					CD3DX12_RECT		SmallScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

					D3D12_CPU_DESCRIPTOR_HANDLE rtv = Context.GetRTVHandle(Small);
					CommandList->OMSetRenderTargets(1, &rtv, true, nullptr);

					Context.BindDescriptors(CommandList, SceneColor);

					D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
					VertexBufferView.BufferLocation = Quad.VertexBuffer->GetGPUVirtualAddress();
					VertexBufferView.SizeInBytes = Quad.VertexBufferSize;
					VertexBufferView.StrideInBytes = Quad.VertexSize;
					CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);

					D3D12_INDEX_BUFFER_VIEW IndexBufferView;
					IndexBufferView.BufferLocation = Quad.IndexBuffer->GetGPUVirtualAddress();
					IndexBufferView.SizeInBytes = Quad.IndexBufferSize;
					IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
					CommandList->IASetIndexBuffer(&IndexBufferView);

					CommandList->SetPipelineState(DownsampleRasterPSO.Get());
					CommandList->RSSetViewports(1, &SmallViewport);
					CommandList->RSSetScissorRects(1, &SmallScissor);

					CommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);
				}

				// readback(render -> copy)
				{
					CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						Small.Resource.Get(),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
					CommandList->ResourceBarrier(1, &barrier);
				}

				{
					D3D12_RESOURCE_DESC Desc = Small.Resource->GetDesc();
					UINT RowCount;
					UINT64 RowPitch;
					UINT64 ResourceSize;
					Context.mDevice->GetCopyableFootprints(&Desc, 0, 1, 0, NULL, &RowCount, &RowPitch, &ResourceSize);

					D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
					bufferFootprint.Footprint.Width = Small.Size.x;
					bufferFootprint.Footprint.Height = Small.Size.y;
					bufferFootprint.Footprint.Depth = 1;
					bufferFootprint.Footprint.RowPitch = (UINT)RowPitch;
					bufferFootprint.Footprint.Format = Small.Format;

					CD3DX12_TEXTURE_COPY_LOCATION Dst(SceneColorStagingBuffer.Get(), bufferFootprint);
					CD3DX12_TEXTURE_COPY_LOCATION Src(Small.Resource.Get());
					CommandList->CopyTextureRegion(&Dst,0,0,0,&Src,nullptr);

					ReadBackReadyFence = Context.mCurrentFenceValue + 1;
				}
			});
		}
#endif

		// backbuffer(render -> present)
		EnqueueRenderThreadWork([&CommandList, CurrentBackBufferIndex](RenderContext& Context) {
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
					Context.GetBackBuffer(CurrentBackBufferIndex).Resource.Get(),
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
				CommandList->ResourceBarrier(1, &barrier);
			}
		});

		EnqueueRenderThreadWork([&CommandList, &FrameFences, CurrentBackBufferIndex](RenderContext& Context) {
			ZoneScopedN("Submit main command list");

			VALIDATE(CommandList->Close());
			Context.FrameFenceValues[CurrentBackBufferIndex] = Context.ExecuteGraphics(
				CommandList.Get(),
				FrameFences[CurrentBackBufferIndex],
				Context.mCurrentFenceValue
			);
			Context.Present();
		});

		CurrentBackBufferIndex = (CurrentBackBufferIndex + 1) % RenderContext::BUFFER_COUNT;
	}

	Importer.SetProgressHandler(nullptr);
	Helper.ShouldProceed = false;

	if (CurrentBackBufferIndex == 0)
	{
		CurrentBackBufferIndex = RenderContext::BUFFER_COUNT;
	}

	// Wait for render thread AND gpu
	TracyLockable(Mutex, WakeUpMainLock);
	std::condition_variable_any WakeUpMain;
	std::atomic_bool Done = false;
	EnqueueRenderThreadWork([&FrameFences,CurrentBackBufferIndex, WaitEvent, &Done, &WakeUpMain](RenderContext& Context) {
		Flush(Context.mGraphicsQueue, FrameFences[CurrentBackBufferIndex - 1], Context.mCurrentFenceValue, WaitEvent);
		Context.WaitForUploadFinish();
		Done = true;
		WakeUpMain.notify_one();
	});
	WakeUpMain.wait(WakeUpMainLock, [&Done]() { return Done.load(); });
	StopRenderThread();
	StopWorkerThreads();
	CloseHandle(WaitEvent);
}

