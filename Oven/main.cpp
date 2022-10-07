
#include "Threading/Worker.h"
#include "Containers/Array.h"
#include "Assets/Pak.h"
#include "Assets/Mesh.h"
#include "Assets/Material.h"
#include "Assets/DDS.h"
#include "Util/Math.h"
#include "external/stb/stb_image.h"
#include "Containers/RingBuffer.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <Tracy.hpp>
#include <filesystem>
#include <TracyDxt1.hpp>
#include <Assets/File.h>

//u8* WorkingMemory = (u8*)AllocateRingBuffer(4_gb, nullptr);
//std::atomic<u32> Offset = 0;

int main(void)
{
	InitDebug();

	StartWorkerThreads();

	using recursive_directory_iterator = std::filesystem::recursive_directory_iterator;
	TArray<TicketCPU> Tickets;

	for (const auto& DirEntry : recursive_directory_iterator("./content/"))
	{
		if (!DirEntry.is_directory())
		{
			std::filesystem::path Extension = DirEntry.path().extension();
			if (Extension == ".fbx" || Extension == ".glb")
			{
				Tickets.push_back() = EnqueueToWorkerWithTicket([DirEntry]()
				{
					std::string FilePath = DirEntry.path().string();
					std::string NewPath = FilePath;
					auto Pos = NewPath.find('\\', 0);
					while (Pos != NewPath.npos)
					{
						NewPath.replace(Pos, 1, 1, '/');
						Pos = NewPath.find('\\', Pos);
					}

					{
						std::string_view BasePath = "content/";
						auto It = NewPath.find(BasePath);
						CHECK(It != NewPath.npos, "");
						NewPath.replace(It, BasePath.length(), "cooked/");
						NewPath += "pak";
					}

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

					PakFile Pak = CreatePak(NewPath.c_str());

					if (!Scene)
						return;

					if (Scene->HasMaterials())
					{
						StringView MeshFolder(FilePath.c_str(), FilePath.find_last_of("\\/"));
						for (u64 i = 0; i < Scene->mNumMaterials; ++i)
						{
							aiMaterial* MaterialPtr = Scene->mMaterials[i];
							MaterialDescription TmpMaterial;
							TmpMaterial.Name = String(MaterialPtr->GetName().C_Str());

							for (aiTextureType TextureType = aiTextureType_NONE;
								TextureType < aiTextureType_UNKNOWN;
								TextureType = (aiTextureType)(TextureType + 1))
							{
								for (u32 j = 0, TextureCount = MaterialPtr->GetTextureCount(TextureType); j < TextureCount; ++j)
								{
									aiString TexPath;
									aiTextureMapping mapping = aiTextureMapping_UV;
									unsigned int uvindex = 0;
									ai_real blend = 0;
									aiTextureOp op = aiTextureOp_Multiply;
									aiTextureMapMode mapmode[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };

									MaterialPtr->GetTexture(TextureType, j, &TexPath, &mapping, &uvindex, &blend, &op, mapmode);

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

										int Channels = 0, w = 0, h = 0;
										u8* Data = stbi_load_from_memory(
											(u8*)Texture->pcData,
											Texture->mWidth,
											&w, &h, &Channels, 4
										);
										u64 CompressedSize = w * h / 2;

										String CompressedData(CompressedSize + 4 + sizeof(DDS_HEADER) + sizeof(DDS_HEADER_DXT10), '\0');

										u8 *Ptr = (u8*)CompressedData.data();

										WriteAndAdvance<u32>(Ptr, DDS_MAGIC);

										DDS_HEADER Header{};
										Header.ddspf.dwFlags = DDPF_FOURCC;
										Header.ddspf.dwFourCC = MAGIC(DX10);
										Header.ddspf.dwSize = CompressedSize;

										WriteAndAdvance<DDS_HEADER>(Ptr, Header);

										DDS_HEADER_DXT10 HeaderDX10 {};
										HeaderDX10.dxgiFormat = DXGI_FORMAT_BC1_UNORM;
										HeaderDX10.resourceDimension = D3D10_RESOURCE_DIMENSION_TEXTURE2D;
										HeaderDX10.arraySize = 1;

										WriteAndAdvance<DDS_HEADER_DXT10>(Ptr, HeaderDX10);

										tracy::CompressImageDxt1((char*)Data, (char*)Ptr, w, h);
										free(Data);

										InsertIntoPak(Pak, TexPath.data, (u8*)CompressedData.data(), CompressedData.size(), true);
									}
									else
									{
										String Path(MeshFolder);
										Path.append(1, '/');
										Path.append(TexPath.data, TexPath.length);

										String Binary = LoadWholeFile(Path);
										if (u8* Data = (u8*)Binary.data())
										{
											InsertIntoPak(Pak, Path.c_str(), Data, Binary.size(), true);
										}
										else
										{
											DEBUG_BREAK();
										}
									}
								}
							}
						}
					}

					{
						ZoneScopedN("Upload mesh data");
						for (u64 i = 0; i < Scene->mNumMeshes; ++i)
						{
							aiMesh* Mesh = Scene->mMeshes[i];
							MeshDescription Description = ExtractMeshDescription(Mesh);

							Vec3 Offset = Vec3{
								Mesh->mAABB.mMin.x,
								Mesh->mAABB.mMin.y,
								Mesh->mAABB.mMin.z,
							};
							Vec3 Scale = Vec3{
								(Mesh->mAABB.mMax.x)-(Mesh->mAABB.mMin.x),
								(Mesh->mAABB.mMax.y)-(Mesh->mAABB.mMin.y),
								(Mesh->mAABB.mMax.z)-(Mesh->mAABB.mMin.z),
							};

							String TmpMemory (
								Description.VertexBufferSize
								+ Description.IndexBufferSize,
								'\0'
							);

							UploadMeshData((u8*)TmpMemory.data(), Mesh, Description.Flags & MeshDescription::PositionPacked);
							UploadIndexData((u8*)TmpMemory.data() + Description.VertexBufferSize, Mesh);
							
							String Name = StringFromFormat("Mesh%d", i);
							InsertIntoPak(Pak, Name.c_str(), (u8*)TmpMemory.data(), TmpMemory.size(), true);
						}
					}

					{
						using namespace std;

						TQueue<pair<aiNode*, aiMatrix4x4>> ProcessingQueue;
						ProcessingQueue.emplace(Scene->mRootNode, aiMatrix4x4());

						while (!ProcessingQueue.empty())
						{
							ZoneScopedN("Pump mesh queue");
							aiNode* Current = ProcessingQueue.front().first;
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
							}
							else if (strcmp(Current->mName.C_Str(), "Camera") == 0)
							{
								aiVector3D Scale, Rotation, Location;
								CurrentTransform.Decompose(Scale, Rotation, Location);
							}
							ProcessingQueue.pop();
						}
					}

					FinalizePak(Pak);
					Importer.FreeScene();
				});
			}
			Debug::Print(DirEntry.path());
		}
	}

	for (TicketCPU T : Tickets)
	{
		WaitForCompletion(T);
	}

	StopWorkerThreads();
}
