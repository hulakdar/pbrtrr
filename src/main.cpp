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
#include <atomic>

#define CAPTURE_SCREEN 1

struct MeshData
{
	ComPtr<ID3D12Resource>		VertexBuffer;
	ComPtr<ID3D12Resource>		IndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW	VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW		IndexBufferView;
	uint32_t					MaterialIndex;
};

struct Material
{
	String Name;
	TArray<uint32_t> DiffuseTextures;
};

TextureData ParseTexture(aiTexture* Texture, Render::Context& RenderContext)
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

	RenderContext.CreateTexture(Result);

	{
		CHECK(Data);
		RenderContext.UploadTextureData(Result, Data);
		free(Data);
	}

	RenderContext.CreateSRV(Result);

	return Result;
}

void AllocateMeshData(aiMesh* Mesh, Render::Context& RenderContext, MeshData& Result)
{
	CHECK(Mesh->HasFaces(), "Mesh without faces?");

	UINT64 VertexSize = sizeof(aiVector3D);

	if (Mesh->HasNormals())
	{
		VertexSize += sizeof(aiVector3D);
	}

	UINT64 VertexColors = 0;
	while (Mesh->HasVertexColors(VertexColors))
	{
		VertexSize += 4;
		VertexColors++;
	}

	UINT64 UVSets = 0;
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
	TArray<TextureData> Textures;

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
			{
				Materials.resize(Scene->mNumMaterials);
				for (UINT i = 0; i < Scene->mNumMaterials; ++i)
				{
					aiMaterial* MaterialPtr = Scene->mMaterials[i];
					Material &Tmp = Materials[i];
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
			}
			if (Scene->HasTextures())
			{
				{
					for (UINT i = 0; i < Scene->mNumTextures; ++i)
					{
						aiTexture* Texture = Scene->mTextures[i];
						Textures.push_back(ParseTexture(Texture, RenderContext));
					}
					RenderContext.FlushUpload();
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
					AllocateMeshData(Mesh, RenderContext, Tmp);
					Tmp.MaterialIndex = Mesh->mMaterialIndex;

					UploadOffsets[i] = UploadBufferSize;
					UploadBufferSize += Tmp.IndexBufferView.SizeInBytes + Tmp.VertexBufferView.SizeInBytes;
				}

				UploadBuffer = RenderContext.CreateBuffer(UploadBufferSize, true);

				{
					ZoneScopedN("Upload")

					unsigned char* CpuPtr = NULL;
					UploadBuffer->Map(0, NULL, (void**)&CpuPtr);
					for (int i = 0; i < Scene->mNumMeshes; ++i)
					{
						aiMesh* Mesh = Scene->mMeshes[i];
						UploadMeshData(CpuPtr + UploadOffsets[i], Mesh);
					}
					UploadBuffer->Unmap(0, NULL);
				}

			}

			{
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
			}

			ComPtr<ID3D12CommandAllocator> WorkerCommandAllocator = RenderContext.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY);
			ComPtr<ID3D12GraphicsCommandList> WorkerCommandList = RenderContext.CreateCommandList(WorkerCommandAllocator, D3D12_COMMAND_LIST_TYPE_COPY);
			WorkerCommandList->Reset(WorkerCommandAllocator.Get(), nullptr);

			{
				TracyD3D12Zone(RenderContext.mCopyProfilingCtx, WorkerCommandList.Get(), "Copy Mesh Data to GPU");
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
	}

	ComPtr<ID3D12PipelineState> SimplePSO;

	{
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
			PSODesc.InputLayout.NumElements = PSOLayout.size();
			PSODesc.InputLayout.pInputElementDescs = PSOLayout.data();
			PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			PSODesc.SampleDesc.Count = 1;
			PSODesc.DepthStencilState.DepthEnable = true;
			PSODesc.DepthStencilState.StencilEnable = false;
			PSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			PSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
			PSODesc.SampleMask = 0xFFFFFFFF;
			PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

			SimplePSO = RenderContext.CreatePSO(&PSODesc);
		}

		RenderContext.FlushUpload();
	}

	ComPtr<ID3D12PipelineState> BlitPSO;
	{
		TArray<D3D12_INPUT_ELEMENT_DESC> PSOLayout;
		{
			D3D12_INPUT_ELEMENT_DESC Tmp = {};

			Tmp.SemanticName = "POSITION";
			Tmp.SemanticIndex = 0;
			Tmp.Format = DXGI_FORMAT_R32G32_FLOAT;
			Tmp.InputSlot = 0;
			Tmp.AlignedByteOffset = 0;
			PSOLayout.push_back(Tmp);
		}

		ComPtr<ID3DBlob> VertexShader = Render::CompileShader(
			"content/shaders/Blit.hlsl",
			"MainVS", "vs_5_1"
		);

		ComPtr<ID3DBlob> PixelShader = Render::CompileShader(
			"content/shaders/Blit.hlsl",
			"MainPS", "ps_5_1"
		);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.VS.BytecodeLength = VertexShader->GetBufferSize();
		PSODesc.VS.pShaderBytecode = VertexShader->GetBufferPointer();
		PSODesc.PS.BytecodeLength = PixelShader->GetBufferSize();
		PSODesc.PS.pShaderBytecode = PixelShader->GetBufferPointer();
		PSODesc.pRootSignature = RenderContext.mRootSignature.Get();
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = Render::Context::BACK_BUFFER_FORMAT;
		PSODesc.InputLayout.NumElements = PSOLayout.size();
		PSODesc.InputLayout.pInputElementDescs = PSOLayout.data();
		PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		PSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		PSODesc.SampleDesc.Count = 1;
		PSODesc.SampleMask = 0xFFFFFFFF;
		PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		BlitPSO = RenderContext.CreatePSO(&PSODesc);
	}

	ComPtr<ID3D12PipelineState> DownsampleRasterPSO;
	{
		TArray<D3D12_INPUT_ELEMENT_DESC> PSOLayout;
		{
			D3D12_INPUT_ELEMENT_DESC Tmp = {};

			Tmp.SemanticName = "POSITION";
			Tmp.SemanticIndex = 0;
			Tmp.Format = DXGI_FORMAT_R32G32_FLOAT;
			Tmp.InputSlot = 0;
			Tmp.AlignedByteOffset = 0;
			PSOLayout.push_back(Tmp);
		}

		ComPtr<ID3DBlob> VertexShader = Render::CompileShader(
			"content/shaders/Blit.hlsl",
			"MainVS", "vs_5_1"
		);

		ComPtr<ID3DBlob> PixelShader = Render::CompileShader(
			"content/shaders/Blit.hlsl",
			"MainPS", "ps_5_1"
		);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
		PSODesc.VS.BytecodeLength = VertexShader->GetBufferSize();
		PSODesc.VS.pShaderBytecode = VertexShader->GetBufferPointer();
		PSODesc.PS.BytecodeLength = PixelShader->GetBufferSize();
		PSODesc.PS.pShaderBytecode = PixelShader->GetBufferPointer();
		PSODesc.pRootSignature = RenderContext.mRootSignature.Get();
		PSODesc.NumRenderTargets = 1;
		PSODesc.RTVFormats[0] = Render::Context::READBACK_FORMAT;
		PSODesc.InputLayout.NumElements = PSOLayout.size();
		PSODesc.InputLayout.pInputElementDescs = PSOLayout.data();
		PSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		PSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		PSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		PSODesc.SampleDesc.Count = 1;
		PSODesc.SampleMask = 0xFFFFFFFF;
		PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		DownsampleRasterPSO = RenderContext.CreatePSO(&PSODesc);
	}

	TextureData DefaultTexture;
	{
		uint8_t *Data = stbi_load("content/uvcheck.jpg", &DefaultTexture.Size.x, &DefaultTexture.Size.y, nullptr, 4);
		DefaultTexture.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

		RenderContext.CreateTexture(DefaultTexture);
		RenderContext.UploadTextureData(DefaultTexture, Data);
		RenderContext.CreateSRV(DefaultTexture);
		stbi_image_free(Data);
	}

	MeshData Quad;
	{
		uint16_t IndexData[] = {
			0, 1, 2,
		};

		Math::Vector2 VertexData[] = {
			{-1, -3},
			{-1, 1},
			{3, 1},
		};

		Quad.IndexBuffer = RenderContext.CreateBuffer(
			sizeof(IndexData)
		);
		SetD3DName(Quad.IndexBuffer, L"Quad IndexBuffer");
		Quad.IndexBufferView = RenderContext.CreateIndexBufferView(
			Quad.IndexBuffer,
			IndexData,
			sizeof(IndexData),
			DXGI_FORMAT_R16_UINT
		);

		Quad.VertexBuffer = RenderContext.CreateBuffer(
			sizeof(VertexData)
		);
		SetD3DName(Quad.VertexBuffer, L"Quad VertexBuffer");
		Quad.VertexBufferView = RenderContext.CreateVertexBufferView(
			Quad.VertexBuffer,
			VertexData,
			sizeof(VertexData),
			sizeof(Math::Vector2)
		);
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

	struct ReadBackData
	{
		UINT64 FenceValue;
		UINT64 BufferIndex;
	};
	TArray<ReadBackData> ReadBackQueue;
	UINT64 WindowStateDirtyFlushes = 0;

	while (!glfwWindowShouldClose(Window.mHandle))
	{
		FrameMark;

		Window.Update();
		if (Window.mWindowStateDirty)
		{
			Flush(RenderContext.mGraphicsQueue, FrameFences[RenderContext.mCurrentBackBufferIndex], CurrentFenceValue, WaitEvent);
			WindowStateDirtyFlushes++;
			RenderContext.CreateBackBufferResources(Window);
			Window.mWindowStateDirty = false;
		}

		Gui.Update(Window);

		ImGui::NewFrame();

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
				ImGui::TextUnformatted("MeshIDs:");
				for (auto ID : Mesh.MeshIDs)
				{
					ImGui::Text("	: %d", ID);
					ImGui::SameLine();
				}
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

		if (!ReadBackQueue.empty() && ReadBackQueue.front().FenceValue < FrameFenceValues[RenderContext.mCurrentBackBufferIndex])
		{
			ZoneScopedN("Readback screen capture");
			UINT64 BufferIndex = ReadBackQueue.front().BufferIndex;
			ComPtr<ID3D12Resource>& Resource = RenderContext.mSceneColorStagingBuffers[BufferIndex];

			UINT   RowCount;
			UINT64 RowPitch;
			UINT64 ResourceSize;
			D3D12_RESOURCE_DESC Desc = Resource->GetDesc();
			RenderContext.mDevice->GetCopyableFootprints(&Desc, 0, 1, 0, NULL, &RowCount, &RowPitch, &ResourceSize);

			D3D12_RANGE Range;
			Range.Begin = 0;
			Range.End = (UINT64)RowCount * RowPitch;
			void* Data;
			Resource->Map(0, &Range, &Data);
			FrameImage(Data, RenderContext.mSceneColorSmall.Size.x, RenderContext.mSceneColorSmall.Size.y, CurrentFenceValue - ReadBackQueue.front().FenceValue - WindowStateDirtyFlushes, false);
			Resource->Unmap(0, NULL);

			ReadBackQueue.erase(ReadBackQueue.begin());
		}

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

		{
			CD3DX12_RESOURCE_BARRIER barriers[] = {
				// backbuffer(present -> render)
				CD3DX12_RESOURCE_BARRIER::Transition(
					BackBuffer.Get(),
					D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET,
					D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
				),

				// SceneColor(srv -> render)
				CD3DX12_RESOURCE_BARRIER::Transition(
					RenderContext.mSceneColor.Resource.Get(),
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
					D3D12_RESOURCE_STATE_RENDER_TARGET
				),
			};
			CommandList->ResourceBarrier(ArraySize(barriers), barriers);
		}

		{
			IVector2 Size = RenderContext.mSceneColor.Size;
			CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, Size.x, Size.y);
			CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

			CommandList->RSSetViewports(1, &SceneColorViewport );
			CommandList->RSSetScissorRects(1, &SceneColorScissor);

			// Clear scene color and depth textures
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

			CommandList->SetPipelineState(SimplePSO.Get());
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

		// SceneColor(render -> srv)
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				RenderContext.mSceneColor.Resource.Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			CommandList->ResourceBarrier(1, &barrier);
		}

		{
			TracyD3D12Zone(RenderContext.mGraphicsProfilingCtx, CommandList.Get(), "Blit scenecolor to backbuffer");

			Math::Matrix Identity;

			RenderContext.BindDescriptors(CommandList, RenderContext.mSceneColor);

			CommandList->SetPipelineState(BlitPSO.Get());
			CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			CommandList->SetGraphicsRoot32BitConstants(1, 16, &Identity, 0);

			CommandList->IASetVertexBuffers(0, 1, &Quad.VertexBufferView);
			CommandList->IASetIndexBuffer(&Quad.IndexBufferView);

			D3D12_CPU_DESCRIPTOR_HANDLE rtv = RenderContext.GetRTVHandleForBackBuffer();
			CommandList->OMSetRenderTargets(1, &rtv, true, nullptr);
			CommandList->RSSetViewports(1, &Window.mViewport);
			CommandList->RSSetScissorRects(1, &Window.mScissorRect);

			CommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);
		}

#if TRACY_ENABLE || CAPTURE_SCREEN // Send screenshot to Tracy
		{
			TracyD3D12Zone(RenderContext.mGraphicsProfilingCtx, CommandList.Get(), "Downsample scenecolor to readback");

			TextureData& Small = RenderContext.mSceneColorSmall;
			IVector2 Size = Small.Size;

			// readback(copy -> render)
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
					Small.Resource.Get(),
					D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
				CommandList->ResourceBarrier(1, &barrier);
			}

			{
				CD3DX12_VIEWPORT	SmallViewport = CD3DX12_VIEWPORT(0.f, 0.f, Size.x, Size.y);
				CD3DX12_RECT		SmallScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

				D3D12_CPU_DESCRIPTOR_HANDLE rtv = RenderContext.GetRTVHandle(Small.RTVIndex);
				CommandList->OMSetRenderTargets(1, &rtv, true, nullptr);

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
				RenderContext.mDevice->GetCopyableFootprints(&Desc, 0, 1, 0, NULL, &RowCount, &RowPitch, &ResourceSize);

				D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
				bufferFootprint.Footprint.Width = Small.Size.x;
				bufferFootprint.Footprint.Height = Small.Size.y;
				bufferFootprint.Footprint.Depth = 1;
				bufferFootprint.Footprint.RowPitch = RowPitch;
				bufferFootprint.Footprint.Format = Small.Format;

				CD3DX12_TEXTURE_COPY_LOCATION Dst(RenderContext.mSceneColorStagingBuffers[RenderContext.mCurrentBackBufferIndex].Get(), bufferFootprint);
				CD3DX12_TEXTURE_COPY_LOCATION Src(Small.Resource.Get());
				CommandList->CopyTextureRegion(&Dst,0,0,0,&Src,nullptr);
			}
		}
#endif
		// backbuffer(render -> present)
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				BackBuffer.Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			CommandList->ResourceBarrier(1, &barrier);
		}

		{
			ZoneScopedN("Submit main command list");

			VALIDATE(CommandList->Close());
			FrameFenceValues[RenderContext.mCurrentBackBufferIndex] = RenderContext.ExecuteGraphics(CommandList.Get(), Fence, CurrentFenceValue);
		}

#if TRACY_ENABLE || CAPTURE_SCREEN
		{
			ReadBackData NewItem;
			NewItem.BufferIndex = RenderContext.mCurrentBackBufferIndex;
			NewItem.FenceValue = FrameFenceValues[RenderContext.mCurrentBackBufferIndex];
			ReadBackQueue.push_back(NewItem);
		}
#endif

		{
			ZoneScopedN("Present");
			RenderContext.Present();
		}
		RenderContext.mCurrentBackBufferIndex = (RenderContext.mCurrentBackBufferIndex + 1) % Render::Context::BUFFER_COUNT;
		//Flush(RenderContext.mGraphicsQueue, FrameFences[RenderContext.mCurrentBackBufferIndex - 1], CurrentFenceValue, WaitEvent);
	}

	Importer.SetProgressHandler(nullptr);
	Helper.ShouldProceed = false;

	if (RenderContext.mCurrentBackBufferIndex == 0)
	{
		RenderContext.mCurrentBackBufferIndex = Render::Context::BUFFER_COUNT;
	}
	Flush(RenderContext.mGraphicsQueue, FrameFences[RenderContext.mCurrentBackBufferIndex - 1], CurrentFenceValue, WaitEvent);
	RenderContext.WaitForUploadFinish();
	CloseHandle(WaitEvent);
}

