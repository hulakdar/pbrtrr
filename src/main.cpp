#include "Util/Util.h"
#include "Util/Debug.h"
#include "Util/Allocator.h"
#include "Containers/Queue.h"
#include "Containers/ComPtr.h"
#include "Containers/String.h"
#include "System/GUI.h"
#include "System/Window.h"
#include "System/Thread.h"
#include "Render/Context.h"
#include "Render/Texture.h"
#include "Render/RenderDebug.h"
#include "Render/CommandListPool.h"
#include "Threading/Mutex.h"
#include "Threading/Worker.h"

#include "external/stb/stb_image.h"

#include <WinPixEventRuntime/pix3.h>
#include <EASTL/algorithm.h>

#include <atomic>
#include <condition_variable>

#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/ProgressHandler.hpp>
#include <TracyD3D12.hpp>

#include <imnodes.h>

struct Camera
{
	float Fov = FLT_MAX;
	float Near = FLT_MAX;
	float Far = FLT_MAX;
	Vec3 Eye;
	Vec2 Angles;
};

struct MeshData
{
	ComPtr<ID3D12Resource>	VertexBuffer;
	ComPtr<ID3D12Resource>	IndexBuffer;
	uint32_t				VertexBufferSize = UINT_MAX;
	uint32_t				IndexBufferSize = UINT_MAX;
	uint32_t				VertexSize = UINT_MAX;

	Vec3 Offset{0};
	Vec3 Scale{0};
	bool b16BitIndeces = false;
};

struct Material
{
	String Name;
	TArray<uint32_t> DiffuseTextures;
};

TextureData ParseTexture(aiTexture* Texture)
{
	ZoneScoped;
	TextureData Result;
	uint8_t* Data = nullptr;

	if (Texture->mHeight == 0)
	{
		int Channels = 0;
		Data = stbi_load_from_memory(
			(uint8_t*)Texture->pcData,
			Texture->mWidth,
			&Result.Width, &Result.Height,
			&Channels, 4);
		Result.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	}
	else
	{
		DEBUG_BREAK();
	}
	CreateTexture(Result, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
	UploadTextureData(Result, Data, Result.Width * Result.Height * 4);
	return Result;
}

void AllocateMeshData(aiMesh* Mesh, MeshData& Result, bool PositionPacked)
{
	ZoneScoped;
	CHECK(Mesh->HasFaces(), "Mesh without faces?");

	UINT VertexSize = sizeof(Vec4h);
	if (PositionPacked)
	{
		VertexSize = sizeof(Vec4PackUnorm);
	}

	//if (Mesh->HasNormals())
	//{
		//VertexSize += sizeof(Vec4h);
	//}

	//UINT VertexColors = 0;
	//while (Mesh->HasVertexColors(VertexColors))
	//{
		//VertexSize += 4;
		//VertexColors++;
	//}

	UINT UVSets = 0;
	while (Mesh->HasTextureCoords(UVSets))
	{
		VertexSize += sizeof(half) * 2;
		UVSets++;
	}

	UINT VertexBufferSize = VertexSize * Mesh->mNumVertices;

	bool b16BitIndeces = Mesh->mNumVertices <= 65535;
	UINT IndexBufferSize = Mesh->mNumFaces * 3 * (b16BitIndeces ? sizeof(uint16_t) : sizeof(uint32_t));

	CHECK(VertexBufferSize != 0, "??");
	CHECK(IndexBufferSize != 0, "??");

	Result.VertexBuffer = CreateBuffer(VertexBufferSize);
	Result.IndexBuffer = CreateBuffer(IndexBufferSize);
	Result.VertexBufferSize = VertexBufferSize;
	Result.IndexBufferSize = IndexBufferSize;
	Result.VertexSize = VertexSize;
	Result.b16BitIndeces = b16BitIndeces;
}

void UploadMeshData(unsigned char* CpuPtr, aiMesh* Mesh, bool PositionPacked)
{
	ZoneScoped;
	Vec3 Min = Vec3{
		Mesh->mAABB.mMin.x - 0.001f,
		Mesh->mAABB.mMin.y - 0.001f,
		Mesh->mAABB.mMin.z - 0.001f,
	};
	Vec3 Max = Vec3{
		Mesh->mAABB.mMax.x + 0.001f,
		Mesh->mAABB.mMax.y + 0.001f,
		Mesh->mAABB.mMax.z + 0.001f,
	};
	for (UINT i = 0; i < Mesh->mNumVertices; ++i)
	{
		if (PositionPacked)
		{
			Vec4 Normalized{
				(Mesh->mVertices[i].x - Min.x) / (Max.x - Min.x),
				(Mesh->mVertices[i].y - Min.y) / (Max.y - Min.y),
				(Mesh->mVertices[i].z - Min.z) / (Max.z - Min.z),
				1.f
			};
			WriteAndAdvance(CpuPtr, Vec4PackUnorm(&Normalized.x));
		}
		else
		{
			WriteAndAdvance(CpuPtr,
				Vec4h{
					half(Mesh->mVertices[i].x),
					half(Mesh->mVertices[i].y),
					half(Mesh->mVertices[i].z),
					1.f
				}
			);
		}

		//if (Mesh->mNormals)
		//{
			//WriteAndAdvance(CpuPtr,
				//Vec4h{
					//half(Mesh->mNormals[i].x),
					//half(Mesh->mNormals[i].y),
					//half(Mesh->mNormals[i].z),
					//1.f
				//}
			//);
		//}

		//for (int j = 0; Mesh->mColors[j]; ++j)
		//{
			//WriteAndAdvance(CpuPtr, (uint8_t)(Mesh->mColors[j][i].r * 255.0f));
			//WriteAndAdvance(CpuPtr, (uint8_t)(Mesh->mColors[j][i].g * 255.0f));
			//WriteAndAdvance(CpuPtr, (uint8_t)(Mesh->mColors[j][i].b * 255.0f));
			//WriteAndAdvance(CpuPtr, (uint8_t)(Mesh->mColors[j][i].a * 255.0f));
		//}

		for (int j = 0; Mesh->mTextureCoords[j]; ++j)
		{
			WriteAndAdvance(CpuPtr, Vec2h{
				half(Mesh->mTextureCoords[j][i].x),
				half(Mesh->mTextureCoords[j][i].y),
			});
		}
	}

	bool b16BitIndeces = Mesh->mNumVertices <= 65535;
	if (b16BitIndeces)
	{
		for (UINT i = 0; i < Mesh->mNumFaces; ++i)
		{
			CHECK(Mesh->mFaces[i].mNumIndices == 3, "Not triangles?");
			IVec3 Triangle = *(IVec3*)Mesh->mFaces[i].mIndices;
			WriteAndAdvance(CpuPtr, (uint16_t)Triangle.x);
			WriteAndAdvance(CpuPtr, (uint16_t)Triangle.y);
			WriteAndAdvance(CpuPtr, (uint16_t)Triangle.z);
		}
	}
	else
	{
		for (UINT i = 0; i < Mesh->mNumFaces; ++i)
		{
			CHECK(Mesh->mFaces[i].mNumIndices == 3, "Not triangles?");
			WriteAndAdvance(CpuPtr, *(IVec3*)Mesh->mFaces[i].mIndices);
		}
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

TracyD3D12Ctx	gGraphicsProfilingCtx;
TracyD3D12Ctx	gComputeProfilingCtx;
TracyD3D12Ctx	gCopyProfilingCtx;

int main(void)
{
	StartDebugSystem();

	gMainThreadID = CurrentThreadID();
	StartWorkerThreads();

	System::Window Window;
	Window.Init();

	InitRender(Window);
	StartRenderThread();

	gGraphicsProfilingCtx = TracyD3D12Context(GetGraphicsDevice(), GetGraphicsQueue());
	//gComputeProfilingCtx = TracyD3D12Context(gDevice.Get(), gComputeQueue.Get());
	//gCopyProfilingCtx = TracyD3D12Context(gDevice.Get(), gCopyQueue.Get());

	InitGUI(Window);

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
	MainCamera.Far = 1000;
	MainCamera.Eye = Vec3{ 0, 0, -2 };
	MainCamera.Angles = Vec2{ 0, 0 };

	TArray<Mesh> Meshes;
	TArray<Material> Materials;
	TArray<MeshData> MeshDatas;
	TArray<TextureData> Textures;

	{
#if 1
		String FilePath = "content/DamagedHelmet.glb";
#else
		String FilePath = "content/Bistro/BistroExterior.fbx";
#endif
		const aiScene* Scene = nullptr;
		{
			ZoneScopedN("Scene file parsing");
			Scene = Importer.ReadFile(
				FilePath.c_str(),
				aiProcess_GenBoundingBoxes | aiProcess_FlipWindingOrder | aiProcessPreset_TargetRealtime_Quality
			);
			if (Helper.ShouldProceed == false)
			{
				return 0;
			}
			CHECK(Scene != nullptr, "Load failed");
		}

		if (Scene->HasMaterials())
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
					if (TexPath.length && TexPath.data[0] == '*')
					{
						uint32_t Index = atoi(TexPath.C_Str() + 1);
						Tmp.DiffuseTextures.push_back(Index);
					}
					else if (TexPath.length)
					{
						StringView MeshFolder(FilePath.c_str(), FilePath.find_last_of("\\/"));

						String Path(MeshFolder);
						Path.append("/");
						Path.append(TexPath.data, TexPath.length);

						Textures.emplace_back();
						auto& Tex = Textures.back();

						StringView Binary = LoadWholeFile(Path);
						if (uint8_t* Data = (uint8_t*)Binary.data())
						{
							UINT Magic = ReadAndAdvance<UINT>(Data);
							CHECK(Magic == DDS_MAGIC, "This is not a valid DDS");

							DDS_HEADER Header = ReadAndAdvance<DDS_HEADER>(Data);
							if (Header.ddspf.dwFlags & DDPF_FOURCC)
							{
								if (Header.ddspf.dwFourCC == MAGIC(DX10))
								{
									DDS_HEADER_DXT10 HeaderDX10 = ReadAndAdvance<DDS_HEADER_DXT10>(Data);
								}
								else if (Header.ddspf.dwFourCC == MAGIC(DXT1))
								{
									Tex.Format = DXGI_FORMAT_BC1_UNORM;
								}
								else if (Header.ddspf.dwFourCC == MAGIC(DXT3))
								{
									Tex.Format = DXGI_FORMAT_BC2_UNORM;
								}
								else if (Header.ddspf.dwFourCC == MAGIC(DXT5))
								{
									Tex.Format = DXGI_FORMAT_BC3_UNORM;
								}
								else if (Header.ddspf.dwFourCC == MAGIC(BC4U))
								{
									Tex.Format = DXGI_FORMAT_BC4_UNORM;
								}
								else if (Header.ddspf.dwFourCC == MAGIC(BC4S))
								{
									Tex.Format = DXGI_FORMAT_BC4_SNORM;
								}
								else
								{
									DEBUG_BREAK();
								}

								Tex.Width = Header.dwWidth;
								Tex.Height = Header.dwHeight;
								CreateTexture(Tex, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
								UploadTextureData(Tex, Data, Header.dwPitchOrLinearSize);
								CreateSRV(Tex);
							}
						}
						else
						{
							DEBUG_BREAK();
						}
					}
					else
					{
						DEBUG_BREAK();
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
					AllocateMeshData(Mesh, Tmp, true);
					Tmp.Offset = Vec3{
						Mesh->mAABB.mMin.x,
						Mesh->mAABB.mMin.y,
						Mesh->mAABB.mMin.z,
					};
					Tmp.Scale = Vec3{
						(Mesh->mAABB.mMax.x)-(Mesh->mAABB.mMin.x),
						(Mesh->mAABB.mMax.y)-(Mesh->mAABB.mMin.y),
						(Mesh->mAABB.mMax.z)-(Mesh->mAABB.mMin.z),
					};
					//Tmp.MaterialIndex = Mesh->mMaterialIndex;

					UploadOffsets[i] = UploadBufferSize;
					UploadBufferSize += Tmp.IndexBufferSize + Tmp.VertexBufferSize;
				}

				UploadBuffer = CreateBuffer(UploadBufferSize, true);

				{
					ZoneScopedN("Upload")

					unsigned char* CpuPtr = NULL;
					UploadBuffer->Map(0, NULL, (void**)&CpuPtr);
					for (UINT i = 0; i < Scene->mNumMeshes; ++i)
					{
						aiMesh* Mesh = Scene->mMeshes[i];
						UploadMeshData(CpuPtr + UploadOffsets[i], Mesh, true);
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
					aiNode*& Current = ProcessingQueue.front().first;
					aiMatrix4x4& ParentTransform = ProcessingQueue.front().second;

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
						MainCamera.Eye = Vec3{ Location.x, Location.y, Location.z };
					}
					ProcessingQueue.pop();
				}
			}

			D3D12CmdList WorkerCommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, 0);

			{
				TracyD3D12Zone(gGraphicsProfilingCtx, WorkerCommandList.Get(), "Copy Mesh Data to GPU");
				PIXScopedEvent(WorkerCommandList.Get(), __LINE__, "Copy Mesh Data to GPU");
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

			UINT64 WorkerFenceValue = 0;

			TArray<CD3DX12_RESOURCE_BARRIER> barriers;
			barriers.reserve(Scene->mNumMeshes * 2);
			for (UINT i = 0; i < Scene->mNumMeshes; ++i)
			{
				MeshData& Data = MeshDatas[i];
				barriers.push_back(
					CD3DX12_RESOURCE_BARRIER::Transition(
						Data.VertexBuffer.Get(),
						D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
						D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
					)
				);
				barriers.push_back(
					CD3DX12_RESOURCE_BARRIER::Transition(
						Data.IndexBuffer.Get(),
						D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER,
						D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
					)
				);
			}
			WorkerCommandList->ResourceBarrier((UINT)barriers.size(), barriers.data());

			Submit(WorkerCommandList, 0);

			ComPtr<ID3D12Fence> WorkerFrameFence = CreateFence();
			HANDLE WorkerWaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			FlushQueue(GetGraphicsQueue(), WorkerFrameFence.Get(), WorkerFenceValue, WorkerWaitEvent);
			CloseHandle(WorkerWaitEvent);
		}
	}

	ComPtr<ID3D12PipelineState> SimplePSO;
	{
		TArray<D3D12_INPUT_ELEMENT_DESC> PSOLayout;

		D3D12_INPUT_ELEMENT_DESC Tmp = {};

		Tmp.SemanticName = "POSITION";
		Tmp.SemanticIndex = 0;
		Tmp.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
		Tmp.InputSlot = 0;
		Tmp.AlignedByteOffset = 0;
		PSOLayout.push_back(Tmp);

		Tmp.SemanticName = "TEXCOORD";
		Tmp.SemanticIndex = 0;
		Tmp.Format = DXGI_FORMAT_R16G16_FLOAT;
		Tmp.InputSlot = 0;
		Tmp.AlignedByteOffset = 4;
		PSOLayout.push_back(Tmp);

		DXGI_FORMAT RenderTargetFormat = SCENE_COLOR_FORMAT;
		TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
		SimplePSO = CreateShaderCombination(
			PSOLayout,
			EntryPoints,
			"content/shaders/Simple.hlsl",
			&RenderTargetFormat,
			DXGI_FORMAT_D24_UNORM_S8_UINT,
			nullptr
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

		DXGI_FORMAT RenderTargetFormat = BACK_BUFFER_FORMAT;
		TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
		BlitPSO = CreateShaderCombination(
			&InputDesc,
			EntryPoints,
			"content/shaders/Blit.hlsl",
			&RenderTargetFormat
		);
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
		DXGI_FORMAT RenderTargetFormat = READBACK_FORMAT;
		DownsampleRasterPSO = CreateShaderCombination(
			&InputDesc, 
			EntryPoints,
			"content/shaders/Blit.hlsl",
			&RenderTargetFormat
		);
	}

	TextureData DefaultTexture;
	{
		uint8_t *Data = stbi_load("content/uvcheck.jpg", &DefaultTexture.Width, &DefaultTexture.Height, nullptr, 4);
		DefaultTexture.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

		CreateTexture(DefaultTexture, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
		UploadTextureData(DefaultTexture, Data);
		CreateSRV(DefaultTexture);
		stbi_image_free(Data);
	}

	MeshData Quad;
	{
		uint16_t IndexData[] = {
			0, 1, 2,
		};

		Vec2 VertexData[] = {
			{-1, -3},
			{-1, 1},
			{3, 1},
		};

		Quad.IndexBuffer = CreateBuffer(sizeof(IndexData));
		Quad.IndexBufferSize = sizeof(IndexData);
		UploadBufferData(Quad.IndexBuffer.Get(), IndexData, sizeof(IndexData), D3D12_RESOURCE_STATE_INDEX_BUFFER);
		SetD3DName(Quad.IndexBuffer, L"Quad IndexBuffer");

		Quad.VertexBuffer = CreateBuffer(sizeof(VertexData));
		Quad.VertexBufferSize = sizeof(VertexData);
		Quad.VertexSize = sizeof(Vec2);
		UploadBufferData(Quad.VertexBuffer.Get(), VertexData, sizeof(VertexData), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		SetD3DName(Quad.VertexBuffer, L"Quad VertexBuffer");
	}

	FlushUpload();

	HANDLE WaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	ComPtr<ID3D12Fence> FrameFences[3] = {};
	for (int i = 0; i < 3; ++i)
	{
		FrameFences[i] = CreateFence();

		SetD3DName(FrameFences[i], L"Fence %d", i);
	}

	std::atomic<bool> ReadbackInflight;
	std::atomic<bool> WorkerInProgress;
	UINT64  ReadBackReadyFence = 0;

	TextureData SceneColor = {};
	{
		SceneColor.Format = SCENE_COLOR_FORMAT;
		SceneColor.Width = Window.mSize.x;
		SceneColor.Height = Window.mSize.y;

		D3D12_CLEAR_VALUE ClearValue = {};
		FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
		memcpy(ClearValue.Color, clearColor, sizeof(clearColor));
		ClearValue.Format = SCENE_COLOR_FORMAT;

		CreateTexture(SceneColor,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&ClearValue
		);
		CreateSRV(SceneColor);
		CreateRTV(SceneColor);
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
		DepthBuffer.Width = Window.mSize.x;
		DepthBuffer.Height = Window.mSize.y;

		CreateTexture(DepthBuffer, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ClearValue);
		CreateDSV(DepthBuffer);
	}

	TextureData SceneColorSmall = {};
	{
		SceneColorSmall.Format = READBACK_FORMAT;
		SceneColorSmall.Width  = 960;
		SceneColorSmall.Height = 540;
		CreateTexture(SceneColorSmall,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			nullptr
		);
		CreateRTV(SceneColorSmall);
	}

	ComPtr<ID3D12Resource>	SceneColorStagingBuffer = CreateBuffer(SceneColorSmall.Width * SceneColorSmall.Height * 4, false, true);

	UINT64 ImVertexHighwatermarks[BACK_BUFFER_COUNT] = {};
	UINT64 ImIndexHighwatermarks[BACK_BUFFER_COUNT] = {};
	ComPtr<ID3D12Resource> ImGuiVertexBuffers[BACK_BUFFER_COUNT];
	ComPtr<ID3D12Resource> ImGuiIndexBuffers[BACK_BUFFER_COUNT];
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
		DXGI_FORMAT RenderTargetFormat = BACK_BUFFER_FORMAT;

		D3D12_RENDER_TARGET_BLEND_DESC GuiBlendDesc =
		{
			TRUE, FALSE,
			D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
			D3D12_BLEND_ZERO, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_LOGIC_OP_NOOP,
			D3D12_COLOR_WRITE_ENABLE_ALL,
		};

		GuiPSO = CreateShaderCombination(
			PSOLayout,
			EntryPoints,
			"content/shaders/GUI.hlsl",
			&RenderTargetFormat,
			DXGI_FORMAT_UNKNOWN,
			&GuiBlendDesc
		);

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
	FlushUpload();

	struct RenderGraph
	{
		enum NodeType
		{
			Output,
			GeometryPass,
			FullscreenPass,
		};
		struct NodeResult
		{
			ComPtr<ID3D12Resource> Resource;
			D3D12_RESOURCE_STATES ResourceState;
		};
		struct Node
		{
			TFunction<void (struct RenderGraph&, struct Node&)> Work;
			TArray<uint16_t> Inputs;
			TArray<uint16_t> Outputs;
			NodeType Type = NodeType::Output;
		};
		struct Edge
		{
			int ID;
			int StartID;
			int EndID;
		};

		TArray<Node> Nodes;
		TArray<Edge> Edges;
		TArray<bool> ValidNodes;
	};

	RenderGraph Graph;

	auto AddNode = [&Graph]() -> RenderGraph::Node& {
		for (int i = 0; i < Graph.ValidNodes.size(); ++i)
		{
			if (!Graph.ValidNodes[i])
			{
				Graph.ValidNodes[i] = true;
				return Graph.Nodes[i];
			}
		}

		Graph.ValidNodes.push_back(true);
		return Graph.Nodes.push_back();
	};

	auto RemoveNode = [&Graph](int NodeID) {
		Graph.ValidNodes[NodeID] = false;
		Graph.Edges.erase(
			eastl::remove_if(Graph.Edges.begin(), Graph.Edges.end(),
			[NodeID](RenderGraph::Edge& Edge) {
				return Edge.StartID == NodeID || Edge.EndID == NodeID;
			}),
			Graph.Edges.end()
		);
	};

	uint64_t CurrentFenceValue = 0;
	uint64_t FrameFenceValues[3] = {};

	std::atomic<UINT64> LastCompletedFence = 0;
	std::atomic<uint64_t> QueuedFrameCount = 0;

	while (!glfwWindowShouldClose(Window.mHandle))
	{
		FrameMark;

		if (Window.mWindowStateDirty)
		{
			ZoneScopedN("Window state dirty");

			// Wait for render thread AND gpu
			Ticket WaitForAll = EnqueueRenderThreadWork([
				FrameFence = FrameFences[CurrentBackBufferIndex].Get(),
				&CurrentFenceValue,
				WaitEvent
			]() {
				ZoneScopedN("Wait for render thread AND gpu");
				FlushQueue(GetGraphicsQueue(), FrameFence, CurrentFenceValue, WaitEvent);
			});

			WaitForCompletion(WaitForAll);
			CreateBackBufferResources(Window);
			Window.mWindowStateDirty = false;
			CurrentBackBufferIndex = 0;
		}

		UpdateGUI(Window);

		ImGui::NewFrame();

		if (!WorkerInProgress && ReadbackInflight && ReadBackReadyFence < LastCompletedFence)
		{
			ZoneScopedN("Readback screen capture");

			WorkerInProgress = true;
			EnqueueToWorker(
				[
					&ReadbackInflight,
					&WorkerInProgress,
					ResourcePtr = SceneColorStagingBuffer.Get(),
					x = SceneColorSmall.Width, y = SceneColorSmall.Height
				]
				() {
					ComPtr<ID3D12Resource>;

					UINT   RowCount;
					UINT64 RowPitch;
					UINT64 ResourceSize;
					D3D12_RESOURCE_DESC Desc = ResourcePtr->GetDesc();
					GetGraphicsDevice()->GetCopyableFootprints(&Desc, 0, 1, 0, NULL, &RowCount, &RowPitch, &ResourceSize);

					ZoneScopedN("Uploading screenshot to Tracy");
					D3D12_RANGE Range;
					Range.Begin = 0;
					Range.End = (UINT64)ResourceSize;
					void* Data = nullptr;
					VALIDATE(ResourcePtr->Map(0, &Range, &Data));
					FrameImage(Data, (uint16_t)x, (uint16_t)y, 3, false);
					ResourcePtr->Unmap(0, NULL);

					WorkerInProgress = false;
					ReadbackInflight = false;
				}
			);
		}

		static bool bRenderGraphOpen = true;

		{
			//ImGui::SetNextWindowSize(ImVec2(500, 500));
			if (ImGui::Begin("RenderGraph", &bRenderGraphOpen))
			{
				ZoneScopedN("RenderGraphEditor");

				int StartNodeID, EndNodeID, StartAttribID, EndAttribID;
				bool FromSnap;
				if (ImNodes::IsLinkCreated(&StartNodeID, &StartAttribID, &EndNodeID, &EndAttribID, &FromSnap))
				{
					static int UID;
					RenderGraph::Edge& Edge = Graph.Edges.push_back();
					Edge.ID = UID++;
					Edge.StartID = StartAttribID;
					Edge.EndID = EndAttribID;
				}

				{
					int LinkID;
					if (ImNodes::IsLinkHovered(&LinkID) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						Graph.Edges.erase(
							eastl::remove_if(Graph.Edges.begin(), Graph.Edges.end(),
								[LinkID](RenderGraph::Edge& Edge) {return Edge.ID == LinkID; }),
							Graph.Edges.end()
						);
					}
				}

				if (ImGui::IsKeyPressed(ImGuiKey_Delete))
				{
					if (int NodesNum = ImNodes::NumSelectedNodes())
					{
						TArray<int> Nodes;
						Nodes.resize(NodesNum);
						ImNodes::GetSelectedNodes(Nodes.data());
						for (int i : Nodes)
						{
							RemoveNode(i);
						}
					}
					if (int LinksNum = ImNodes::NumSelectedLinks())
					{
						TArray<int> Links;
						Links.resize(LinksNum);
						ImNodes::GetSelectedNodes(Links.data());

						Graph.Edges.erase(
							eastl::remove_if(Graph.Edges.begin(), Graph.Edges.end(),
								[&Links](RenderGraph::Edge& Edge) {
									return std::find(Links.begin(),Links.end(), Edge.ID) != Links.end();
								}),
							Graph.Edges.end()
						);
					}
				}

				ImNodes::BeginNodeEditor();

				if (ImNodes::IsEditorHovered()
				&& ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right))
				{
					ImGui::OpenPopup("RightClickNodePopup");
				}
				if (ImGui::BeginPopup("RightClickNodePopup"))
				{
					static unsigned short AttribID;
					size_t ID = Graph.Nodes.size();
					if (ImGui::MenuItem("Output"))
					{
						RenderGraph::Node& Node = AddNode();
						Node.Type = RenderGraph::Output;
						Node.Inputs.push_back(AttribID++);
						ImNodes::SetNodeScreenSpacePos((int)ID, ImGui::GetMousePos());
					}
					if (ImGui::MenuItem("GeometryPass"))
					{
						RenderGraph::Node& Node = AddNode();
						Node.Type = RenderGraph::GeometryPass;
						Node.Inputs.push_back(AttribID++);
						Node.Outputs.push_back(AttribID++);
						ImNodes::SetNodeScreenSpacePos((int)ID, ImGui::GetMousePos());
					}
					if (ImGui::MenuItem("FullscreenPass"))
					{
						RenderGraph::Node& Node = Graph.Nodes.push_back();
						Node.Type = RenderGraph::FullscreenPass;
						Node.Inputs.push_back(AttribID++);
						Node.Outputs.push_back(AttribID++);
						ImNodes::SetNodeScreenSpacePos((int)ID, ImGui::GetMousePos());
					}
					ImGui::EndPopup();
				}

				for (int i = 0; i < Graph.Nodes.size(); ++i)
				{
					RenderGraph::Node& Node = Graph.Nodes[i];
					ImNodes::BeginNode(i);

					if (Node.Type == RenderGraph::Output)
					{
						ImNodes::BeginNodeTitleBar();
						ImGui::Text("Output (backbuffer)");
						ImNodes::EndNodeTitleBar();

						for (int In : Node.Inputs)
						{
							ImNodes::BeginInputAttribute(In);
							ImGui::TextUnformatted("Backbuffer");
							ImNodes::EndInputAttribute();
						}
					}
					else if (Node.Type == RenderGraph::GeometryPass)
					{
						ImNodes::BeginNodeTitleBar();
						ImGui::Text("Geometry pass");
						ImNodes::EndNodeTitleBar();

						static int current = 0;
						static const char* items[] = {
							"Forward",
							"GBuffer",
						};

						ImGui::PushItemWidth(100);
						ImGui::Combo("Shader:", &current, items, ArrayCount(items));
						ImGui::PopItemWidth();

						for (int In : Node.Inputs)
						{
							ImNodes::BeginInputAttribute(In);
							ImGui::TextUnformatted("");
							ImNodes::EndInputAttribute();
						}

						for (int Out : Node.Outputs)
						{
							ImNodes::BeginOutputAttribute(Out);
							ImGui::TextUnformatted("");
							ImNodes::EndOutputAttribute();
						}
					}
					else if (Node.Type == RenderGraph::FullscreenPass)
					{
						ImNodes::BeginNodeTitleBar();
						ImGui::Text("Fullscreen pass");
						ImNodes::EndNodeTitleBar();

						for (int In : Node.Inputs)
						{
							ImNodes::BeginInputAttribute(In);
							ImGui::TextUnformatted("");
							ImNodes::EndInputAttribute();
						}

						static int current = 0;
						static const char* items[] = {
							"Downsample",
							"",
						};

						ImGui::PushItemWidth(100);
						ImGui::Combo("Shader:", &current, items, ArrayCount(items));
						ImGui::PopItemWidth();

						for (int Out : Node.Outputs)
						{
							ImNodes::BeginOutputAttribute(Out);
							ImGui::TextUnformatted("");
							ImNodes::EndOutputAttribute();
						}
					}
					ImNodes::EndNode();
				}

				for (int i = 0; i < Graph.Edges.size(); ++i)
				{
					RenderGraph::Edge& Edge = Graph.Edges[i];

					//ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding, 1);
					ImNodes::Link(Edge.ID, Edge.StartID, Edge.EndID);
				}

				ImNodes::EndNodeEditor();
				ImGui::End();
			}
		}

		{
			Window.Update();
			FlushUpload();
			ZoneScopedN("Wait for free backbuffer in swapchain");
			//while (!IsSwapChainReady())
			//while (FenceWithDelay - LastCompletedFence > 2)
			while (QueuedFrameCount > 2)
			{
				if (!TryPopAndExecute(GetWorkerDedicatedThreadData()))
				{
					//Sleep(1);
				}
			}
		}

		EnqueueRenderThreadWork(
			[
				&CurrentFenceValue,
				&LastCompletedFence,
				&SceneColor,
				&DepthBuffer,
				&FrameFences,
				&WaitEvent,
				&FrameFenceValues,
				CurrentBackBufferIndex
			]() {
				PIXBeginEvent(GetGraphicsQueue(), __LINE__, "FRAME");

				UINT64 FenceValue = FrameFenceValues[CurrentBackBufferIndex];
				{
					ZoneScopedN("Wait for fence");
					WaitForFenceValue(FrameFences[CurrentBackBufferIndex].Get(), FenceValue, WaitEvent);
				}
				FlushUpload();
				LastCompletedFence = FenceValue;

				TracyD3D12NewFrame(gGraphicsProfilingCtx);
				ZoneScopedN("New frame");

				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
				{
					CD3DX12_RESOURCE_BARRIER barriers[] = {
						// backbuffer(present -> render)
						CD3DX12_RESOURCE_BARRIER::Transition(
							GetBackBufferResource(CurrentBackBufferIndex),
							D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET,
							D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
						),

						// SceneColor(srv -> render)
						CD3DX12_RESOURCE_BARRIER::Transition(
							GetTextureResource(SceneColor.ID),
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_RENDER_TARGET
						),
					};
					CommandList->ResourceBarrier(ArrayCount(barriers), barriers);
				}

				{
					CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)SceneColor.Width, (float)SceneColor.Height);
					CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

					CommandList->RSSetViewports(1, &SceneColorViewport);
					CommandList->RSSetScissorRects(1, &SceneColorScissor);

					BindRenderTargets(CommandList.Get(), {SceneColor.RTV}, DepthBuffer.DSV);
					{
						FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
						ClearRenderTarget(CommandList.Get(), SceneColor.RTV, clearColor);
					}
					ClearDepth(CommandList.Get(), DepthBuffer.DSV, 1.0f);
				}
				Submit(CommandList, CurrentFenceValue);
			}
		);

		// Mesh
		{
			ImGui::SliderFloat("FOV", &MainCamera.Fov, 5, 160);
			ImGui::SliderFloat("Near", &MainCamera.Near, 0.01, 3);
			ImGui::SliderFloat("Far", &MainCamera.Far, 10, 1000);
			ImGui::DragFloat3("Eye", &MainCamera.Eye.x);
			ImGui::DragFloat2("Angles", &MainCamera.Angles.x, 0.05);

			if (ImGui::IsKeyDown(ImGuiKey_W))
			{
				MainCamera.Eye.z += 0.01;
			}
			if (ImGui::IsKeyDown(ImGuiKey_S))
			{
				MainCamera.Eye.z -= 0.01;
			}
			if (ImGui::IsKeyDown(ImGuiKey_A))
			{
				MainCamera.Eye.x += 0.01;
			}
			if (ImGui::IsKeyDown(ImGuiKey_D))
			{
				MainCamera.Eye.x -= 0.01;
			}
			if (ImGui::IsKeyDown(ImGuiKey_Q))
			{
				MainCamera.Eye.y += 0.01;
			}
			if (ImGui::IsKeyDown(ImGuiKey_E))
			{
				MainCamera.Eye.y -= 0.01;
			}

			EnqueueRenderThreadWork(
				[
					MainCamera,
					&DefaultTexture,
					&SimplePSO,
					&SceneColor,
					&DepthBuffer,
					&Window,
					&Meshes,
					&MeshDatas,
					&CurrentFenceValue
				]()
				{
					D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
					{
						TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Render Meshes");
						PIXScopedEvent(CommandList.Get(), __LINE__, "Render Meshes");
						ZoneScopedN("Drawing meshes");

						BindDescriptors(CommandList.Get(), DefaultTexture);
						BindRenderTargets(CommandList.Get(), {SceneColor.RTV}, DepthBuffer.DSV);

						float Fov = MainCamera.Fov;
						float Near = MainCamera.Near;
						float Far = MainCamera.Far;
						Vec2 Angles = MainCamera.Angles;
						Vec3 Eye = MainCamera.Eye;

						CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)SceneColor.Width, (float)SceneColor.Height);
						CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

						CommandList->RSSetViewports(1, &SceneColorViewport);
						CommandList->RSSetScissorRects(1, &SceneColorScissor);

						CommandList->SetPipelineState(SimplePSO.Get());
						CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
						Matrix4 Projection = CreatePerspectiveMatrix(DegreesToRadians(Fov), (float)Window.mSize.x / (float)Window.mSize.y, Near, Far);
						Matrix4 View = CreateViewMatrix(-Eye, Angles);
						Matrix4 VP = View * Projection;

						for (auto& Mesh : Meshes)
						{
							for (UINT ID : Mesh.MeshIDs)
							{
								MeshData& MeshData = MeshDatas[ID];
								Matrix4 Scale = CreateScaleMatrix(MeshData.Scale);
								Matrix4 Translation = CreateTranslationMatrix(MeshData.Offset);

								Matrix4 Combined = Scale * Translation * Mesh.Transform * VP;

								CommandList->SetGraphicsRoot32BitConstants(1, sizeof(Combined)/4, &Combined, 0);

								D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
								VertexBufferView.BufferLocation = MeshData.VertexBuffer->GetGPUVirtualAddress();
								VertexBufferView.SizeInBytes = MeshData.VertexBufferSize;
								VertexBufferView.StrideInBytes = MeshData.VertexSize;
								CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);

								D3D12_INDEX_BUFFER_VIEW IndexBufferView;
								IndexBufferView.BufferLocation = MeshData.IndexBuffer->GetGPUVirtualAddress();
								IndexBufferView.SizeInBytes = MeshData.IndexBufferSize;
								IndexBufferView.Format = MeshData.b16BitIndeces ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
								CommandList->IASetIndexBuffer(&IndexBufferView);

								CommandList->DrawIndexedInstanced(MeshData.IndexBufferSize / (MeshData.b16BitIndeces ? 2 : 4), 1, 0, 0, 0);
							}
						}
					}
					Submit(CommandList, CurrentFenceValue);
				}
			);
		}

		EnqueueRenderThreadWork(
			[
				&SceneColor,
				&BlitPSO,
				&Quad,
				&Window,
				&CurrentFenceValue,
				CurrentBackBufferIndex
			]
			() {
				ZoneScopedN("SceneColor(render -> srv)");
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
				{
					CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						GetTextureResource(SceneColor.ID),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

					CommandList->ResourceBarrier(1, &barrier);
				}
				{
					TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Blit scenecolor to backbuffer");
					PIXScopedEvent(CommandList.Get(), __LINE__, "Blit scenecolor to backbuffer");

					BindDescriptors(CommandList.Get(), SceneColor);

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

					BindRenderTargets(CommandList.Get(), {CurrentBackBufferIndex}, -1);

					CD3DX12_VIEWPORT	Viewport    = CD3DX12_VIEWPORT(0.f, 0.f, (float)Window.mSize.x, (float)Window.mSize.y);
					CD3DX12_RECT		ScissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
					CommandList->RSSetViewports(1, &Viewport);
					CommandList->RSSetScissorRects(1, &ScissorRect);

					CommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);
				}
				Submit(CommandList, CurrentFenceValue);
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
			TArray<ImDrawList*> DrawLists;
			for (int i = 0; i < DrawData->CmdListsCount; ++i)
			{
				ImDrawList* ImGuiCmdList = DrawData->CmdLists[i];
				DrawLists.push_back(ImGuiCmdList->CloneOutput());
			}

			EnqueueRenderThreadWork(
				[
					TotalVtxCount = DrawData->TotalVtxCount,
					TotalIdxCount = DrawData->TotalIdxCount,
					DL = MOVE(DrawLists),
					&GuiPSO,
					&ImGuiIndexBuffer,
					&ImGuiVertexBuffer,
					&WatermarkVertex,
					&WatermarkIndex,
					&CurrentFenceValue,
					CurrentBackBufferIndex
				]()
			{
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
				{
					ZoneScopedN("Render GUI");
					TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Render GUI");
					PIXScopedEvent(CommandList.Get(), __LINE__, "Render GUI");

					UINT64 VertexBufferSize = TotalVtxCount * sizeof(ImDrawVert);
					UINT64 IndexBufferSize = TotalIdxCount * sizeof(ImDrawIdx);

					if (VertexBufferSize > WatermarkVertex)
					{
						ImGuiVertexBuffer = CreateBuffer(VertexBufferSize, true);
						WatermarkVertex = VertexBufferSize;
					}

					if (IndexBufferSize > WatermarkIndex)
					{
						ImGuiIndexBuffer = CreateBuffer(IndexBufferSize, true);
						WatermarkIndex = IndexBufferSize;
					}

					Ticket VertexUploadDone = EnqueueToWorker([
						&WatermarkVertex,
						&WatermarkIndex,
						&ImGuiIndexBuffer,
						&ImGuiVertexBuffer,
						DL
					]() {
						ZoneScopedN("GUI buffer upload");
						UINT64 VtxOffset = 0;
						UINT64 IdxOffset = 0;

						uint8_t* VtxP = NULL;
						uint8_t* IdxP = NULL;
						ImGuiVertexBuffer->Map(0, nullptr, (void**)&VtxP);
						ImGuiIndexBuffer->Map(0, nullptr, (void**)&IdxP);
						for (ImDrawList* ImGuiCmdList : DL)
						{
							memcpy(VtxP + VtxOffset, ImGuiCmdList->VtxBuffer.Data, ImGuiCmdList->VtxBuffer.Size * sizeof(ImDrawVert));
							VtxOffset += ImGuiCmdList->VtxBuffer.Size * sizeof(ImDrawVert);
							memcpy(IdxP + IdxOffset, ImGuiCmdList->IdxBuffer.Data, ImGuiCmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
							IdxOffset += ImGuiCmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
						}
						ImGuiVertexBuffer->Unmap(0, nullptr);
						ImGuiIndexBuffer->Unmap(0, nullptr);
					});

					BindDescriptors(CommandList.Get(), GetGUIFont());

					TextureData& RenderTarget = GetBackBuffer(CurrentBackBufferIndex);
					BindRenderTargets(CommandList.Get(), { RenderTarget.RTV }, -1);
					CommandList->SetPipelineState(GuiPSO.Get());
					CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

					Vec2 RenderTargetSize{ (float)RenderTarget.Width, (float)RenderTarget.Height };
					CommandList->SetGraphicsRoot32BitConstants(1, 2, &RenderTargetSize, 0);

					D3D12_INDEX_BUFFER_VIEW ImGuiIndexBufferView;
					ImGuiIndexBufferView.BufferLocation = ImGuiIndexBuffer->GetGPUVirtualAddress();
					ImGuiIndexBufferView.SizeInBytes = UINT(IndexBufferSize);
					ImGuiIndexBufferView.Format = DXGI_FORMAT_R16_UINT;
					CommandList->IASetIndexBuffer(&ImGuiIndexBufferView);

					TextureData& BackBuffer = GetBackBuffer(CurrentBackBufferIndex);
					CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)BackBuffer.Width, (float)BackBuffer.Height);
					CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

					CommandList->RSSetViewports(1, &SceneColorViewport);
					CommandList->RSSetScissorRects(1, &SceneColorScissor);

					UINT64 VtxOffset = 0;
					UINT64 IdxOffset = 0;
					for (int i = 0; i < DL.size(); ++i)
					{
						ImDrawList* ImGuiCmdList = DL[i];

						D3D12_VERTEX_BUFFER_VIEW ImGuiVertexBufferView;
						ImGuiVertexBufferView.BufferLocation = ImGuiVertexBuffer->GetGPUVirtualAddress() + VtxOffset;
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

							CommandList->DrawIndexedInstanced(ImGuiCmd.ElemCount, 1, IdxOffset, 0, 0);
							IdxOffset += ImGuiCmd.ElemCount;
						}
						VtxOffset += ImGuiVertexBufferView.SizeInBytes;
					}

					WaitForCompletion(VertexUploadDone);
				}
				Submit(CommandList, CurrentFenceValue);

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
					&SceneColorSmall,
					&SceneColor,
					&Quad,
					&DownsampleRasterPSO,
					&SceneColorStagingBuffer,
					&ReadBackReadyFence,
					CurrentFenceValue,
					CurrentBackBufferIndex
				] () {
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);

				ZoneScopedN("Downsample scenecolor to readback");
				TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Downsample scenecolor to readback");
				PIXScopedEvent(CommandList.Get(), __LINE__, "Downsample scenecolor to readback");

				TextureData& Small = SceneColorSmall;

				// readback(copy_src -> render)
				{
					CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						GetTextureResource(Small.ID),
						D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
					CommandList->ResourceBarrier(1, &barrier);
				}

				{
					CD3DX12_VIEWPORT	SmallViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)Small.Width, (float)Small.Height);
					CD3DX12_RECT		SmallScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

					BindRenderTargets(CommandList.Get(), { Small.RTV }, 0);
					BindDescriptors(CommandList.Get(), SceneColor);

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

				// readback(render -> copy_src)
				{
					CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						GetTextureResource(Small.ID),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
					CommandList->ResourceBarrier(1, &barrier);
				}

				{
					D3D12_RESOURCE_DESC Desc = GetTextureResource(Small.ID)->GetDesc();
					UINT RowCount;
					UINT64 RowPitch;
					UINT64 ResourceSize;
					GetGraphicsDevice()->GetCopyableFootprints(&Desc, 0, 1, 0, NULL, &RowCount, &RowPitch, &ResourceSize);

					D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
					bufferFootprint.Footprint.Width = Small.Width;
					bufferFootprint.Footprint.Height = Small.Height;
					bufferFootprint.Footprint.Depth = 1;
					bufferFootprint.Footprint.RowPitch = (UINT)RowPitch;
					bufferFootprint.Footprint.Format = (DXGI_FORMAT)Small.Format;

					CD3DX12_TEXTURE_COPY_LOCATION Dst(SceneColorStagingBuffer.Get(), bufferFootprint);
					CD3DX12_TEXTURE_COPY_LOCATION Src(GetTextureResource(Small.ID));
					CommandList->CopyTextureRegion(&Dst,0,0,0,&Src,nullptr);

					ReadBackReadyFence = CurrentFenceValue + 1;
				}
				Submit(CommandList, CurrentFenceValue);
			});
		}
#endif
		QueuedFrameCount++;
		EnqueueRenderThreadWork([
			&QueuedFrameCount,
			&FrameFences,
			&FrameFenceValues,
			&CurrentFenceValue,
			CurrentBackBufferIndex
		]()
		{
			{
				ZoneScopedN("Present");
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
				// backbuffer(render -> present)
				{
					CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						GetBackBufferResource(CurrentBackBufferIndex),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
					CommandList->ResourceBarrier(1, &barrier);
				}

				Submit(CommandList, CurrentFenceValue);
				FrameFenceValues[CurrentBackBufferIndex] = Signal(
					GetGraphicsQueue(),
					FrameFences[CurrentBackBufferIndex].Get(),
					CurrentFenceValue
				);

				PresentCurrentBackBuffer();
			}
			QueuedFrameCount--;

			TracyD3D12Collect(gGraphicsProfilingCtx);
			//TracyD3D12Collect(gComputeProfilingCtx);
			//TracyD3D12Collect(gCopyProfilingCtx);

			PIXEndEvent(GetGraphicsQueue());
		});

		CurrentBackBufferIndex = (CurrentBackBufferIndex + 1) % BACK_BUFFER_COUNT;
	}

	Importer.SetProgressHandler(nullptr);
	Helper.ShouldProceed = false;

	if (CurrentBackBufferIndex == 0)
	{
		CurrentBackBufferIndex = BACK_BUFFER_COUNT;
	}

	EnqueueRenderThreadWork([&FrameFences,CurrentBackBufferIndex, &CurrentFenceValue, WaitEvent]() {
		FlushQueue(GetGraphicsQueue(), FrameFences[CurrentBackBufferIndex - 1].Get(), CurrentFenceValue, WaitEvent);
		WaitForUploadFinish();
		ReleaseTextures();
		TracyD3D12Destroy(gGraphicsProfilingCtx);
		//TracyD3D12Destroy(gComputeProfilingCtx);
		//TracyD3D12Destroy(gCopyProfilingCtx);
	});
	StopRenderThread();
	StopWorkerThreads();
	CloseHandle(WaitEvent);
}

