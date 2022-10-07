#include "Assets/File.h"
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

//DISABLE_OPTIMIZATION
#define A_CPU
#include "../thirdparty/FidelityFX-SPD/ffx-spd/ffx_a.h"
#include "../thirdparty/FidelityFX-SPD/ffx-spd/ffx_spd.h"

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
	Vec3 Offset{0};
	Vec3 Scale{0};

	D3D12_GPU_VIRTUAL_ADDRESS VertexBufferCachedPtr;
	D3D12_GPU_VIRTUAL_ADDRESS IndexBufferCachedPtr;
	ID3D12PipelineState*      PSO;

	uint32_t				VertexSize    : 10;
	uint32_t				MaterialIndex : 21;
	uint32_t				b16BitIndeces : 1;

	uint32_t				VertexBufferSize = UINT_MAX;
	uint32_t				IndexBufferSize = UINT_MAX;
	ComPtr<ID3D12Resource>	VertexBuffer;
	ComPtr<ID3D12Resource>	IndexBuffer;
	TArray<D3D12_INPUT_ELEMENT_DESC> VertexDeclaration;
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
	CreateResourceForTexture(Result, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
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
	Result.VertexBufferCachedPtr = Result.VertexBuffer->GetGPUVirtualAddress();
	Result.IndexBufferCachedPtr = Result.IndexBuffer->GetGPUVirtualAddress();
	Result.VertexBufferSize = VertexBufferSize;
	Result.IndexBufferSize = IndexBufferSize;
	Result.VertexSize = VertexSize;
	Result.b16BitIndeces = b16BitIndeces;
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
#include <Assets/Mesh.h>
#include <Assets/DDS.h>

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

struct Mesh
{
	String Name;
	Matrix4 Transform;
	TArray<u32> MeshIDs;
};

TArray<Mesh> gMeshes;
TArray<Material> gMaterials;
TArray<MeshData> gMeshDatas;
TArray<TextureData> gNumberedTextures;

TArray<TextureData> gLoadedTextures;
std::atomic<u32> gLoadedTexturesIndex;

TracyLockable(Mutex, gMeshLock);
TMap<uint32_t, ComPtr<ID3D12PipelineState>> gMeshPSOs;

Camera gMainCamera;

void StartSceneLoading(String FilePath)
{
	EnqueueToWorker([FilePath]() {
		Assimp::Importer Importer;

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
			return;

		EnqueueToRenderThread([Scene]() {
			gMeshes.reserve(Scene->mNumMeshes);
			gMaterials.reserve(Scene->mNumMaterials);
			gMeshDatas.reserve(Scene->mNumMeshes);
		});

		TArray<TicketCPU> Tickets;
		if (Scene->HasMaterials())
		{
			Tickets.push_back() = EnqueueToWorkerWithTicket([Scene, FilePath]() {
				StringView MeshFolder(FilePath.c_str(), FilePath.find_last_of("\\/"));
				ParallelFor([Scene, &MeshFolder](u64 Begin, u64 End) {
					for (u64 i = Begin; i < End; ++i)
					{
						aiMaterial* MaterialPtr = Scene->mMaterials[i];
						Material TmpMaterial;
						TmpMaterial.Name = String(MaterialPtr->GetName().C_Str());

						for (int l = 0; l < MaterialPtr->mNumProperties; ++l)
						{
							aiMaterialProperty* Property = MaterialPtr->mProperties[l];

							const char* Key = Property->mKey.C_Str();
							Debug::Print(Key);
							Debug::Print(Property->mType);
							Debug::Print(Property->mSemantic);
							Debug::Print(Property->mDataLength); 
						}

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
								u32 Index = atoi(TexPath.C_Str() + 1);
								aiTexture* Texture = Scene->mTextures[Index];
								TmpMaterial.DiffuseTextures.push_back({ Index, true });
								TextureData TmpTexData = ParseTexture(Texture);
								EnqueueToRenderThread(
									[&Tmp = MOVE(TmpTexData)]() mutable {
										gNumberedTextures.push_back(MOVE(Tmp));
									}
								);
							}
							else
							{
								String Path(MeshFolder);
								Path.append(1, '/');
								Path.append(TexPath.data, TexPath.length);

								TextureData TmpTexData;

								String Binary = LoadWholeFile(Path);
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
										CreateResourceForTexture(TmpTexData, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
										UploadTextureData(TmpTexData, Data, Header.dwPitchOrLinearSize);
										CreateSRV(TmpTexData);
										CHECK(TmpTexData.Format != DXGI_FORMAT_UNKNOWN, "Unknown format");
									}
								}
								TmpMaterial.DiffuseTextures.push_back({ gLoadedTexturesIndex++, false });
								EnqueueToRenderThread(
									[Tmp = MOVE(TmpTexData)]() mutable {
										gLoadedTextures.push_back(Tmp);
									}
								);
							}
						}
						EnqueueToRenderThread([Tmp = MOVE(TmpMaterial)]() mutable {
							gMaterials.push_back(MOVE(Tmp));
						});
					}
				}, Scene->mNumMaterials, 4);
			});
		}

		Tickets.push_back() = EnqueueToWorkerWithTicket([Scene]() {
			{
				ZoneScopedN("Upload mesh data");

				ParallelFor([Scene](u64 Begin, u64 End) {
					for (u64 i = Begin; i < End; ++i)
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

						UploadBufferData(Tmp.VertexBuffer.Get(), Tmp.VertexBufferSize, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
							[Mesh](void *GPUAddress, u64 Size) {
								UploadMeshData((u8*)GPUAddress, Mesh, true);
							}
						);
						UploadBufferData(Tmp.IndexBuffer.Get(), Tmp.IndexBufferSize, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
							[Mesh](void *GPUAddress, u64 Size) {
								UploadIndexData((u8*)GPUAddress, Mesh);
							}
						);

						Tmp.MaterialIndex = Mesh->mMaterialIndex;

						uint32_t Hash = HashDeclaration(Tmp.VertexDeclaration);

						gMeshLock.lock();
						ComPtr<ID3D12PipelineState> PSO = gMeshPSOs[Hash];

						if (PSO == nullptr)
						{
							DXGI_FORMAT RenderTargetFormat = SCENE_COLOR_FORMAT;
							TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
							Shader MeshShader = CreateShaderCombinationGraphics(
								Tmp.VertexDeclaration,
								EntryPoints,
								"content/shaders/Simple.hlsl",
								&RenderTargetFormat,
								D3D12_CULL_MODE_BACK,
								DEPTH_FORMAT
							);
							Tmp.PSO = (gMeshPSOs[Hash] = MOVE(MeshShader.PSO)).Get();
						}
						else
						{
							Tmp.PSO = PSO.Get();
						}
						gMeshLock.unlock();

						EnqueueToRenderThread([Tmp = MOVE(Tmp)]() mutable {
							gMeshDatas.push_back(MOVE(Tmp));
						});
					}
				}, Scene->mNumMeshes, 4);
			}

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
						EnqueueToRenderThread([
							CurrentName = String(Current->mName.C_Str()),
							CurrentTransform = Matrix4(CurrentTransform[0]),
							MeshIDs = MOVE(MeshIDs)
						]() mutable {
							gMeshes.push_back(
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
						EnqueueToMainThread([Location]() {
							gMainCamera.Eye = Vec3{ Location.x, Location.y, Location.z };
						});
					}
					ProcessingQueue.pop();
				}
			}
		});
		for (TicketCPU T : Tickets)
		{
			WaitForCompletion(T);
		}
		Importer.FreeScene();
	});
}

extern TracyD3D12Ctx	gCopyProfilingCtx;
int main(void)
{
	//while( !::IsDebuggerPresent() )
		//::SleepEx(100, true); // to avoid 100% CPU load

	InitDebug();

	gMainThreadID = CurrentThreadID();
	SetThreadAffinityMask(GetCurrentThread(), 0x1);
	stbi_set_flip_vertically_on_load(false);

	System::Window Window;
	Window.Init();

	InitRender(Window);
	StartRenderThread();

	StartWorkerThreads();

	gGraphicsProfilingCtx = TracyD3D12Context(GetGraphicsDevice(), GetGPUQueue(D3D12_COMMAND_LIST_TYPE_DIRECT));
	gComputeProfilingCtx = TracyD3D12Context(GetGraphicsDevice(), GetGPUQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE));

	TracyD3D12ContextName(gGraphicsProfilingCtx, "Graphics", 8);

	InitGUI(Window);

	gMainCamera.Fov = 60;
	gMainCamera.Near = 0.1;
	gMainCamera.Far = 1000;
	gMainCamera.Eye = Vec3{ 0, 0, -2 };
	gMainCamera.Angles = Vec2{ 0, 0 };

	ComPtr<ID3D12Fence> Fence = CreateFence();
	HANDLE WaitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
#if 0
	String FilePath = "content/DamagedHelmet.glb";
#elif 1
	String FilePath = "content/Bistro/BistroExterior.fbx";
#else
	String FilePath = "content/SunTemple/SunTemple.fbx";
#endif

	StartSceneLoading(FilePath);

	Shader BlitShader;
	{
		D3D12_INPUT_ELEMENT_DESC InputDesc = {};
		InputDesc.SemanticName = "POSITION";
		InputDesc.SemanticIndex = 0;
		InputDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		InputDesc.InputSlot = 0;
		InputDesc.AlignedByteOffset = 0;

		DXGI_FORMAT RenderTargetFormat = BACK_BUFFER_FORMAT;
		TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
		BlitShader = CreateShaderCombinationGraphics(
			&InputDesc,
			EntryPoints,
			"content/shaders/Blit.hlsl",
			&RenderTargetFormat,
			D3D12_CULL_MODE_BACK
		);
	}

	Shader DownsampleRaster;
	{
		D3D12_INPUT_ELEMENT_DESC InputDesc = {};

		InputDesc.SemanticName = "POSITION";
		InputDesc.SemanticIndex = 0;
		InputDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		InputDesc.InputSlot = 0;
		InputDesc.AlignedByteOffset = 0;

		TArray<StringView> EntryPoints = {"MainPS", "MainVS"};
		DXGI_FORMAT RenderTargetFormat = READBACK_FORMAT;
		DownsampleRaster = CreateShaderCombinationGraphics(
			&InputDesc, 
			EntryPoints,
			"content/shaders/Blit.hlsl",
			&RenderTargetFormat,
			D3D12_CULL_MODE_NONE
		);
	}

	Shader FfxSpd = CreateShaderCombinationCompute(
		"MainCS",
		"content/shaders/FfxSpd.hlsl"
	);

	TextureData DefaultTexture;
	{
		int w = 0, h = 0;
		uint8_t *Data = stbi_load("content/uvcheck.jpg", &w, &h, nullptr, 4);
		DefaultTexture.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		DefaultTexture.Width  = (u16)w;
		DefaultTexture.Height = (u16)h;

		CreateResourceForTexture(DefaultTexture, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
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

	TextureData SceneColor = {};
	{
		SceneColor.Format = SCENE_COLOR_FORMAT;
		SceneColor.Width  = (u16)Window.mSize.x;
		SceneColor.Height = (u16)Window.mSize.y;

		GetTransientTexture(SceneColor,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr
		);
	}

	TextureData DepthBuffer;
	auto CreateDepth = [&]()
	{
		D3D12_CLEAR_VALUE ClearValue = {};
		ClearValue.Format = DEPTH_FORMAT;
		ClearValue.DepthStencil.Depth = 0.0f;

		DepthBuffer.Format = (u8)DEPTH_FORMAT;
		DepthBuffer.Width = (u16)Window.mSize.x;
		DepthBuffer.Height = (u16)Window.mSize.y;

		CreateResourceForTexture(DepthBuffer, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ClearValue);
		CreateDSV(DepthBuffer);
	};
	CreateDepth();

	UINT CurrentBackBufferIndex = 0;

	Shader GuiShader;
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

		GuiShader = CreateShaderCombinationGraphics(
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

	static ComPtr<ID3D12Resource> AtomicBuffer = CreateBuffer(6 * 4);

	static TextureData OutputTextures[12];
	for (int i = 0; i < 12; ++i)
	{
		OutputTextures[i].Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		OutputTextures[i].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		CreateUAV(OutputTextures[i]);
	}
	
	u16 StartIndex = OutputTextures[0].UAV;
	for (int i = 0; i < 12; ++i)
	{
		CHECK(StartIndex + i == OutputTextures[i].UAV, "?");
	}
	static u64 OutputUAVDescriptors = GetGeneralHandleGPU(StartIndex);

	while (!glfwWindowShouldClose(Window.mHandle))
	{
		FrameMark;

		if (Window.mWindowStateDirty)
		{
			ZoneScopedN("Window state dirty");

			// Wait for render thread AND gpu
			TicketCPU WaitForAll = EnqueueToRenderThreadWithTicket(
			[
				FrameFence = Fence.Get(),
				WaitEvent
			]() {
				ZoneScopedN("Wait for render thread AND gpu");
				FlushQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
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

				CreateDepth();
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

					ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding, 1.f);
					ImNodes::Link(Edge.ID, Edge.StartID, Edge.EndID);
					ImNodes::PopStyleVar();
				}

				ImNodes::EndNodeEditor();
				ImGui::End();
			}
		}

		{
			ZoneScopedN("Wait for free backbuffer in swapchain");
			ExecuteMainThreadWork();

			//while (FenceWithDelay - LastCompletedFence > 2)
			while (!IsSwapChainReady())
			{
				//if (!StealWork())
				{
					ExecuteMainThreadWork();
					//std::this_thread::sleep_for(std::chrono::microseconds(10));
				}
			}
			Window.Update();
		}

		//D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Transfer atomic buffer");
		//CommandList->ResourceBarrier();

		EnqueueToRenderThread(
			[
				&SceneColor,
				&DepthBuffer
			]() {
				FrameMarkStart("Render thread");
				PIXBeginEvent(GetGPUQueue(), __LINE__, "FRAME");

				TracyD3D12NewFrame(gGraphicsProfilingCtx);
				ZoneScopedN("New frame");

				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Clear");
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

				Submit(CommandList);
			}
		);

		// Mesh
		{
			ZoneScopedN("ImGui sliders");

			ImGui::SliderFloat("FOV", &gMainCamera.Fov, 5, 160);
			ImGui::SliderFloat("Near", &gMainCamera.Near, 0.01, 3);
			ImGui::SliderFloat("Far", &gMainCamera.Far, 10, 1000);
			ImGui::DragFloat3("Eye", &gMainCamera.Eye.x);
			ImGui::DragFloat2("Angles", &gMainCamera.Angles.x, 0.05);

			if (ImGui::IsKeyDown(ImGuiKey_W))
			{
				gMainCamera.Eye.z += 0.01;
			}
			if (ImGui::IsKeyDown(ImGuiKey_S))
			{
				gMainCamera.Eye.z -= 0.01;
			}
			if (ImGui::IsKeyDown(ImGuiKey_A))
			{
				gMainCamera.Eye.x += 0.01;
			}
			if (ImGui::IsKeyDown(ImGuiKey_D))
			{
				gMainCamera.Eye.x -= 0.01;
			}
			if (ImGui::IsKeyDown(ImGuiKey_Q))
			{
				gMainCamera.Eye.y += 0.01;
			}
			if (ImGui::IsKeyDown(ImGuiKey_E))
			{
				gMainCamera.Eye.y -= 0.01;
			}

			EnqueueToRenderThread(
				[
					MainCamera = gMainCamera,
					&DefaultTexture,
					&SceneColor,
					&DepthBuffer,
					&Window
				]()
				{
					TArray<D3D12CmdList> CommandLists;
					D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Main thread drawing meshes");
					{
						ZoneScopedN("Drawing meshes");

						const int DrawingThreads = 6;

						float Fov = MainCamera.Fov;
						float Near = MainCamera.Near;
						float Far = MainCamera.Far;
						Vec2 Angles = MainCamera.Angles;
						Vec3 Eye = MainCamera.Eye;

						Matrix4 Projection = CreatePerspectiveMatrixReverseZ(DegreesToRadians(Fov), (float)Window.mSize.x / (float)Window.mSize.y, Near);
						Matrix4 View = CreateViewMatrix(-Eye, Angles);
						Matrix4 VP = View * Projection;

						CommandLists.push_back(MOVE(CommandList));
						CommandLists.resize(DrawingThreads);
						for (auto It = CommandLists.begin() + 1; It != CommandLists.end(); ++It)
						{
							*It = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Worker thread drawing meshes");
						}

						ParallelFor(
							[
								&CommandLists,
								&DefaultTexture,
								&VP, &SceneColor, &DepthBuffer
							](u64 Index, u64 Begin, u64 End)
							{
								D3D12CmdList& CmdList = CommandLists[Index];
								ID3D12GraphicsCommandList* CommandList = CmdList.Get();

								TracyD3D12Zone(gGraphicsProfilingCtx, CommandList, "Render Meshes from worker");
								PIXScopedEvent(CommandList, __LINE__, "Render Meshes from worker");

								CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)SceneColor.Width, (float)SceneColor.Height);
								CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

								CommandList->RSSetViewports(1, &SceneColorViewport);
								CommandList->RSSetScissorRects(1, &SceneColorScissor);

								BindRenderTargets(CommandList, {SceneColor.RTV}, DepthBuffer.DSV);
								CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

								for (u64 i = Begin; i < End; ++i)
								{
									auto& Mesh = gMeshes[i];
									for (UINT ID : Mesh.MeshIDs)
									{
										if (ID >= gMeshDatas.size())
											continue;

										MeshData& MeshData = gMeshDatas[ID];
										Matrix4 Scale = CreateScaleMatrix(MeshData.Scale);
										Matrix4 Translation = CreateTranslationMatrix(MeshData.Offset);
										Matrix4 Combined = Scale * Translation * Mesh.Transform * VP;

										uint32_t MatIndex = MeshData.MaterialIndex;
										TextureData* Texture = nullptr;

										if (MatIndex < gMaterials.size())
										{
											const auto& Tex = gMaterials[MatIndex].DiffuseTextures[0];
											if (Tex.Numbered)
											{
												Texture = &gNumberedTextures[Tex.Index];
											}
											else
											{
												Texture = &gLoadedTextures[Tex.Index];
											}
										}
										else
										{
											Texture = &DefaultTexture;
										}

										BindDescriptors(CommandList, *Texture);
										CommandList->SetGraphicsRoot32BitConstants(1, sizeof(Combined)/4, &Combined, 0);
										CommandList->SetPipelineState(MeshData.PSO);

										D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
										VertexBufferView.BufferLocation = MeshData.VertexBufferCachedPtr;
										VertexBufferView.SizeInBytes = MeshData.VertexBufferSize;
										VertexBufferView.StrideInBytes = MeshData.VertexSize;
										CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);

										D3D12_INDEX_BUFFER_VIEW IndexBufferView;
										IndexBufferView.BufferLocation = MeshData.IndexBufferCachedPtr;
										IndexBufferView.SizeInBytes = MeshData.IndexBufferSize;
										IndexBufferView.Format = MeshData.b16BitIndeces ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
										CommandList->IASetIndexBuffer(&IndexBufferView);

										CommandList->DrawIndexedInstanced(MeshData.IndexBufferSize / (MeshData.b16BitIndeces ? 2 : 4), 1, 0, 0, 0);
									}
								}
							}, gMeshes.size(), DrawingThreads
						);
					}
					Submit(CommandLists);
				}
			);
		}

		EnqueueToRenderThread(
			[
				&SceneColor,
				&BlitShader,
				&Quad,
				&Window,
				FrameFence = Fence.Get(),
				WaitEvent,
				CurrentBackBufferIndex
			]
			() {
				ZoneScopedN("SceneColor(render -> srv)");
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Blit scenecolor to backbuffer");
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

					CommandList->SetPipelineState(BlitShader.PSO.Get());

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
				Submit(CommandList);
				FlushUpload();

				RunDelayedWork();
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
			DrawLists.resize(DrawData->CmdListsCount);
			for (int i = 0; i < DrawData->CmdListsCount; ++i)
			{
				DrawLists[i] = DrawData->CmdLists[i]->CloneOutput();
			}

			EnqueueToRenderThread(
				[
					TotalVtxCount = DrawData->TotalVtxCount,
					TotalIdxCount = DrawData->TotalIdxCount,
					DrawLists = MOVE(DrawLists),
					&GuiShader,
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

				{
					ZoneScopedN("GUI buffer upload");
					UINT64 VtxOffset = GuiVBuffer.Offset;
					UINT64 IdxOffset = GuiIBuffer.Offset;

					uint8_t* VtxP = NULL;
					uint8_t* IdxP = NULL;
					GuiVBuffer->Map(0, nullptr, (void**)&VtxP);
					GuiIBuffer->Map(0, nullptr, (void**)&IdxP);
					for (ImDrawList* ImGuiCmdList : DrawLists)
					{
						memcpy(VtxP + VtxOffset, ImGuiCmdList->VtxBuffer.Data, ImGuiCmdList->VtxBuffer.size_in_bytes());
						VtxOffset += ImGuiCmdList->VtxBuffer.Size * sizeof(ImDrawVert);
						memcpy(IdxP + IdxOffset, ImGuiCmdList->IdxBuffer.Data, ImGuiCmdList->IdxBuffer.size_in_bytes());
						IdxOffset += ImGuiCmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
					}
					GuiVBuffer->Unmap(0, nullptr);
					GuiIBuffer->Unmap(0, nullptr);
				}

				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Rener GUI");
				{
					TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Render GUI");
					PIXScopedEvent(CommandList.Get(), __LINE__, "Render GUI");
					BindDescriptors(CommandList.Get(), GetGUIFont());

					TextureData& RenderTarget = GetBackBuffer(CurrentBackBufferIndex);
					BindRenderTargets(CommandList.Get(), { RenderTarget.RTV }, (uint32_t)-1);
					CommandList->SetPipelineState(GuiShader.PSO.Get());
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
					for (int i = 0; i < DrawLists.size(); ++i)
					{
						ImDrawList* ImGuiCmdList = DrawLists[i];

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
				}
				Submit(CommandList);
				TicketGPU GuiRenderingDone = CurrentFrameTicket();

				EnqueueDelayedWork(
					[GuiVBuffer, GuiIBuffer]() mutable {
						DiscardTransientBuffer(GuiVBuffer);
						DiscardTransientBuffer(GuiIBuffer);
					},
					GuiRenderingDone
				);

				for (ImDrawList* List : DrawLists)
				{
					IM_DELETE(List);
				}
			});
		}

#define CHECK_SCREENSHOT_CODE 0
#if CHECK_SCREENSHOT_CODE || TRACY_ENABLE // Send screenshot to Tracy
		if (CHECK_SCREENSHOT_CODE || TracyIsConnected)
		{
			static bool ScreenShotInFlight = false;
			if (!ScreenShotInFlight)
			{
				ScreenShotInFlight = true;
				EnqueueToRenderThread(
					[
						&Window,
						&SceneColor,
						&Quad,
						&FfxSpd
					] () {
					D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Downsample scenecolor to readback");

					TextureData Small = {};
					PooledBuffer SceneColorStagingBuffer;
					u16 x, y;
					u32 NeededMipIndex = -1;
					{
						ZoneScopedN("Downsample scenecolor to readback");
						TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Downsample scenecolor to readback");
						PIXScopedEvent(CommandList.Get(), __LINE__, "Downsample scenecolor to readback");

						{
							Small.Format = READBACK_FORMAT;
							Small.Width  = (u16)Window.mSize.x;
							Small.Height = (u16)Window.mSize.y;
							while (Small.Width * Small.Height * 4 > 256_kb)
							{
								Small.Width  >>= 1;
								Small.Height >>= 1;
								NeededMipIndex++;
							}
							GetTransientTexture(Small,
								D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
								D3D12_RESOURCE_STATE_COMMON,
								nullptr
							);
							D3D12_UNORDERED_ACCESS_VIEW_DESC Desc;
							Desc.Format = (DXGI_FORMAT)Small.Format;
							Desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
							Small.UAV = OutputTextures[NeededMipIndex].UAV;
							CreateUAV(Small);
						}
						x = Small.Width;
						y = Small.Height;

						{
							CD3DX12_RESOURCE_BARRIER barriers[] = {
								CD3DX12_RESOURCE_BARRIER::Transition(
									GetTextureResource(Small.ID),
									D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
								),
								CD3DX12_RESOURCE_BARRIER::Transition(
									GetTextureResource(SceneColor.ID),
									D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
								),
							};
							CommandList->ResourceBarrier(ArrayCount(barriers), barriers);
						}

						{
							CommandList->SetComputeRootSignature(FfxSpd.RootSignature);
							CommandList->SetPipelineState(FfxSpd.PSO.Get());
							BindDescriptors(CommandList.Get(), SceneColor);

							CommandList->SetComputeRootDescriptorTable(0, D3D12_GPU_DESCRIPTOR_HANDLE{GetGeneralHandleGPU(SceneColor.SRV)});
							//CommandList->SetComputeRootUnorderedAccessView();

							varAU2(dispatchThreadGroupCountXY); // output variable
							varAU2(workGroupOffset);  // output variable, this constants are required if Left and Top are not 0,0
							varAU2(numWorkGroupsAndMips); // output variable
							// input information about your source texture:
							// left and top of the rectancle within your texture you want to downsample
							// width and height of the rectancle you want to downsample
							// if complete source texture should get downsampled: left = 0, top = 0, width = sourceTexture.width, height = sourceTexture.height
							varAU4(rectInfo) = initAU4(0, 0, SceneColor.Width, SceneColor.Height); // left, top, width, height
							SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);
							// constants:
							struct SpdConstants {
							   u32 numWorkGroups;
							   u32 mips;
							   float invInputSizeX;
							   float invInputSizeY;
							};
							SpdConstants Data;
							Data.numWorkGroups = numWorkGroupsAndMips[0];
							Data.mips = numWorkGroupsAndMips[1];
							Data.invInputSizeX = 1.f / SceneColor.Width;
							Data.invInputSizeY = 1.f / SceneColor.Height;

							//CommandList->SetComputeRootDescriptorTable(2);
							//PooledBuffer AtomicBuffer;
							//GetTransientBuffer(AtomicBuffer, 6 * 4, BUFFER_GENERIC);
							//CommandList->ClearUnorderedAccessViewFloat();
							CommandList->SetComputeRootUnorderedAccessView(1, AtomicBuffer->GetGPUVirtualAddress());
							CommandList->SetComputeRootDescriptorTable(2, D3D12_GPU_DESCRIPTOR_HANDLE{OutputUAVDescriptors});
							CommandList->SetComputeRoot32BitConstants(3, 4, &Data, 0);

							uint32_t dispatchX = dispatchThreadGroupCountXY[0];
							uint32_t dispatchY = dispatchThreadGroupCountXY[1];
							uint32_t dispatchZ = 1;
							CommandList->Dispatch(dispatchX, dispatchY, dispatchZ);

							{
								CD3DX12_RESOURCE_BARRIER barriers[] = {
									CD3DX12_RESOURCE_BARRIER::Transition(
										GetTextureResource(Small.ID),
										D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON
									),
									CD3DX12_RESOURCE_BARRIER::Transition(
										GetTextureResource(SceneColor.ID),
										D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
									),
								};
								CommandList->ResourceBarrier(ArrayCount(barriers), barriers);
							}
						}
					}
					DiscardTransientTexture(Small);

					Submit(CommandList);

					TicketGPU DownsampleDone = CurrentFrameTicket();

					ZoneScopedN("Readback screen capture");
					TicketGPU CopyDone;
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

						GetTransientBuffer(SceneColorStagingBuffer, ResourceSize, BUFFER_STAGING);

						CD3DX12_TEXTURE_COPY_LOCATION Dst(SceneColorStagingBuffer.Get(), bufferFootprint);
						CD3DX12_TEXTURE_COPY_LOCATION Src(GetTextureResource(Small.ID));

						InsertWait(D3D12_COMMAND_LIST_TYPE_COPY, DownsampleDone);
						D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_COPY, L"Copy screenshot texture");
						{
							//TracyD3D12Zone(gCopyProfilingCtx, CommandList.Get(), "Copy screenshot texture");
							CommandList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
						}
						Submit(CommandList);
						CopyDone = Signal(D3D12_COMMAND_LIST_TYPE_COPY);
					}

					EnqueueDelayedWork(
						[
							SceneColorStagingBuffer, x, y
						]
					() mutable {
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
						FrameImage(Data, x, y, 3, false);
					#endif
						ResourcePtr->Unmap(0, NULL);

						DiscardTransientBuffer(SceneColorStagingBuffer);

						ScreenShotInFlight = false;
					},
					CopyDone);
				});
			}
		}
#endif
		EnqueueToRenderThread(
		[
			FrameFence = Fence.Get(),
			CurrentBackBufferIndex
		]()
		{
			{
				ZoneScopedN("Present");
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Present");
				{
					// backbuffer(render -> present)
					CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						GetBackBufferResource(CurrentBackBufferIndex),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
					CommandList->ResourceBarrier(1, &barrier);
				}
				Submit(CommandList);
				Signal(CommandList.Type);
				PresentCurrentBackBuffer();
			}

			TracyD3D12Collect(gGraphicsProfilingCtx);
			TracyD3D12Collect(gComputeProfilingCtx);

			PIXEndEvent(GetGPUQueue());

			FrameMarkEnd("Render thread");
		});

		CurrentBackBufferIndex = (CurrentBackBufferIndex + 1) % BACK_BUFFER_COUNT;
	}

	if (CurrentBackBufferIndex == 0)
	{
		CurrentBackBufferIndex = BACK_BUFFER_COUNT;
	}

	EnqueueToRenderThread([FrameFence = Fence.Get(), CurrentBackBufferIndex, WaitEvent]() {
		FlushQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		ReleaseTextures();
		TracyD3D12Destroy(gGraphicsProfilingCtx);
		TracyD3D12Destroy(gComputeProfilingCtx);
	});
	StopRenderThread();
	StopWorkerThreads();
	CloseHandle(WaitEvent);
}

