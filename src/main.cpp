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
#include <assimp/ProgressHandler.hpp>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <Containers/Queue.h>
#include <eathread/eathread.h>
#include <thread>
#include <tbb/parallel_for.h>
#include <tbb/task_group.h>
#include <atomic>

#define CAPTURE_SCREEN 1

struct MeshData
{
	ComPtr<ID3D12Resource>		VertexBuffer;
	ComPtr<ID3D12Resource>		IndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW	VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW		IndexBufferView;
};

struct Material
{
	String Name;
	TArray<uint32_t> DiffuseTextures;
};

Render::TextureData ParseTexture(aiTexture* Texture)
{
	Render::TextureData Result;

	if (Texture->mHeight == 0)
	{
		int Channels = 0;
		stbi_uc* Data = stbi_load_from_memory(
			(stbi_uc*)Texture->pcData,
			Texture->mWidth,
			&Result.Size.x, &Result.Size.y,
			&Channels, 4);

		Result.Data = StringView((char *)Data, Result.Size.x * Result.Size.y * Channels);
		Result.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		return Result;
	}

	return Result;
}

void AllocateMeshData(aiMesh* Mesh, Render::Context& RenderContext, MeshData& Result)
{
	CHECK(Mesh->HasFaces(), "Mesh without faces?");

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
	ProgressBarHelper Helper;
	Importer.SetProgressHandler(&Helper);

	struct Mesh
	{
		String Name;
		Math::Matrix Transform;
		TArray<UINT> MeshIDs;
	};

	TArray<Material> Materials;
	TArray<MeshData> MeshDatas;
	TArray<Mesh> Meshes;
	TArray<Render::TextureData> Textures;

	std::atomic<UINT> Counter(0);
	std::atomic<bool> TexturesLoaded;

	using namespace tbb;
	task_group TaskGroup;
	TaskGroup.run([&]()
	{
		const aiScene* Scene = nullptr;
		{
			ZoneScopedN("Scene file parsing");

			Scene = Importer.ReadFile(
				"content/DamagedHelmet.glb",
				aiProcess_FlipWindingOrder | aiProcessPreset_TargetRealtime_Quality
			);
			if (Helper.ShouldProceed == false)
			{
				return 0;
			}
			CHECK_RETURN(Scene != nullptr, "Load failed", 0);
		}

		if (Scene->HasMaterials())
		{
			TaskGroup.run([&]() {
				for (UINT i = 0; i < Scene->mNumMaterials; ++i)
				{
					aiMaterial* MaterialPtr = Scene->mMaterials[i];
					Material Tmp;
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
							DEBUG_BREAK();
						}
					}
				}
			});
			if (Scene->HasTextures())
			{
				TaskGroup.run([&]() {
					for (UINT i = 0; i < Scene->mNumTextures; ++i)
					{
						aiTexture* Texture = Scene->mTextures[i];
						Render::TextureData TexData = ParseTexture(Texture);
						RenderContext.CreateTexture(TexData);
						RenderContext.CreateSRV(TexData);
						Textures.push_back(TexData);
					}
					TexturesLoaded = true;
				});
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
					AllocateMeshData(Mesh, RenderContext, Tmp);

					UploadOffsets[i] = UploadBufferSize;
					UploadBufferSize += Tmp.IndexBufferView.SizeInBytes + Tmp.VertexBufferView.SizeInBytes;
				}

				UploadBuffer = RenderContext.CreateBuffer(UploadBufferSize, true);

				unsigned char* CpuPtr = NULL;
				UploadBuffer->Map(0, NULL, (void**)&CpuPtr);
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

			TaskGroup.run([&]() {
				using namespace Math;
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
					for (int i = 0; i < Current->mNumChildren; ++i)
					{
						ProcessingQueue.emplace(Current->mChildren[i], CurrentTransform);
					}

					if (Current->mNumMeshes > 0)
					{
						TArray<UINT> MeshIDs;
						MeshIDs.reserve(Current->mNumMeshes);
						for (int i = 0; i < Current->mNumMeshes; ++i)
						{
							unsigned int MeshID = Current->mMeshes[i];
							MeshIDs.push_back(MeshID);
						}

						CurrentTransform.Transpose();
						Meshes.push_back(
							Mesh
							{
								String(Current->mName.C_Str()),
								Math::Matrix(CurrentTransform[0]),
								MOVE(MeshIDs)
							}
						);
					}
				}
				Counter.fetch_add(1);
			});

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
			UINT64 CurrentFenceValue = 0;

			ComPtr<ID3D12Fence> WorkerFrameFence = RenderContext.CreateFence();
			CurrentFenceValue = RenderContext.ExecuteCopy(WorkerCommandList.Get(), WorkerFrameFence, CurrentFenceValue);
			HANDLE WorkerWaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

			WaitForFenceValue(WorkerFrameFence, 1, WorkerWaitEvent);
			CloseHandle(WorkerWaitEvent);
			RenderContext.FlushUpload();
		}
		Counter.fetch_add(1);
		return 0;
	});

	ComPtr<ID3D12PipelineState> MeshDrawPSO;
	ComPtr<ID3D12PipelineState> DownsampleComputePSO;

	TaskGroup.run([&]()
	{
		{
			ComPtr<ID3DBlob> ComputeShader = Render::CompileShader(
				"content/shaders/Downsample.hlsl",
				"Main", "cs_5_1"
			);

			D3D12_COMPUTE_PIPELINE_STATE_DESC PSODesc = {};
			PSODesc.CS.pShaderBytecode = ComputeShader->GetBufferPointer();
			PSODesc.CS.BytecodeLength = ComputeShader->GetBufferSize();
			PSODesc.pRootSignature = RenderContext.mComputeRootSignature.Get();

			DownsampleComputePSO = RenderContext.CreatePSO(&PSODesc);
		}
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
			PSODesc.RTVFormats[0] = Render::Context::SCENE_COLOR_FORMAT;
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

			MeshDrawPSO = RenderContext.CreatePSO(&PSODesc);
		}

		RenderContext.FlushUpload();
		Counter.fetch_add(1);
		return 0;
	});

	Render::TextureData DefaultTexture;
	{
		unsigned char *Data = stbi_load("content/uvcheck.jpg", &DefaultTexture.Size.x, &DefaultTexture.Size.y, nullptr, 4);
		DefaultTexture.Data = StringView((char *)Data, DefaultTexture.Size.x * DefaultTexture.Size.y * 4);
		DefaultTexture.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

		RenderContext.CreateTexture(DefaultTexture);
		RenderContext.CreateSRV(DefaultTexture);
		stbi_image_free(Data);
	}

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

		Window.Update();
		if (Window.mWindowStateDirty)
		{
			Flush(RenderContext.mGraphicsQueue, FrameFences[RenderContext.mCurrentBackBufferIndex], CurrentFenceValue, WaitEvent);
			RenderContext.CreateBackBufferResources(Window);
			Window.mWindowStateDirty = false;
		}

		Gui.Update(Window);

		ImGui::NewFrame();

		static bool ShowDemo = true;
		ImGui::ShowDemoWindow(&ShowDemo);

		ImGui::Begin("Meshes");
		if (Helper.Progress >= 1.f)
		{
			for (auto& Mesh : Meshes)
			{
				Math::Vector3 Location, Scale;
				Math::Quaternion Rotation;
				Mesh.Transform.Decompose(Scale, Rotation, Location);

				ImGui::BeginGroup();
				ImGui::TextUnformatted(Mesh.Name.c_str());
				ImGui::Text("Location: %f %f %f", Location.x, Location.y, Location.z);
				ImGui::Text("Scale:    %f %f %f", Scale.x, Scale.y, Scale.z);
				ImGui::Text("Rotation: %f %f %f %f", Rotation.x, Rotation.y, Rotation.z, Rotation.w);
				ImGui::Text("MeshIDs: %d", Mesh.MeshIDs.size());
				ImGui::EndGroup();
			}
		}
		else
		{
			ImGui::Text("Loading Meshes");
			ImGui::ProgressBar(Helper.Progress);
		}
		ImGui::End();

		ComPtr<ID3D12Resource>& BackBuffer = RenderContext.mBackBuffers[RenderContext.mCurrentBackBufferIndex];
		ComPtr<ID3D12CommandAllocator>& CommandAllocator = CommandAllocators[RenderContext.mCurrentBackBufferIndex];
		ComPtr<ID3D12Fence>& Fence = FrameFences[RenderContext.mCurrentBackBufferIndex];

		{
			ZoneScopedN("Wait for free backbuffer in swapchain");
			while (!RenderContext.IsSwapChainReady())
			{
				// Do more useful work?
				Window.Update();
				Gui.Update(Window);
			}
		}

		WaitForFenceValue(Fence, FrameFenceValues[RenderContext.mCurrentBackBufferIndex], WaitEvent);

		VALIDATE(CommandAllocator->Reset());
		VALIDATE(CommandList->Reset(CommandAllocator.Get(), nullptr));

		TracyD3D12NewFrame(RenderContext.mGraphicsProfilingCtx);

		// Clear the render target.
		{
			// backbuffer(present -> render)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
					BackBuffer.Get(),
					D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
				CommandList->ResourceBarrier(1, &barrier);
			}

			// SceneColor(srv -> render)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
					RenderContext.mSceneColor.Resource.Get(),
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

				CommandList->ResourceBarrier(1, &barrier);
			}

			CommandList->RSSetViewports(1, &Window.mViewport);
			CommandList->RSSetScissorRects(1, &Window.mScissorRect);

			{
				D3D12_CPU_DESCRIPTOR_HANDLE rtv = RenderContext.GetRTVHandleForBackBuffer();
				CommandList->OMSetRenderTargets(1, &rtv, true, nullptr);
				{
					FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
					CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
				}
			}

			D3D12_CPU_DESCRIPTOR_HANDLE rtv = RenderContext.GetRTVHandle(RenderContext.mSceneColor.RTVIndex);
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = RenderContext.GetDSVHandle();
			CommandList->OMSetRenderTargets(1, &rtv, true, &dsv);
			{
				FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
				CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
			}

			CommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}

		// Mesh
		if (Counter == 3)
		{
			using namespace Math;
			static float Fov = 60;
			static float Near = 0.1;
			static float Far = 1000000;
			static Vector3 Eye(0, 0, 2);
			static Vector2 Angles(0, 0);
			
			ImGui::SliderFloat("FOV", &Fov, 5, 160);
			ImGui::SliderFloat("Near", &Near, 0.01, 3);
			ImGui::SliderFloat("Far", &Far, 10000, 1000000);
			ImGui::DragFloat3("Eye", &Eye.x);
			ImGui::DragFloat2("Target", &Angles.x, 0.05);

			TracyD3D12Zone(RenderContext.mGraphicsProfilingCtx, CommandList.Get(), "Render Meshes");

			ZoneScopedN("Drawing meshes");

			RenderContext.BindDescriptors(CommandList, DefaultTexture);

			CommandList->SetPipelineState(MeshDrawPSO.Get());
			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			Matrix Projection = Matrix::CreatePerspectiveFieldOfView(Math::Radians(Fov), Window.mSize.x / Window.mSize.y, Near, Far);
			Matrix View = Matrix::CreateTranslation(-Eye) * Matrix::CreateRotationY(Angles.x) * Matrix::CreateRotationX(Angles.y);
			for (auto& Mesh : Meshes)
			{
				Matrix Combined = Mesh.Transform;

				Combined *= View;
				Combined *= Projection;
				CommandList->SetGraphicsRoot32BitConstants(1, 16, &Combined, 0);
				for (UINT ID : Mesh.MeshIDs)
				{
					MeshData& MeshData = MeshDatas[ID];

					CommandList->IASetVertexBuffers(0, 1, &MeshData.VertexBufferView);
					CommandList->IASetIndexBuffer(&MeshData.IndexBufferView);

					CommandList->DrawIndexedInstanced(MeshData.IndexBufferView.SizeInBytes / 4, 1, 0, 0, 0);
				}
			}
		}

		RenderContext.RenderGUI(CommandList, Window, Gui.FontTexData);

#if TRACY_ENABLE || CAPTURE_SCREEN
		// Send screenshot to Tracy
		{
			// SceneColor(render -> uav)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
					RenderContext.mSceneColor.Resource.Get(),
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

				CommandList->ResourceBarrier(1, &barrier);
			}

			// downsample scenecolor to readback

			CommandList->SetPipelineState(DownsampleComputePSO.Get());
			CommandList->SetComputeRootSignature(RenderContext.mComputeRootSignature.Get());
			{
				D3D12_GPU_DESCRIPTOR_HANDLE SceneColorSRV = RenderContext.GetGeneralHandleGPU(RenderContext.mSceneColor.SRVIndex);
				CommandList->SetComputeRootDescriptorTable(0, SceneColorSRV);

				D3D12_GPU_DESCRIPTOR_HANDLE SceneColorReadbackUAV = RenderContext.GetGeneralHandleGPU(RenderContext.mSceneColorReadback.UAVIndex);
				CommandList->SetComputeRootDescriptorTable(1, SceneColorReadbackUAV);
			}

			IVector2 Size = RenderContext.mSceneColorReadback.Size;
			CommandList->Dispatch(Size.x, Size.y, 1);
		}
#endif

		// backbuffer(render -> present)
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				BackBuffer.Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			CommandList->ResourceBarrier(1, &barrier);
		}

		// Present backbuffer
		{
			VALIDATE(CommandList->Close());
			FrameFenceValues[RenderContext.mCurrentBackBufferIndex] = RenderContext.ExecuteGraphics(CommandList.Get(), Fence, CurrentFenceValue);
			RenderContext.Present();
		}
		RenderContext.mCurrentBackBufferIndex = (RenderContext.mCurrentBackBufferIndex + 1) % Render::Context::BUFFER_COUNT;
	}

	Importer.SetProgressHandler(nullptr);
	Helper.ShouldProceed = false;
	TaskGroup.wait();
	// hack, don't care
	if (RenderContext.mCurrentBackBufferIndex == 0)
	{
		RenderContext.mCurrentBackBufferIndex = Render::Context::BUFFER_COUNT;
	}
	Flush(RenderContext.mGraphicsQueue, FrameFences[RenderContext.mCurrentBackBufferIndex - 1], CurrentFenceValue, WaitEvent);
	CloseHandle(WaitEvent);
}

