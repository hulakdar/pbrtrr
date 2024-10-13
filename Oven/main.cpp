#include <filesystem>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <stb/stb_image_resize.h>
#include <stb/stb_image.h>
#include <dxcapi.h>
#include <stb/stb_sprintf.h>
#include <d3d12.h>

#include <EASTL/bitset.h>

#include "Common.h"
#include "AllDeclarations.h"

#include "Containers/ComPtr.h"

#include "Assets/Shader.generated.h"
#include "Assets/Mesh.generated.h"
#include "Assets/TextureDescription.generated.h"

#include "Assets/Pak.h"
#include "Assets/Scene.h"

#include "Util/Math.h"
#include "Util/Debug.h"
#include "Util/Util.h"

#include "Threading/Worker.h"

#include "Util/TypeInfo.h"
#include <d3d12shader.h>

void MaterialSetTextureType(MaterialDescription& Material, aiTextureType TextureType, u16 Index)
{
	switch (TextureType)
	{
	case aiTextureType_DIFFUSE:
	case aiTextureType_BASE_COLOR:
		CHECK(Material.DiffuseTexture == (u16)-1);
		Material.DiffuseTexture = Index;
		break;

	case aiTextureType_SPECULAR:
		CHECK(Material.SpecularTexture == (u16)-1);
		Material.SpecularTexture = Index;
		break;

	case aiTextureType_EMISSION_COLOR:
	case aiTextureType_EMISSIVE:
		CHECK(Material.EmissiveTexture == (u16)-1);
		Material.EmissiveTexture = Index;

	case aiTextureType_METALNESS:
		CHECK(Material.MetalicTexture == (u16)-1);
		Material.MetalicTexture = Index;
		break;

	case aiTextureType_DIFFUSE_ROUGHNESS:
		CHECK(Material.RoughnessTexture == (u16)-1);
		Material.RoughnessTexture = Index;
		break;
	
	case aiTextureType_NORMALS:
	case aiTextureType_NORMAL_CAMERA:
		CHECK(Material.NormalTexture == (u16)-1);
		Material.NormalTexture = Index;
		break;

	case aiTextureType_OPACITY:
		CHECK(Material.OpacityTexture == (u16)-1);
		Material.OpacityTexture = Index;
		break;

	default:
		CHECK(FALSE);
	}
}

bool CanPackPosition(aiMesh* Mesh)
{
	return false;

	Vec3 Min = Vec3{
		Mesh->mAABB.mMin.x,
		Mesh->mAABB.mMin.y,
		Mesh->mAABB.mMin.z,
	};
	Vec3 Max = Vec3{
		Mesh->mAABB.mMax.x,
		Mesh->mAABB.mMax.y,
		Mesh->mAABB.mMax.z,
	};

	Vec3 Extent = Vec3{
		std::abs(Mesh->mAABB.mMax.x - Mesh->mAABB.mMin.x),
		std::abs(Mesh->mAABB.mMax.y - Mesh->mAABB.mMin.y),
		std::abs(Mesh->mAABB.mMax.z - Mesh->mAABB.mMin.z),
	};

	float Scale = (1.0f / float(INT16_MAX));
	Vec3 ScaleVector = Vec3{Extent.x * Scale, Extent.y * Scale, Extent.z * Scale};
	for (unsigned i = 0; i < Mesh->mNumVertices; ++i)
	{
		aiVector3D Position = Mesh->mVertices[i];

		Vec4 Normalized{
			(Mesh->mVertices[i].x - Min.x) / (Extent.x),
			(Mesh->mVertices[i].y - Min.y) / (Extent.y),
			(Mesh->mVertices[i].z - Min.z) / (Extent.z),
			1.f
		};
		Vec4PackShorts Packed(Normalized);
		Vec4 Unpacked = Packed;
		float d = DistanceSquared(Normalized, Unpacked);
		if (d > (1.0/1024.0)*(1.0/1024.0))
		{
			return false;
		}
	}
	return true;
}

bool ColorHasUsefulInfo(aiMesh* Mesh)
{
	if (!Mesh->HasVertexColors(0))
	{
		return false;
	}

	for (unsigned i = 0; i < Mesh->mNumVertices; ++i)
	{
		aiColor4D C = Mesh->mColors[0][i];
		if (C.r != 1.f && C.g != 1.f && C.b != 1.f)
		{
			return true;
		}
	}
	return false;
}

MeshDescription ExtractMeshDescription(aiMesh* Mesh)
{
	bool bPositionPacked = CanPackPosition(Mesh);

	MeshDescription Result;
	Result.Flags = bPositionPacked ? MeshFlags::PositionPacked : 0;

	CHECK(Mesh->HasFaces(), "Mesh without faces?");

	u16 VertexSize = bPositionPacked ? sizeof(Vec4PackShorts) : sizeof(Vec3);

	if (Mesh->HasNormals())
	{
		Result.Flags |= MeshFlags::HasNormals;
		VertexSize += sizeof(Vec4PackUnorm);
	}

	if (Mesh->HasTextureCoords(0))
	{
		Result.Flags |= MeshFlags::HasUV0;
		VertexSize += sizeof(Vec2h);
	}

	if (ColorHasUsefulInfo(Mesh))
	{
		Result.Flags |= MeshFlags::HasVertexColor;
		VertexSize += sizeof(TVec4<u8>);
	}

	Result.VertexCount = Mesh->mNumVertices;
	Result.IndexCount = Mesh->mNumFaces * 3;
	Result.MaterialIndex = Mesh->mMaterialIndex;
	CHECK(VertexSize <= 255);
	Result.VertexSize = (u8)VertexSize;

	Result.BoxMin = Vec3{
		Mesh->mAABB.mMin.x,
		Mesh->mAABB.mMin.y,
		Mesh->mAABB.mMin.z,
	};
	Result.BoxMax = Vec3{
		Mesh->mAABB.mMax.x,
		Mesh->mAABB.mMax.y,
		Mesh->mAABB.mMax.z,
	};

	return Result;
}

void UploadIndexData(u8* CpuPtr, aiMesh* Mesh)
{
	bool b16BitIndeces = Mesh->mNumVertices <= UINT16_MAX;
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

void UploadMeshData(u8* CpuPtr, aiMesh* Mesh, bool PositionPacked)
{
	Vec3 Min = Vec3{
		Mesh->mAABB.mMin.x,
		Mesh->mAABB.mMin.y,
		Mesh->mAABB.mMin.z,
	};
	Vec3 Max = Vec3{
		Mesh->mAABB.mMax.x,
		Mesh->mAABB.mMax.y,
		Mesh->mAABB.mMax.z,
	};

	Vec3 Extent = Max - Min;

	float Scale = (1.0f / float(INT16_MAX));
	Vec3 ScaleVector = Vec3{Extent.x * Scale, Extent.y * Scale, Extent.z * Scale};

	bool HasUsefulNormalData = Mesh->HasNormals();
	bool HasUsefulColorData = ColorHasUsefulInfo(Mesh);
	bool HasUsefulUVData = Mesh->HasTextureCoords(0);

	for (UINT i = 0; i < Mesh->mNumVertices; ++i)
	{
		if (PositionPacked)
		{
			u16 PackedColor = 0;

			if (HasUsefulColorData)
			{
				PackedColor = RGBto565(Mesh->mColors[0][i].r, Mesh->mColors[0][i].g, Mesh->mColors[0][i].b);
			}

			Vec4 Normalized{
				(Mesh->mVertices[i].x - Min.x) * ScaleVector.x,
				(Mesh->mVertices[i].y - Min.y) * ScaleVector.y,
				(Mesh->mVertices[i].z - Min.z) * ScaleVector.z,
				0.f
			};
			Vec4PackShorts Packed = Vec4PackShorts(&Normalized.x);
			Packed.w = i16(PackedColor);
			WriteAndAdvance(CpuPtr, Packed);
		}
		else
		{
			WriteAndAdvance(CpuPtr, Mesh->mVertices[i]);
		}

		if (HasUsefulNormalData)
		{
			WriteAndAdvance(CpuPtr,
				Vec4PackUnorm(
					Mesh->mNormals[i].x * 0.5f + 0.5f,
					Mesh->mNormals[i].y * 0.5f + 0.5f,
					Mesh->mNormals[i].z * 0.5f + 0.5f,
					1.0f
				)
			);
		}

		if (HasUsefulUVData)
		{
			WriteAndAdvance(CpuPtr, Vec2h{
				half(Mesh->mTextureCoords[0][i].x),
				half(Mesh->mTextureCoords[0][i].y),
			});
		}
	}
}
namespace
{
	Color4 AiColorToColor(aiColor4D In)
	{
		return Color4{
			(u8)(In.r * UINT8_MAX),
			(u8)(In.g * UINT8_MAX),
			(u8)(In.b * UINT8_MAX),
			(u8)(In.a * UINT8_MAX),
		};
	}
}

struct ParsedArgs
{
	ParsedArgs(int Argc, const char* Argv[])
	{
		for (int i = 1; i < Argc; ++i)
		{
			Text.push_back(Argv[i]);
		}
	}

	bool Includes(StringView Arg)
	{
		for (StringView Candidate : Text)
		{
			if (Candidate.size() == Candidate.size() && strncmp(Candidate.data(), Arg.data(), Arg.size()) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool Empty()
	{
		return Text.empty();
	}

	TArray<StringView> Text;
};

int main(int Argc, const char* Argv[])
{
	ParsedArgs Args(Argc, Argv);

	StartWorkerThreads();

	using recursive_directory_iterator = std::filesystem::recursive_directory_iterator;
	TArray<TicketCPU> Tickets;

	InitShaderCompiler();

	InitDirectStorage();

	FrameMark;

	if (Args.Empty() || Args.Includes("cook_content"))
	{
		ZoneScopedN("cook_content kickoff");
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

						Assimp::Importer Importer;

						const aiScene* Scene = nullptr;
						{
							ZoneScopedN("Scene file parsing");
							Scene = Importer.ReadFile(
								FilePath.c_str(),
								(unsigned int)(aiProcess_GenBoundingBoxes | aiProcess_ConvertToLeftHanded | aiProcessPreset_TargetRealtime_MaxQuality)
							);
							CHECK(Scene != nullptr, "Load failed");

							if (!Scene)
								return;
						}

						String NewPath = String(FilePath.c_str());
						{
							auto Pos = NewPath.find('\\', 0);
							while (Pos != NewPath.npos)
							{
								NewPath.replace(Pos, 1, 1, '/');
								Pos = NewPath.find('\\', Pos);
							}
						}

						{
							StringView BasePath = "content/";
							auto It = NewPath.find(BasePath.data());
							CHECK(It != NewPath.npos, "");
							NewPath.replace(It, BasePath.length(), "cooked/");
							NewPath += "pak";
						}

						PakFileWriter Pak = CreatePak(NewPath);

						TMap<String, u64> NodeNameToIndex;
						{
							TArray<Node>                StaticGeometry;
							using namespace std;

							StaticGeometry.reserve(Scene->mNumMeshes);
							TFunction<u32(aiNode*, const aiMatrix4x4&, LocalBounds&, u64)> VisitRecursively =
							[&](aiNode* Current, const aiMatrix4x4& ParentTransform, LocalBounds& ParentBounds, u64 ParentIndex) -> u32
							{
								ZoneScopedN("Pump mesh queue");

								u64 Index = StaticGeometry.size();
								StaticGeometry.push_back();

								u32 NumChildren = Current->mNumChildren;
								aiMatrix4x4 CurrentTransform = ParentTransform * Current->mTransformation;

								LocalBounds Bounds{ 0.f, 0.f };
								for (u32 i = 0; i < Current->mNumChildren; ++i)
								{
									NumChildren += VisitRecursively(Current->mChildren[i], CurrentTransform, Bounds, Index);
								}

								CurrentTransform.Transpose();

								Node& Result = StaticGeometry[Index];

								for (u32 i = 0; i < Current->mNumMeshes; ++i)
								{
									aiMesh* Mesh = Scene->mMeshes[Current->mMeshes[i]];
									for (unsigned int j = 0; j < Mesh->mNumVertices; ++j)
									{
										aiVector3D P = Mesh->mVertices[j];
										Vec3 Position (P[0]);
										float L = sqrt(Dot(Position, Position));
										Bounds.SphereRadius = std::max(Bounds.SphereRadius, L);
										Bounds.BoxExtent.x = std::max(Bounds.BoxExtent.x, abs(Position.x));
										Bounds.BoxExtent.y = std::max(Bounds.BoxExtent.y, abs(Position.y));
										Bounds.BoxExtent.z = std::max(Bounds.BoxExtent.z, abs(Position.z));
									}
								}

								ParentBounds.SphereRadius = std::max(Bounds.SphereRadius, ParentBounds.SphereRadius);
								ParentBounds.BoxExtent.x = std::max(Bounds.BoxExtent.x, abs(ParentBounds.BoxExtent.x));
								ParentBounds.BoxExtent.y = std::max(Bounds.BoxExtent.y, abs(ParentBounds.BoxExtent.y));
								ParentBounds.BoxExtent.z = std::max(Bounds.BoxExtent.z, abs(ParentBounds.BoxExtent.z));

								Result.Bounds = Bounds;
								CHECK(NumChildren <= UINT16_MAX);
								Result.NumStaticChildren = (u16)NumChildren;
								Result.Transform = Matrix4(CurrentTransform[0]);

								String Name = String(Current->mName.C_Str());
								if (NodeNameToIndex.find(Name) == NodeNameToIndex.end())
								{
									NodeNameToIndex.emplace(Name, Index);
								}

								if (ParentIndex == 0)
								{
									Result.OffsetBackToParent = 0;
								}
								else
								{
									CHECK(Index > ParentIndex);
									CHECK(Index - ParentIndex < UINT16_MAX);
									Result.OffsetBackToParent = (u16)(Index - ParentIndex);
								}

								if (Current->mNumMeshes > 0)
								{
									CHECK(Current->mNumMeshes < 255);
									std::sort(Current->mMeshes, Current->mMeshes + Current->mNumMeshes);

									Node* CurrentMesh = &Result;

									u16 i = 0;
									u16 Count = 0;
									while (i < Current->mNumMeshes - 1)
									{
										if (Current->mMeshes[i] + 1 != Current->mMeshes[i + 1])
										{
											CurrentMesh->MeshIDStart = Current->mMeshes[i - Count];
											CurrentMesh->MeshCount = Count + 1;
											Index = StaticGeometry.size();
											auto& NewMesh = StaticGeometry.push_back();
											NewMesh = Result;
											Result.NumStaticChildren++;
											NewMesh.NumStaticChildren = 0;
											NewMesh.OffsetBackToParent = u16(Index - ParentIndex);
											CurrentMesh = &NewMesh;

											Count = 0;
										}
										else
										{
											Count++;
										}
										i++;
									}

									CurrentMesh->MeshIDStart = Current->mMeshes[i - Count];
									CurrentMesh->MeshCount = Count + 1;
								}

								return NumChildren;
							};
							LocalBounds Bounds;
							VisitRecursively(Scene->mRootNode, aiMatrix4x4(), Bounds, 0);
							//Debug::Print("Whole scene radius: ", Bounds.SphereRadius);
							InsertIntoPak(Pak, "___Scene_StaticGeometry", ContainerToView(StaticGeometry));
						}

						if (Scene->HasCameras())
						{
							TArray<Camera> Cameras;
							Cameras.reserve(Scene->mNumCameras);
							for (u64 i = 0; i < Scene->mNumCameras; ++i)
							{
								aiCamera* Cam = Scene->mCameras[i];
								String Name = String(Cam->mName.C_Str());
								auto It = NodeNameToIndex.find(Name);
								CHECK(It != NodeNameToIndex.end());

								Camera& Result = Cameras.push_back();
								CHECK(Cam->mOrthographicWidth == 0);
								CHECK(Cam->mPosition.Length() == 0);
								Result.Fov = Cam->mHorizontalFOV;
								Result.Position = Vec3(&Cam->mPosition[0]);
								Result.NodeID = It->second;
							}
							InsertIntoPak(Pak, "___Cameras", ContainerToView(Cameras));
						}

						if (Scene->HasAnimations())
						{
							for (u64 i = 0; i < Scene->mNumAnimations; ++i)
							{
								aiAnimation* Anim = Scene->mAnimations[i];
								String AnimName = String(Anim->mName.C_Str());
								for (unsigned j = 0; j < Anim->mNumChannels; ++j)
								{
									aiNodeAnim* NodeAnim = Anim->mChannels[j];
									String NodeName = String(NodeAnim->mNodeName.C_Str());
									CHECK(NodeNameToIndex.find(NodeName) != NodeNameToIndex.end());
								}
							}
						}

						if (Scene->HasLights())
						{
							for (u64 i = 0; i < Scene->mNumLights; ++i)
							{
								aiLight* Light = Scene->mLights[i];
								String Name = String(Light->mName.C_Str());
								CHECK(NodeNameToIndex.find(Name) != NodeNameToIndex.end());

								switch (Light->mType)
								{
								case aiLightSource_DIRECTIONAL: break;
								case aiLightSource_POINT:       break;
								case aiLightSource_SPOT:        [[fallthrough]];
								case aiLightSource_AMBIENT:     [[fallthrough]];
								case aiLightSource_AREA:        [[fallthrough]];
								default:
									CHECK(false);
									break;
								}
							}
						}

						{
							TArray<String> TextureNames;
							TArray<MaterialDescription> Materials;
							if (Scene->HasMaterials())
							{
								StringView MeshFolder(FilePath.c_str(), FilePath.find_last_of("\\/"));
								for (u64 i = 0; i < Scene->mNumMaterials; ++i)
								{
									MaterialDescription& Material = Materials.push_back();
									Material.Flags = 0;

									aiMaterial* MaterialPtr = Scene->mMaterials[i];
									String MaterialName = String(MaterialPtr->GetName().C_Str());

									{
										aiShadingMode ShadingMode{};
										MaterialPtr->Get(AI_MATKEY_SHADING_MODEL, ShadingMode);
										Material.Flags |= ShadingMode == aiShadingMode_PBR_BRDF ? MaterialFlags::PBR : 0;
									}

									{
										aiColor4D Color;
										if (Material.Flags & MaterialFlags::PBR)
										{
											MaterialPtr->Get(AI_MATKEY_BASE_COLOR, Color);
										}
										else
										{
											MaterialPtr->Get(AI_MATKEY_COLOR_DIFFUSE, Color);
										}
										MaterialPtr->Get(AI_MATKEY_OPACITY, Color.a);
										Material.DiffuseColorAndOpacity = AiColorToColor(Color);

										MaterialPtr->Get(AI_MATKEY_COLOR_EMISSIVE, Color);
										Color.a = 1;
										Material.ColorEmissive = AiColorToColor(Color);
									}

									{
										int TwoSided{};
										MaterialPtr->Get(AI_MATKEY_TWOSIDED, TwoSided);
										Material.Flags |= TwoSided ? MaterialFlags::TwoSided : 0;
									}

									{
										aiString ShaderLang{};
										aiString ShaderVert{};
										aiString ShaderFrag{};
										MaterialPtr->Get(AI_MATKEY_GLOBAL_SHADERLANG, ShaderLang);
										MaterialPtr->Get(AI_MATKEY_SHADER_VERTEX, ShaderVert);
										MaterialPtr->Get(AI_MATKEY_SHADER_FRAGMENT, ShaderFrag);
										CHECK(ShaderLang.length == 0);
										CHECK(ShaderVert.length == 0);
										CHECK(ShaderFrag.length == 0);
									}

									if (Material.Flags & MaterialFlags::PBR)
									{
										int UseMetallicMap{};
										int UseRoughnessMap{};
										int UseEmissiveMap{};
										int UseAOMap{};
										MaterialPtr->Get(AI_MATKEY_USE_METALLIC_MAP, UseMetallicMap);
										MaterialPtr->Get(AI_MATKEY_USE_ROUGHNESS_MAP, UseRoughnessMap);
										MaterialPtr->Get(AI_MATKEY_USE_EMISSIVE_MAP, UseEmissiveMap);
										MaterialPtr->Get(AI_MATKEY_USE_AO_MAP, UseAOMap);

										Material.Flags |= UseMetallicMap ? MaterialFlags::MetallicMap : 0;
										Material.Flags |= UseRoughnessMap ? MaterialFlags::RoughnessMap : 0;
										Material.Flags |= UseEmissiveMap ? MaterialFlags::EmissiveMap : 0;
										Material.Flags |= UseAOMap ? MaterialFlags::AOMap : 0;

										float MetallicFactor{};
										float RoughnessFactor{};

										MaterialPtr->Get(AI_MATKEY_METALLIC_FACTOR, MetallicFactor);
										MaterialPtr->Get(AI_MATKEY_ROUGHNESS_FACTOR, RoughnessFactor);
										MaterialPtr->Get(AI_MATKEY_EMISSIVE_INTENSITY, Material.EmissiveIntensity);

										CHECK(MetallicFactor == 1.0f || UseMetallicMap);
										CHECK(RoughnessFactor == 1.0f || UseRoughnessMap);
										CHECK(MetallicFactor >= 0.0f && MetallicFactor <= 1.0f);
										CHECK(RoughnessFactor >= 0.0f && RoughnessFactor <= 1.0f);

										Material.MetallicFactor   = (u8)(MetallicFactor * 255.f);
										Material.RoughnessFactor = (u8)(RoughnessFactor * 255.f);
									}

									for (aiTextureType TextureType = aiTextureType_DIFFUSE;
										TextureType <= AI_TEXTURE_TYPE_MAX;
										TextureType = (aiTextureType)(TextureType + 1))
									{
										CHECK(MaterialPtr->GetTextureCount(TextureType) <= 1);
										for (u32 j = 0, TextureCount = MaterialPtr->GetTextureCount(TextureType); j < TextureCount; ++j)
										{
											CHECK(TextureCount == 1);
											{
												aiUVTransform UVTransform;
												MaterialPtr->Get(AI_MATKEY_UVTRANSFORM(TextureType, j), UVTransform);
												CHECK(
													UVTransform.mTranslation == aiVector2D(0, 0)
													&& UVTransform.mScaling == aiVector2D(1, 1)
													&& UVTransform.mRotation == 0
												);

												enum aiTextureFlags Flags {};
												MaterialPtr->Get(AI_MATKEY_TEXFLAGS(TextureType, j), Flags);
												CHECK(Flags == 0);

												aiTextureMapping TexMapping{};
												MaterialPtr->Get(AI_MATKEY_UVWSRC(TextureType, j), TexMapping);
												CHECK(TexMapping == aiTextureMapping_UV);
											}

											aiString TexPath;
											{
												aiTextureMapping mapping = aiTextureMapping_UV;
												unsigned int uvindex = 0;
												ai_real blend = 0;
												aiTextureOp op = aiTextureOp_Multiply;
												aiTextureMapMode mapmode[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };

												MaterialPtr->GetTexture(TextureType, j, &TexPath, &mapping, &uvindex, &blend, &op, mapmode);

												CHECK(TexPath.length != 0);
												CHECK(mapmode[0] == aiTextureMapMode_Wrap && mapmode[1] == aiTextureMapMode_Wrap);
												CHECK(op == aiTextureOp_Multiply);
												CHECK(mapping == aiTextureMapping_UV);
												CHECK(uvindex == 0);
												CHECK(blend == 0);
											}

											if (TexPath.data[0] == '*')
											{
												u32 Index = atoi(TexPath.C_Str() + 1);
												Material.DiffuseTexture = u16(Index);

												aiTexture* Texture = Scene->mTextures[Index];

												int Channels = 0, w = 0, h = 0;
												u8* Data = stbi_load_from_memory(
													(u8*)Texture->pcData,
													Texture->mWidth,
													&w, &h, &Channels, 4
												);
												CHECK(w == h);
												CHECK(_mm_popcnt_u32(w) == 1);
												u32 Log2OfSize = _tzcnt_u32(w);
												CHECK((1 << Log2OfSize) == w);

												u32 CompressedSize = w * h / 2;

												String CompressedData(CompressedSize, '\0');

												CompressImageDxt1FromRGBA8(Data, (u8*)CompressedData.data(), w, h);

												stbi_image_free(Data);
												TextureDescription Desc;
												Desc.Value = 0; // bc1 format
												Desc.Value |= Log2OfSize << 4; // size
												Desc.Value |= 1 << 8; // mip count
												InsertIntoPak(Pak, StringFromFormat("___Texture_%d", Index), CompressedData, Desc.Value, true);
											}
											else
											{
												String Path(MeshFolder);
												Path.append(1, '/');
												Path.append(TexPath.data, TexPath.length);

												auto It = eastl::find(TextureNames.begin(), TextureNames.end(), StringView(TexPath.data, TexPath.length));
												u64 Index = TextureNames.size();
												if (It == TextureNames.end())
												{
													TextureNames.emplace_back(TexPath.data, TexPath.length);
													MaterialSetTextureType(Material, TextureType, Index + Scene->mNumTextures);
												}
												else
												{
													Index = u32(It - TextureNames.begin());
													MaterialSetTextureType(Material, TextureType, Index + Scene->mNumTextures);
													continue;
												}

												{
													auto Pos = Path.find('\\', 0);
													while (Pos != Path.npos)
													{
														Path.replace(Pos, 1, 1, '/');
														Pos = Path.find('\\', Pos);
													}
												}

												FileMapping File = MapFile(Path);
												RawDataView View = GetView(File);

												if (!View.empty())
												{
													const u8* Data = View.data();
													TextureDescription TexDesc = AdvanceToDataAndGetDescription(Data);
													u64 NumMips = (TexDesc.Value >> 8) & 0xf;
													u64 Log2OfSize = (TexDesc.Value >> 4) & 0xf;
													CHECK(NumMips == Log2OfSize + 1);

													u32 Size = GetTextureSize(TexDesc);

													u32 BytesPerBlock = BitsPerPixel((DXGI_FORMAT)GetTextureFormat(TexDesc)) * 2;

													u32 NumBlocks = std::max<u32>(Size / 4, 1);
													u64 Pitch = NumBlocks * BytesPerBlock;
													Pitch = AlignUp(Pitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

													String WorkingMemory(NumBlocks * Pitch * 3, '\0');

													const u8* Src = Data;
													u64 Offset = 0;
													bool InsertSeparateMip = true;
													u64 PackedMipsBytes = 0;

													u8* Dest = (u8*)WorkingMemory.data();
													for (int Mip = 0; Mip < NumMips; ++Mip)
													{
														NumBlocks = std::max<u32>(Size / 4, 1);
														Pitch = NumBlocks * BytesPerBlock;

														if (InsertSeparateMip && NumBlocks * Pitch < 64_kb)
														{
															InsertSeparateMip = false;
															Dest = (u8*)WorkingMemory.data();
														}

														if (Pitch >= D3D12_TEXTURE_DATA_PITCH_ALIGNMENT)
														{
															if (InsertSeparateMip)
															{
																InsertIntoPak(
																	Pak,
																	StringFromFormat("___Texture_%d_%d", Index, Mip),
																	RawDataView(Src, NumBlocks * Pitch),
																	TexDesc.Value,
																	true
																);
															}
															else
															{
																memcpy(Dest, Src, NumBlocks * Pitch);
																Dest += NumBlocks * Pitch;
																PackedMipsBytes += NumBlocks * Pitch;
															}

															Size /= 2;
															Src += NumBlocks * Pitch;
															Offset += NumBlocks * Pitch;

															continue;
														}

														Pitch = AlignUp(Pitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

														for (int Line = 0; Line < NumBlocks; Line++)
														{
															memcpy(Dest, Src, NumBlocks * BytesPerBlock);
															Src += NumBlocks * BytesPerBlock;
															Dest += Pitch;
														}

														if (InsertSeparateMip)
														{
															InsertIntoPak(
																Pak,
																StringFromFormat("___Texture_%d_%d", Index, Mip),
																RawDataView((u8*)WorkingMemory.data(), Dest - (u8*)WorkingMemory.data()),
																TexDesc.Value,
																true
															);
															if (Pitch * NumBlocks < 64_kb)
															{
																InsertSeparateMip = false;
																Dest = (u8 *)WorkingMemory.data();
															}
														}
														else
														{
															PackedMipsBytes += Pitch * NumBlocks;
														}

														Offset += Pitch * NumBlocks;
														if (Pitch * NumBlocks < D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT && Mip != NumMips - 1)
														{
															Dest += D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - Pitch * NumBlocks;
															Offset += Pitch;
														}
														Size /= 2;
													}

													CHECK(Size == 0);

													InsertIntoPak(
														Pak,
														StringFromFormat("___Texture_%d", Index),
														RawDataView((u8*)WorkingMemory.data(), Dest - (u8*)WorkingMemory.data()),
														TexDesc.Value,
														true
													);
													UnmapFile(File);
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
							InsertIntoPak(Pak, "___Materials", ContainerToView(Materials));
						}

						{
							ZoneScopedN("Upload mesh data");

							MeshBufferOffsets RunningOffset{ 0,0 };
							TArray<MeshBufferOffsets> BufferOffsets;
							TArray<MeshDescription> MeshDatas;

							for (u64 i = 0; i < Scene->mNumMeshes; ++i)
							{
								aiMesh* Mesh = Scene->mMeshes[i];
								MeshDescription& Description = MeshDatas.push_back();
								Description = ExtractMeshDescription(Mesh);

								BufferOffsets.push_back() = RunningOffset;
								RunningOffset.VBufferOffset += GetVertexBufferSize(Description);
								RunningOffset.IBufferOffset += GetIndexBufferSize(Description);

								RunningOffset.IBufferOffset = AlignUp<u32>(RunningOffset.IBufferOffset, 4);
							}
							InsertIntoPak(Pak, "___Scene_BufferOffsets", ContainerToView(BufferOffsets));

							String GlobalVBuffer(RunningOffset.VBufferOffset, '\0');
							String GlobalIBuffer(RunningOffset.IBufferOffset, '\0');
							eastl::bitset<256> CombinationsPresent;
							for (u64 i = 0; i < Scene->mNumMeshes; ++i)
							{
								aiMesh* Mesh = Scene->mMeshes[i];

								MeshDescription& Description = MeshDatas[i];
								CombinationsPresent.set(Description.Flags, 1);

								UploadMeshData ((u8*)GlobalVBuffer.data() + BufferOffsets[i].VBufferOffset, Mesh, Description.Flags & MeshFlags::PositionPacked);
								UploadIndexData((u8*)GlobalIBuffer.data() + BufferOffsets[i].IBufferOffset, Mesh);
							}
							InsertIntoPak(Pak, "___Scene_MeshDatas", ContainerToView(MeshDatas));

							InsertIntoPak(Pak, "___Scene_Vertices", GlobalVBuffer, 0, true);
							InsertIntoPak(Pak, "___Scene_Indeces", GlobalIBuffer, 0, true);

							RawDataView TmpMemory((const u8*)CombinationsPresent.data(), CombinationsPresent.size() / 8);
							InsertIntoPak(Pak, "___VertexCombinationsMask", TmpMemory);
						}

						FinalizePak(Pak);
						Importer.FreeScene();
					});
				}
			}
		}
	}

	if (Args.Empty() || Args.Includes("compile_shaders"))
	{
		ZoneScopedN("compile_shaders kickoff");
		PakFileWriter ShadersPak = CreatePak("./cooked/shaders.pak");
		for (const auto& DirEntry : recursive_directory_iterator("./content/shaders"))
		{
			if (!DirEntry.is_directory() && DirEntry.path().extension().compare(".hlsl") == 0)
			{
				String FilePath = String(DirEntry.path().string().c_str());

				{
					auto Pos = FilePath.find('\\', 0);
					while (Pos != FilePath.npos)
					{
						FilePath.replace(Pos, 1, 1, '/');
						Pos = FilePath.find('\\', Pos);
					}
				}

				TComPtr<IDxcBlob> CS = CompileShader(FilePath, "MainCS");
				TComPtr<IDxcBlob> VS = CompileShader(FilePath, "MainVS");
				TComPtr<IDxcBlob> PS = CompileShader(FilePath, "MainPS");
				if (CS)
				{
					TComPtr<ID3D12ShaderReflection> Reflection = GetReflection(CS);
					UINT TGSx, TGSy, TGSz;
					Reflection->GetThreadGroupSize(&TGSx, &TGSy, &TGSz);

					D3D12_SHADER_DESC Desc;
					Reflection->GetDesc(&Desc);
					for (UINT i = 0; i < Desc.ConstantBuffers; ++i)
					{
						ID3D12ShaderReflectionConstantBuffer* ConstBuffer = Reflection->GetConstantBufferByIndex(i);

						D3D12_SHADER_BUFFER_DESC BufferDesc;
						ConstBuffer->GetDesc(&BufferDesc);
					}
					for (UINT i = 0; i < Desc.BoundResources; ++i)
					{
						D3D12_SHADER_INPUT_BIND_DESC BindDesc;
						Reflection->GetResourceBindingDesc(i, &BindDesc);
					}
				}

				StringView Name = FilePath;
				Name.remove_prefix(Name.find_last_of("\\/") + 1);
				Name.remove_suffix(5); // ".hlsl"
				CHECK(CS || VS || PS);
				CHECK(!VS == !PS);

				u64 BufferSize = 0;
				if (CS) BufferSize += CS->GetBufferSize();
				if (VS) BufferSize += VS->GetBufferSize();
				if (PS) BufferSize += PS->GetBufferSize();

				String Data(BufferSize, 0);

				if (VS) CHECK(VS->GetBufferSize() <= INT16_MAX);
				if (PS) CHECK(PS->GetBufferSize() <= UINT16_MAX);

				u32 PrivateFlags = CS ? 1 << 31 : 0;

				if (VS) PrivateFlags |= (VS->GetBufferSize() << 16) | PS->GetBufferSize();

				u8* Ptr = (u8*)Data.data();
				if (CS)
				{
					memcpy(Ptr, CS->GetBufferPointer(), CS->GetBufferSize());
					Ptr += CS->GetBufferSize();
				}
				if (VS)
				{
					memcpy(Ptr, VS->GetBufferPointer(), VS->GetBufferSize());
					Ptr += VS->GetBufferSize();
				}
				if (PS)
				{
					memcpy(Ptr, PS->GetBufferPointer(), PS->GetBufferSize());
					Ptr += PS->GetBufferSize();
				}

				//CHECK(Ptr == (u8*)Data.end());

				InsertIntoPak(ShadersPak, Name, Data, PrivateFlags);
			}
		}
		FinalizePak(ShadersPak);
	}

	for (TicketCPU T : Tickets)
	{
		WaitForCompletion(T);
	}

	StopWorkerThreads();
}
