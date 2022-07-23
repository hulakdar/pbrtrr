#include "Util/Util.h"
#include "Util/Debug.h"
#include "Util/Allocator.h"
#include "Containers/Queue.h"
#include "Containers/ComPtr.h"
#include "Containers/String.h"
#include "Containers/Map.h"
#include "System/GUI.h"
#include "System/Window.h"
#include "Render/Context.h"
#include "Render/Texture.h"
#include "Render/RenderDebug.h"
#include "Render/CommandListPool.h"
#include "Render/TransientResourcesPool.h"
#include "Threading/Mutex.h"
#include "Threading/Worker.h"
#include "Threading/Thread.h"
#include "Threading/MainThread.h"

#include "external/stb/stb_image.h"

#include <WinPixEventRuntime/pix3.h>
#include <EASTL/algorithm.h>

#include <atomic>

#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/ProgressHandler.hpp>
#include <TracyD3D12.hpp>

#include <imnodes.h>

DISABLE_OPTIMIZATION

struct Camera
{
	float Fov = FLT_MAX;
	float Near = FLT_MAX;
	float Far = FLT_MAX;
	Vec3 Eye;
	Vec2 Angles;
};

struct VertexDesc
{
	DXGI_FORMAT Format;
	uint32_t	Offset;
	const char* Semantic;
};

struct MeshData
{
	TArray<D3D12_INPUT_ELEMENT_DESC> VertexDeclaration;
	ComPtr<ID3D12Resource>	VertexBuffer;
	ComPtr<ID3D12Resource>	IndexBuffer;
	uint32_t				VertexBufferSize = UINT_MAX;
	uint32_t				IndexBufferSize = UINT_MAX;
	uint32_t				VertexSize = UINT_MAX;
	uint32_t				MaterialIndex = UINT_MAX;

	Vec3 Offset{0};
	Vec3 Scale{0};
	bool b16BitIndeces = false;
};

struct TextureBindingInfo
{
	u32 Index : 31;
	u32 Numbered : 1;

	Vec3 Offset{0};
	Vec3 Scale{0};
};

struct Material
{
	String Name;
	TArray<TextureBindingInfo> DiffuseTextures;
	Vec4 DiffuseColor;
};

TextureData ParseTexture(aiTexture* Texture)
{
	ZoneScoped;
	TextureData Result;
	uint8_t* Data = nullptr;

	if (Texture->mHeight == 0)
	{
		int Channels = 0, w = 0, h = 0;
		Data = stbi_load_from_memory(
			(uint8_t*)Texture->pcData,
			Texture->mWidth,
			&w, &h,
			&Channels, 4);
		Result.Width  = (u16)w;
		Result.Height = (u16)h;
		Result.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	}
	else
	{
		DEBUG_BREAK();
	}
	CreateResourceForTexture(Result, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
	UploadTextureData(Result, Data, Result.Width * Result.Height * 4);
	CreateSRV(Result);
	return Result;
}

void AllocateMeshData(aiMesh* Mesh, MeshData& Result, bool PositionPacked)
{
	CHECK(Mesh->HasFaces(), "Mesh without faces?");

	UINT VertexSize = 0;
	if (PositionPacked)
	{
		Result.VertexDeclaration.push_back(D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, VertexSize });
		VertexSize += sizeof(Vec4PackUnorm);
	}
	else
	{
		Result.VertexDeclaration.push_back(D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, VertexSize });
		VertexSize += sizeof(aiVector3D);
	}

	if (Mesh->HasNormals())
	{
		Result.VertexDeclaration.push_back(D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, VertexSize });
		VertexSize += sizeof(Vec4h);
	}

	UINT VertexColors = 0;
	while (Mesh->HasVertexColors(VertexColors))
	{
		Result.VertexDeclaration.push_back(D3D12_INPUT_ELEMENT_DESC{ "COLOR", VertexColors, DXGI_FORMAT_R8G8B8A8_UNORM, 0, VertexSize });
		VertexSize += 4;
		VertexColors++;
	}

	UINT UVSets = 0;
	while (Mesh->HasTextureCoords(UVSets))
	{
		Result.VertexDeclaration.push_back(D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", UVSets, DXGI_FORMAT_R16G16_FLOAT, 0, VertexSize });
		VertexSize += sizeof(half) * 2;
		UVSets++;
	}
	if (UVSets == 0) // fake uv binding
	{
		Result.VertexDeclaration.push_back(D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R8_UNORM, 0, 0 });
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
	Vec3 Min = Vec3{
		Mesh->mAABB.mMin.x - 0.0001f,
		Mesh->mAABB.mMin.y - 0.0001f,
		Mesh->mAABB.mMin.z - 0.0001f,
	};
	Vec3 Max = Vec3{
		Mesh->mAABB.mMax.x + 0.0001f,
		Mesh->mAABB.mMax.y + 0.0001f,
		Mesh->mAABB.mMax.z + 0.0001f,
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

		if (Mesh->mNormals)
		{
			WriteAndAdvance(CpuPtr,
				Vec4h{
					half(Mesh->mNormals[i].x),
					half(Mesh->mNormals[i].y),
					half(Mesh->mNormals[i].z),
					1.f
				}
			);
		}

		for (int j = 0; Mesh->mColors[j]; ++j)
		{
			WriteAndAdvance(CpuPtr, (uint8_t)(MIN(Mesh->mColors[j][i].r * 255.0f, 255)));
			WriteAndAdvance(CpuPtr, (uint8_t)(MIN(Mesh->mColors[j][i].g * 255.0f, 255)));
			WriteAndAdvance(CpuPtr, (uint8_t)(MIN(Mesh->mColors[j][i].b * 255.0f, 255)));
			WriteAndAdvance(CpuPtr, (uint8_t)(MIN(Mesh->mColors[j][i].a * 255.0f, 255)));
		}

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

u32 HashDeclaration(TArray<D3D12_INPUT_ELEMENT_DESC>& VertDeclarations)
{
	u32 hash = 5381;
	
	for (auto& Vert : VertDeclarations)
	{
		u32 c = (Vert.Format << 24) | Vert.AlignedByteOffset ^ *(u32*)Vert.SemanticName;
		hash = (hash << 5) + hash + c;
	}
	return hash;
}

TracyD3D12Ctx	gGraphicsProfilingCtx;
TracyD3D12Ctx	gComputeProfilingCtx;
TracyD3D12Ctx	gCopyProfilingCtx;

struct DelayedWork
{
	TFunction<void(void)> Work;
	u64 SafeFenceValue;
};

TQueue<DelayedWork> gDelayedWork;
TracyLockable(Mutex, gDelayedWorkLock);

void EnqueueDelayedWork(TFunction<void(void)>&& Work, u64 SafeFenceValue)
{
	ScopedLock AutoLock(gDelayedWorkLock);

	gDelayedWork.push(
		DelayedWork {
			MOVE(Work),
			SafeFenceValue
		}
	);
}

void CheckForDelayedWork(u64 CurrentFenceValue)
{
	if (!gDelayedWork.empty())
	{
		TArray<decltype(gDelayedWork.front().Work)> Work;
		{
			ScopedLock AutoLock(gDelayedWorkLock);
			while (gDelayedWork.front().SafeFenceValue < CurrentFenceValue)
			{
				Work.push_back(MOVE(gDelayedWork.front().Work));
				gDelayedWork.pop();
			}
		}
		for (auto& Item : Work)
			EnqueueToWorker(MOVE(Item));
	}
}

std::atomic<u64> gLastCompletedFence = 0;
int main(void)
{
	while( !::IsDebuggerPresent() )
		::SleepEx(100, true); // to avoid 100% CPU load

	StartDebugSystem();

	gMainThreadID = CurrentThreadID();
	StartWorkerThreads();
	stbi_set_flip_vertically_on_load(false);

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
		TArray<u32> MeshIDs;
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
	TArray<TextureData> NumberedTextures;
	TArray<TextureData> LoadedTextures;
	std::atomic<u32> LoadedTexturesIndex;

	ComPtr<ID3D12Fence> Fence = CreateFence();
	u64 CurrentFenceValue = 0;
	HANDLE WaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	{
#if 0
		String FilePath = "content/DamagedHelmet.glb";
#elif 1
		String FilePath = "content/Bistro/BistroExterior.fbx";
#else
		String FilePath = "content/SunTemple/SunTemple.fbx";
#endif
		const aiScene* Scene = nullptr;
		{
			ZoneScopedN("Scene file parsing");
			Scene = Importer.ReadFile(
				FilePath.c_str(),
				aiProcess_GenBoundingBoxes | aiProcess_ConvertToLeftHanded | aiProcessPreset_TargetRealtime_Quality
			);
			CHECK(Scene != nullptr, "Load failed");
		}

		if (!Scene)
			return 1;

		Meshes.reserve(Scene->mNumMeshes);
		Materials.reserve(Scene->mNumMaterials);
		MeshDatas.reserve(Scene->mNumMeshes);

		if (Scene->HasMaterials())
		{
			EnqueueToWorker([Scene, &Materials, &LoadedTextures, &LoadedTexturesIndex, &NumberedTextures,FilePath]() {
				StringView MeshFolder(FilePath.c_str(), FilePath.find_last_of("\\/"));
				ParallelFor(Scene->mNumMaterials,
					[Scene, &Materials, &LoadedTextures, &LoadedTexturesIndex, &NumberedTextures, &MeshFolder](u64 Index, u64 Begin, u64 End) {
					for (u32 i = Begin; i < End; ++i)
					{
						aiMaterial* MaterialPtr = Scene->mMaterials[i];
						Material TmpMaterial;
						TmpMaterial.Name = String(MaterialPtr->GetName().C_Str());

						for (u32 j = 0, TextureCount = MaterialPtr->GetTextureCount(aiTextureType_DIFFUSE); j < TextureCount; ++j)
						{
							aiString TexPath;
							aiTextureMapping mapping = aiTextureMapping_UV;
							unsigned int uvindex = 0;
							ai_real blend = 0;
							aiTextureOp op = aiTextureOp_Multiply;
							aiTextureMapMode mapmode[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };

							MaterialPtr->GetTexture(aiTextureType_DIFFUSE, j, &TexPath, &mapping, &uvindex, &blend, &op, mapmode);

							CHECK(TexPath.length != 0, "");
							CHECK(mapmode[0] == aiTextureMapMode_Wrap && mapmode[1] == aiTextureMapMode_Wrap, "");
							CHECK(op == aiTextureOp_Multiply, "");
							CHECK(mapping == aiTextureMapping_UV, "");
							CHECK(uvindex == 0, "");
							CHECK(blend == 0, "");

							if (TexPath.data[0] == '*')
							{
								aiTexture* Texture = Scene->mTextures[i];
								u32 Index = atoi(TexPath.C_Str() + 1);
								TmpMaterial.DiffuseTextures.push_back({ Index, true });
								TextureData TmpTexData = ParseTexture(Texture);
								EnqueueRenderThreadWork(
									[&NumberedTextures, Tmp = MOVE(TmpTexData)]() mutable {
										NumberedTextures.push_back(MOVE(Tmp));
									}
								);
							}
							else
							{
								String Path(MeshFolder);
								Path.append(1, '/');
								Path.append(TexPath.data, TexPath.length);

								TextureData TmpTexData;

								StringView Binary = LoadWholeFile(Path);
								if (u8* Data = (u8*)Binary.data())
								{
									auto Magic = ReadAndAdvance<u32>(Data);
									CHECK(Magic == DDS_MAGIC, "This is not a valid DDS");

									auto Header = ReadAndAdvance<DDS_HEADER>(Data);
									if (Header.ddspf.dwFlags & DDPF_FOURCC)
									{
										if (Header.ddspf.dwFourCC == MAGIC(DX10))
										{
											auto HeaderDX10 = ReadAndAdvance<DDS_HEADER_DXT10>(Data);
											TmpTexData.Format = (u8)HeaderDX10.dxgiFormat;
											CHECK(HeaderDX10.resourceDimension == D3D10_RESOURCE_DIMENSION_TEXTURE2D, "Now only Tex2d supported");
											CHECK(HeaderDX10.arraySize == 1, "Doesn't support tex arrays yet");
										}
										else
										{
											TmpTexData.Format = (u8)FormatFromFourCC(Header.ddspf.dwFourCC);
										}

										TmpTexData.Width = (u16)Header.dwWidth;
										TmpTexData.Height = (u16)Header.dwHeight;
										CreateResourceForTexture(TmpTexData, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
										UploadTextureData(TmpTexData, Data, Header.dwPitchOrLinearSize);
										CreateSRV(TmpTexData);
										CHECK(TmpTexData.Format != DXGI_FORMAT_UNKNOWN, "Unknown format");
									}
								}
								TmpMaterial.DiffuseTextures.push_back({ LoadedTexturesIndex++, false });
								EnqueueRenderThreadWork(
									[&LoadedTextures, Tmp = MOVE(TmpTexData)]() mutable {
										LoadedTextures.push_back(Tmp);
									}
								);
							}
						}
						EnqueueRenderThreadWork([&Materials, Tmp = MOVE(TmpMaterial)]() mutable {
							Materials.push_back(MOVE(Tmp));
						});
					}
				});
			});
		}

		EnqueueToWorker([Scene, &MeshDatas, &Meshes, &MainCamera, &CurrentFenceValue, &Fence, &WaitEvent]() {
			TArray<u32> UploadOffsets;
			PooledBuffer UploadBuffer;
			{
				ZoneScopedN("Allocate mesh data");

				UploadOffsets.resize(Scene->mNumMeshes);
				u32 UploadBufferSize = 0;
				for (unsigned int i = 0; i < Scene->mNumMeshes; ++i)
				{
					aiMesh* Mesh = Scene->mMeshes[i];
					MeshData Tmp;
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

					UploadOffsets[i] = UploadBufferSize;
					UploadBufferSize += Tmp.IndexBufferSize + Tmp.VertexBufferSize;
					Tmp.MaterialIndex = Mesh->mMaterialIndex;
					EnqueueRenderThreadWork([&MeshDatas, Tmp = MOVE(Tmp)]() mutable {
						MeshDatas.push_back(MOVE(Tmp));
					});
				}

				GetTransientBuffer(UploadBuffer, UploadBufferSize, BUFFER_UPLOAD);

				{
					ZoneScopedN("Upload");

					unsigned char* CpuPtr = NULL;
					UploadBuffer->Map(0, NULL, (void**)&CpuPtr);
					CpuPtr += UploadBuffer.Offset;
					ParallelFor(Scene->mNumMeshes, [Scene, CpuPtr, &UploadOffsets](u64 Index, u64 Start, u64 End) {
						for (u32 i = Start; i < End; ++i)
						{
							aiMesh* Mesh = Scene->mMeshes[i];
							UploadMeshData(CpuPtr + UploadOffsets[i], Mesh, true);
						}
					});
					UploadBuffer->Unmap(0, NULL);
				}
			}

			D3D12CmdList WorkerCommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, 0);

			{
				TracyD3D12Zone(gGraphicsProfilingCtx, WorkerCommandList.Get(), "Copy Mesh Data to GPU");
				PIXScopedEvent(WorkerCommandList.Get(), __LINE__, "Copy Mesh Data to GPU");
				{
					ZoneScopedN("Fill command list for upload");
					for (u32 i = 0; i < MeshDatas.size(); ++i)
					{
						MeshData& Data = MeshDatas[i];
						WorkerCommandList->CopyBufferRegion(Data.VertexBuffer.Get(), 0, UploadBuffer.Get(), UploadOffsets[i] + UploadBuffer.Offset, Data.VertexBufferSize);
						WorkerCommandList->CopyBufferRegion(Data.IndexBuffer.Get(), 0, UploadBuffer.Get(), UploadOffsets[i] + Data.VertexBufferSize + UploadBuffer.Offset, Data.IndexBufferSize);
					}
				}
			}

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

			Submit(WorkerCommandList, CurrentFenceValue);

			{
				using namespace std;

				TQueue<pair<aiNode*, aiMatrix4x4>> ProcessingQueue;
				ProcessingQueue.emplace(Scene->mRootNode, aiMatrix4x4());

				while (!ProcessingQueue.empty())
				{
					ZoneScopedN("Pump mesh queue");
					aiNode*& Current = ProcessingQueue.front().first;
					aiMatrix4x4& ParentTransform = ProcessingQueue.front().second;

					aiMatrix4x4 CurrentTransform = ParentTransform * Current->mTransformation;
					for (u32 i = 0; i < Current->mNumChildren; ++i)
					{
						ProcessingQueue.emplace(Current->mChildren[i], CurrentTransform);
					}

					if (Current->mNumMeshes > 0)
					{
						TArray<u32> MeshIDs;
						MeshIDs.reserve(Current->mNumMeshes);
						for (u32 i = 0; i < Current->mNumMeshes; ++i)
						{
							unsigned int MeshID = Current->mMeshes[i];
							MeshIDs.push_back(MeshID);
						}

						CurrentTransform.Transpose();
						EnqueueRenderThreadWork([
							&Meshes,
							CurrentName = String(Current->mName.C_Str()),
							CurrentTransform = Matrix4(CurrentTransform[0]),
							MeshIDs = MOVE(MeshIDs)
						]() mutable {
							Meshes.push_back(
								Mesh
								{
									MOVE(CurrentName),
									CurrentTransform,
									MOVE(MeshIDs)
								}
							);
						});
					}
					else if (strcmp(Current->mName.C_Str(), "Camera") == 0)
					{
						aiVector3D Scale, Rotation, Location;
						CurrentTransform.Decompose(Scale, Rotation, Location);
						MainCamera.Eye = Vec3{ Location.x, Location.y, Location.z };
					}
					ProcessingQueue.pop();
				}
			}

			EnqueueDelayedWork([UploadBuffer = MOVE(UploadBuffer)]() mutable {
					DiscardTransientBuffer(UploadBuffer);
				}, CurrentFenceValue
			);
		});
	}

	TMap<uint32_t, ComPtr<ID3D12PipelineState>> MeshPSOs;
	for (auto& Mesh : MeshDatas)
	{
		uint32_t Hash = HashDeclaration(Mesh.VertexDeclaration);
		auto& Value = MeshPSOs[Hash];
		if (Value)
			continue;

		DXGI_FORMAT RenderTargetFormat = SCENE_COLOR_FORMAT;
		TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
		Value = CreateShaderCombination(
			Mesh.VertexDeclaration,
			EntryPoints,
			"content/shaders/Simple.hlsl",
			&RenderTargetFormat,
			D3D12_CULL_MODE_BACK,
			DEPTH_FORMAT
		);
	}

	ComPtr<ID3D12PipelineState> BlitPSO;
	{
		D3D12_INPUT_ELEMENT_DESC InputDesc = {};
		InputDesc.SemanticName = "POSITION";
		InputDesc.SemanticIndex = 0;
		InputDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		InputDesc.InputSlot = 0;
		InputDesc.AlignedByteOffset = 0;

		DXGI_FORMAT RenderTargetFormat = BACK_BUFFER_FORMAT;
		TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
		BlitPSO = CreateShaderCombination(
			&InputDesc,
			EntryPoints,
			"content/shaders/Blit.hlsl",
			&RenderTargetFormat,
			D3D12_CULL_MODE_BACK
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
			&RenderTargetFormat,
			D3D12_CULL_MODE_NONE
		);
	}

	TextureData DefaultTexture;
	{
		int w = 0, h = 0;
		uint8_t *Data = stbi_load("content/uvcheck.jpg", &w, &h, nullptr, 4);
		DefaultTexture.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		DefaultTexture.Width  = (u16)w;
		DefaultTexture.Height = (u16)h;

		CreateResourceForTexture(DefaultTexture, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
		UploadTextureData(DefaultTexture, Data, 0);
		CreateSRV(DefaultTexture);
		stbi_image_free(Data);
	}

	MeshData Quad;
	{
		uint16_t IndexData[] = {
			0, 1, 2,
		};

		Vec2h VertexData[] = {
			{half(-1.f), half(-3.f)},
			{half(-1.f), half(1.f)},
			{half(3.f), half(1.f)},
		};

		Quad.IndexBuffer = CreateBuffer(sizeof(IndexData));
		Quad.IndexBufferSize = sizeof(IndexData);
		UploadBufferData(Quad.IndexBuffer.Get(), IndexData, sizeof(IndexData), D3D12_RESOURCE_STATE_INDEX_BUFFER);
		SetD3DName(Quad.IndexBuffer, L"Quad IndexBuffer");

		Quad.VertexBuffer = CreateBuffer(sizeof(VertexData));
		Quad.VertexBufferSize = sizeof(VertexData);
		Quad.VertexSize = sizeof(VertexData[0]);
		UploadBufferData(Quad.VertexBuffer.Get(), VertexData, sizeof(VertexData), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		SetD3DName(Quad.VertexBuffer, L"Quad VertexBuffer");
	}

	FlushUpload(CurrentFenceValue);

	TextureData SceneColor = {};
	{
		SceneColor.Format = SCENE_COLOR_FORMAT;
		SceneColor.Width  = (u16)Window.mSize.x;
		SceneColor.Height = (u16)Window.mSize.y;

		GetTransientTexture(SceneColor,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr
		);
	}

	TextureData DepthBuffer;
	{
		D3D12_RESOURCE_DESC TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			DEPTH_FORMAT,
			Window.mSize.x, Window.mSize.y,
			1, 1, // ArraySize, MipLevels
			1, 0, // SampleCount, SampleQuality
			D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);
		D3D12_CLEAR_VALUE ClearValue = {};
		ClearValue.Format = DEPTH_FORMAT;
		ClearValue.DepthStencil.Depth = 0.0f;

		DepthBuffer.Format = (u8)TextureDesc.Format;
		DepthBuffer.Width  = (u16)Window.mSize.x;
		DepthBuffer.Height = (u16)Window.mSize.y;

		CreateResourceForTexture(DepthBuffer, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ClearValue);
		CreateDSV(DepthBuffer);
	}

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
			D3D12_CULL_MODE_NONE,
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
	FlushUpload(CurrentFenceValue);

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

	u64 FrameFenceValues[3] = {};

	std::atomic<u64> QueuedFrameCount = 0;

	while (!glfwWindowShouldClose(Window.mHandle))
	{
		FrameMark;

		if (Window.mWindowStateDirty)
		{
			ZoneScopedN("Window state dirty");

			// Wait for render thread AND gpu
			Ticket WaitForAll = EnqueueRenderThreadWork([
				FrameFence = Fence.Get(),
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

			if (Window.mSize.x != 0 && Window.mSize.y != 0)
			{
				DiscardTransientTexture(SceneColor);
				SceneColor.Width  = (u16)Window.mSize.x;
				SceneColor.Height = (u16)Window.mSize.y;
				GetTransientTexture(SceneColor,
					D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
					nullptr
				);
			}
		}

		if (Window.mSize.x == 0)
		{
			Window.Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}

		UpdateGUI(Window);

		ImGui::NewFrame();

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
			ZoneScopedN("Wait for free backbuffer in swapchain");
			ExecuteMainThreadWork();

			//while (FenceWithDelay - LastCompletedFence > 2)
			//while (QueuedFrameCount > 2)
			while (!IsSwapChainReady())
			{
				if (!StealWork())
				{
					//SleepEx(1, true);
					//std::this_thread::sleep_for(std::chrono::microseconds(10));
				}
			}
			Window.Update();
		}

		EnqueueRenderThreadWork(
			[
				&CurrentFenceValue,
				&SceneColor,
				&DepthBuffer,
				CurrentBackBufferIndex
			]() {
				PIXBeginEvent(GetGraphicsQueue(), __LINE__, "FRAME");

				FlushUpload(CurrentFenceValue);

				TracyD3D12NewFrame(gGraphicsProfilingCtx);
				ZoneScopedN("New frame");

				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
				CD3DX12_RESOURCE_BARRIER barriers[] = {
					// SceneColor(srv -> render)
					CD3DX12_RESOURCE_BARRIER::Transition(
						GetTextureResource(SceneColor.ID),
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_RENDER_TARGET
					),
				};
				CommandList->ResourceBarrier(ArrayCount(barriers), barriers);

				CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)SceneColor.Width, (float)SceneColor.Height);
				CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

				CommandList->RSSetViewports(1, &SceneColorViewport);
				CommandList->RSSetScissorRects(1, &SceneColorScissor);

				BindRenderTargets(CommandList.Get(), {SceneColor.RTV}, DepthBuffer.DSV);
				{
					FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
					ClearRenderTarget(CommandList.Get(), SceneColor.RTV, clearColor);
				}
				ClearDepth(CommandList.Get(), DepthBuffer.DSV, 0.0f);

				Submit(CommandList, CurrentFenceValue);
			}
		);

		// Mesh
		{
			ZoneScopedN("ImGui sliders");

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
					&SceneColor,
					&DepthBuffer,
					&Window,
					&Materials,
					&Meshes,
					&MeshPSOs,
					&MeshDatas,
					&NumberedTextures,
					&LoadedTextures,
					&CurrentFenceValue
				]()
				{
					TArray<D3D12CmdList> CommandLists;
					D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
					{
						ZoneScopedN("Drawing meshes");

						float Fov = MainCamera.Fov;
						float Near = MainCamera.Near;
						float Far = MainCamera.Far;
						Vec2 Angles = MainCamera.Angles;
						Vec3 Eye = MainCamera.Eye;

						Matrix4 Projection = CreatePerspectiveMatrixReverseZ(DegreesToRadians(Fov), (float)Window.mSize.x / (float)Window.mSize.y, Near);
						Matrix4 View = CreateViewMatrix(-Eye, Angles);
						Matrix4 VP = View * Projection;

						CommandLists.resize(NumberOfWorkers() + 1);
						for (auto& CmdList : CommandLists)
						{
							CmdList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
						}
						CommandLists[NumberOfWorkers()] = MOVE(CommandList);

						ParallelFor(
							Meshes.size(), 
							[
								&CommandLists,
								&NumberedTextures, &LoadedTextures, &DefaultTexture,
								&MeshDatas, &Meshes, &MeshPSOs,
								&VP, &Materials, &SceneColor, &DepthBuffer,
								CurrentFenceValue
							](u64 Index, u64 Begin, u64 End)
							{
								D3D12CmdList& CommandList = CommandLists[Index];
								TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Render Meshes from worker");
								PIXScopedEvent(CommandList.Get(), __LINE__, "Render Meshes from worker");

								BindRenderTargets(CommandList.Get(), {SceneColor.RTV}, DepthBuffer.DSV);
								CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)SceneColor.Width, (float)SceneColor.Height);
								CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

								CommandList->RSSetViewports(1, &SceneColorViewport);
								CommandList->RSSetScissorRects(1, &SceneColorScissor);

								CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
								for (u64 i = Begin; i < End; ++i)
								{
									auto& Mesh = Meshes[i];
									for (UINT ID : Mesh.MeshIDs)
									{
										MeshData& MeshData = MeshDatas[ID];
										Matrix4 Scale = CreateScaleMatrix(MeshData.Scale);
										Matrix4 Translation = CreateTranslationMatrix(MeshData.Offset);
										Matrix4 Combined = Scale * Translation * Mesh.Transform * VP;

										uint32_t MatIndex = MeshData.MaterialIndex;
										if (MatIndex < Materials.size())
										{
											const auto& Tex = Materials[MatIndex].DiffuseTextures[0];
											if (Tex.Numbered)
											{
												BindDescriptors(CommandList.Get(), NumberedTextures[Tex.Index]);
											}
											else
											{
												BindDescriptors(CommandList.Get(), LoadedTextures[Tex.Index]);
											}

										}
										else
										{
											BindDescriptors(CommandList.Get(), DefaultTexture);
										}
										CommandList->SetGraphicsRoot32BitConstants(1, sizeof(Combined)/4, &Combined, 0);

										CommandList->SetGraphicsRoot32BitConstants(1, 16, &Combined, 0);

										uint32_t Hash = HashDeclaration(MeshData.VertexDeclaration);
										CommandList->SetPipelineState(MeshPSOs[Hash].Get());

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
						);
					}
					Submit(CommandLists, CurrentFenceValue);
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
				&FrameFenceValues,
				FrameFence = Fence.Get(),
				WaitEvent,
				CurrentBackBufferIndex
			]
			() {
				ZoneScopedN("SceneColor(render -> srv)");
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
				{
					ZoneScopedN("SceneColor barrier");
					CD3DX12_RESOURCE_BARRIER barriers[] =
					{
						// scenecolot(render -> srv)
						CD3DX12_RESOURCE_BARRIER::Transition(
							GetTextureResource(SceneColor.ID),
							D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						),

						// backbuffer(present -> render)
						CD3DX12_RESOURCE_BARRIER::Transition(
							GetBackBufferResource(CurrentBackBufferIndex),
							D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
							//,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
						),
					};

					CommandList->ResourceBarrier(ArrayCount(barriers), barriers);
				}
				{
					TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Blit scenecolor to backbuffer");
					PIXScopedEvent(CommandList.Get(), __LINE__, "Blit scenecolor to backbuffer");

					BindDescriptors(CommandList.Get(), SceneColor);

					CommandList->SetPipelineState(BlitPSO.Get());

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

					BindRenderTargets(CommandList.Get(), {CurrentBackBufferIndex}, (uint32_t)-1);

					CD3DX12_VIEWPORT	Viewport    = CD3DX12_VIEWPORT(0.f, 0.f, (float)Window.mSize.x, (float)Window.mSize.y);
					CD3DX12_RECT		ScissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
					CommandList->RSSetViewports(1, &Viewport);
					CommandList->RSSetScissorRects(1, &ScissorRect);

					CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					CommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);
				}
				WaitForFenceValue(FrameFence, FrameFenceValues[CurrentBackBufferIndex], WaitEvent);
				Submit(CommandList, CurrentFenceValue);
				EnqueueToMainThread([CompletedFence = FrameFenceValues[CurrentBackBufferIndex]]() {
					gLastCompletedFence = CompletedFence;
					CheckForDelayedWork(CompletedFence);
				});
			}
		);

		{
			ZoneScopedN("ImGui endframe work");
			ImGui::Render();
		}

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
					&CurrentFenceValue,
					CurrentBackBufferIndex
				]()
			{
				ZoneScopedN("Render GUI");

				UINT VtxBufferSize = TotalVtxCount * sizeof(ImDrawVert);
				UINT IdxBufferSize = TotalIdxCount * sizeof(ImDrawIdx);

				PooledBuffer GuiVBuffer;
				GetTransientBuffer(GuiVBuffer, VtxBufferSize, BUFFER_UPLOAD);
				PooledBuffer GuiIBuffer;
				GetTransientBuffer(GuiIBuffer, IdxBufferSize, BUFFER_UPLOAD);

				Ticket VertexUploadDone = EnqueueToWorker([
					GuiVBuffer,
					GuiIBuffer,
					&DL
				]() {
					ZoneScopedN("GUI buffer upload");
					UINT64 VtxOffset = GuiVBuffer.Offset;
					UINT64 IdxOffset = GuiIBuffer.Offset;

					uint8_t* VtxP = NULL;
					uint8_t* IdxP = NULL;
					GuiVBuffer->Map(0, nullptr, (void**)&VtxP);
					GuiIBuffer->Map(0, nullptr, (void**)&IdxP);
					for (ImDrawList* ImGuiCmdList : DL)
					{
						memcpy(VtxP + VtxOffset, ImGuiCmdList->VtxBuffer.Data, ImGuiCmdList->VtxBuffer.size_in_bytes());
						VtxOffset += ImGuiCmdList->VtxBuffer.Size * sizeof(ImDrawVert);
						memcpy(IdxP + IdxOffset, ImGuiCmdList->IdxBuffer.Data, ImGuiCmdList->IdxBuffer.size_in_bytes());
						IdxOffset += ImGuiCmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
					}
					GuiVBuffer->Unmap(0, nullptr);
					GuiIBuffer->Unmap(0, nullptr);
				});

				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
				{
					TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Render GUI");
					PIXScopedEvent(CommandList.Get(), __LINE__, "Render GUI");
					BindDescriptors(CommandList.Get(), GetGUIFont());

					TextureData& RenderTarget = GetBackBuffer(CurrentBackBufferIndex);
					BindRenderTargets(CommandList.Get(), { RenderTarget.RTV }, (uint32_t)-1);
					CommandList->SetPipelineState(GuiPSO.Get());
					CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

					Vec2 RenderTargetSize{ (float)RenderTarget.Width, (float)RenderTarget.Height };
					CommandList->SetGraphicsRoot32BitConstants(1, 2, &RenderTargetSize, 0);

					D3D12_INDEX_BUFFER_VIEW ImGuiIdxBufferView;
					ImGuiIdxBufferView.BufferLocation = GuiIBuffer->GetGPUVirtualAddress() + GuiIBuffer.Offset;
					ImGuiIdxBufferView.SizeInBytes    = IdxBufferSize;
					ImGuiIdxBufferView.Format         = DXGI_FORMAT_R16_UINT;
					CommandList->IASetIndexBuffer(&ImGuiIdxBufferView);

					D3D12_VERTEX_BUFFER_VIEW ImGuiVtxBufferView;
					ImGuiVtxBufferView.BufferLocation = GuiVBuffer->GetGPUVirtualAddress() + GuiVBuffer.Offset;
					ImGuiVtxBufferView.StrideInBytes  = sizeof(ImDrawVert);
					ImGuiVtxBufferView.SizeInBytes    = VtxBufferSize;
					CommandList->IASetVertexBuffers(0, 1, &ImGuiVtxBufferView);

					TextureData& BackBuffer = GetBackBuffer(CurrentBackBufferIndex);
					CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)BackBuffer.Width, (float)BackBuffer.Height);
					CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

					CommandList->RSSetViewports(1, &SceneColorViewport);
					CommandList->RSSetScissorRects(1, &SceneColorScissor);

					int VtxOffset = 0;
					int IdxOffset = 0;
					for (int i = 0; i < DL.size(); ++i)
					{
						ImDrawList* ImGuiCmdList = DL[i];

						for (auto& ImGuiCmd : ImGuiCmdList->CmdBuffer)
						{
							D3D12_RECT Rect{
								LONG(ImGuiCmd.ClipRect.x),
								LONG(ImGuiCmd.ClipRect.y),
								LONG(ImGuiCmd.ClipRect.z),
								LONG(ImGuiCmd.ClipRect.w),
							};
							CommandList->RSSetScissorRects(1, &Rect);
							CommandList->DrawIndexedInstanced(ImGuiCmd.ElemCount, 1, IdxOffset + ImGuiCmd.IdxOffset, VtxOffset + ImGuiCmd.VtxOffset, 0);
						}
						VtxOffset += ImGuiCmdList->VtxBuffer.Size;
						IdxOffset += ImGuiCmdList->IdxBuffer.Size;
					}

					WaitForCompletion(VertexUploadDone);
				}
				Submit(CommandList, CurrentFenceValue);

				EnqueueDelayedWork(
					[GuiVBuffer, GuiIBuffer]() mutable {
						DiscardTransientBuffer(GuiVBuffer);
						DiscardTransientBuffer(GuiIBuffer);
					},
					CurrentFenceValue
				);

				for (ImDrawList* List : DL)
				{
					IM_DELETE(List);
				}
			});
		}

#define CHECK_SCREENSHOT_CODE 0
#if CHECK_SCREENSHOT_CODE //|| TRACY_ENABLE // Send screenshot to Tracy
		if (CHECK_SCREENSHOT_CODE || TracyIsConnected)
		{
			EnqueueRenderThreadWork(
				[
					&Window,
					&SceneColor,
					&Quad,
					&DownsampleRasterPSO,
					&CurrentFenceValue
				] () {
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
				{
					ZoneScopedN("Downsample scenecolor to readback");
					TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Downsample scenecolor to readback");
					PIXScopedEvent(CommandList.Get(), __LINE__, "Downsample scenecolor to readback");

					TextureData Small = {};
					{
						Small.Format = READBACK_FORMAT;
						Small.Width  = (u16)MIN(Window.mSize.x, 512);
						Small.Height = (u16)MIN(Window.mSize.y, 512);
						if (Window.mSize.x > Window.mSize.y)
						{
							Small.Height = (u16)floor(Small.Width * (Window.mSize.y / (float)Window.mSize.x));
						}
						else
						{
							Small.Width  = (u16)floor(Small.Height * (Window.mSize.x / (float)Window.mSize.y));
						}
						Small.Width  = ((Small.Width  + 31) / 32) * 32;
						Small.Height = ((Small.Height + 15) / 16) * 16;
						GetTransientTexture(Small,
							D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
							D3D12_RESOURCE_STATE_COPY_SOURCE,
							nullptr
						);
					}

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

						CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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

						PooledBuffer SceneColorStagingBuffer;
						GetTransientBuffer(SceneColorStagingBuffer, ResourceSize, BUFFER_STAGING);

						CD3DX12_TEXTURE_COPY_LOCATION Dst(SceneColorStagingBuffer.Get(), bufferFootprint);
						CD3DX12_TEXTURE_COPY_LOCATION Src(GetTextureResource(Small.ID));
						CommandList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);

						int x = Small.Width, y = Small.Height;
						DiscardTransientTexture(Small);

						{
							ZoneScopedN("Readback screen capture");

							EnqueueDelayedWork(
								[
									SceneColorStagingBuffer,
									x, y, CurrentFenceValue
								]
							() mutable {
									EnqueueToMainThread([SceneColorStagingBuffer, x, y, CurrentFenceValue]() mutable {
										auto ResourcePtr = SceneColorStagingBuffer.Get();

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
		#if TRACY_ENABLE
										FrameImage(Data, (uint16_t)x, (uint16_t)y, CurrentFenceValue - gLastCompletedFence, false);
		#endif
										ResourcePtr->Unmap(0, NULL);

										DiscardTransientBuffer(SceneColorStagingBuffer);
									});
							},
							CurrentFenceValue);
						}
					}
				}
				Submit(CommandList, CurrentFenceValue);
			});
		}
#endif
		QueuedFrameCount++;
		EnqueueRenderThreadWork([
			&QueuedFrameCount,
			FrameFence = Fence.Get(),
			&FrameFenceValues,
			&CurrentFenceValue,
			CurrentBackBufferIndex
		]()
		{
			{
				ZoneScopedN("Present");
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CurrentFenceValue);
				{
					// backbuffer(render -> present)
					{
						CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
							GetBackBufferResource(CurrentBackBufferIndex),
							D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
						CommandList->ResourceBarrier(1, &barrier);
					}

					FrameFenceValues[CurrentBackBufferIndex] = Signal(
						GetGraphicsQueue(),
						FrameFence,
						CurrentFenceValue
					);
				}
				Submit(CommandList, CurrentFenceValue);
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

	EnqueueRenderThreadWork([FrameFence = Fence.Get(), CurrentBackBufferIndex, &CurrentFenceValue, WaitEvent]() {
		FlushQueue(GetGraphicsQueue(), FrameFence, CurrentFenceValue, WaitEvent);
		ReleaseTextures();
		TracyD3D12Destroy(gGraphicsProfilingCtx);
		//TracyD3D12Destroy(gComputeProfilingCtx);
		//TracyD3D12Destroy(gCopyProfilingCtx);
	});
	StopRenderThread();
	StopWorkerThreads();
	CloseHandle(WaitEvent);

	Importer.FreeScene();
}

