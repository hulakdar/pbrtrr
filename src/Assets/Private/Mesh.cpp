#include "Assets/Mesh.h"
#include <assimp/mesh.h>
#include <Util/Debug.h>
#include <Util/Math.h>
#include <Util/Util.h>

bool CanPackPosition(aiMesh* Mesh)
{
	return true;
}

MeshDescription ExtractMeshDescription(aiMesh* Mesh)
{
	bool bPositionPacked = CanPackPosition(Mesh);

	MeshDescription Result;

	CHECK(Mesh->HasFaces(), "Mesh without faces?");

	u8 VertexSize = 0;
	if (bPositionPacked)
	{
		Result.VertexDeclaration.push_back(VertexStreamDescription{ "POSITION", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, VertexSize });
		VertexSize += sizeof(Vec4PackUnorm);
	}
	else
	{
		Result.VertexDeclaration.push_back(VertexStreamDescription{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, VertexSize });
		VertexSize += sizeof(aiVector3D);
	}

	if (Mesh->HasNormals())
	{
		Result.VertexDeclaration.push_back(VertexStreamDescription{ "NORMAL", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, VertexSize });
		VertexSize += sizeof(Vec4h);
	}

	u8 VertexColors = 0;
	while (Mesh->HasVertexColors(VertexColors))
	{
		Result.VertexDeclaration.push_back(VertexStreamDescription{ "COLOR", VertexColors, DXGI_FORMAT_R8G8B8A8_UNORM, 0, VertexSize });
		VertexSize += 4;
		VertexColors++;
	}

	u8 UVSets = 0;
	while (Mesh->HasTextureCoords(UVSets))
	{
		Result.VertexDeclaration.push_back(VertexStreamDescription{ "TEXCOORD", UVSets, DXGI_FORMAT_R16G16_FLOAT, 0, VertexSize });
		VertexSize += sizeof(half) * 2;
		UVSets++;
	}
	if (UVSets == 0) // fake uv binding
	{
		Result.VertexDeclaration.push_back(VertexStreamDescription{ "TEXCOORD", 0, DXGI_FORMAT_R8_UNORM, 0, 0 });
	}

	UINT VertexBufferSize = VertexSize * Mesh->mNumVertices;

	bool b16BitIndeces = Mesh->mNumVertices <= 65535;
	UINT IndexBufferSize = Mesh->mNumFaces * 3 * (b16BitIndeces ? sizeof(uint16_t) : sizeof(uint32_t));

	CHECK(VertexBufferSize != 0, "??");
	CHECK(IndexBufferSize != 0, "??");

	Result.VertexBufferSize = VertexBufferSize;
	Result.IndexBufferSize = IndexBufferSize;
	Result.VertexSize = VertexSize;
	Result.Flags  = b16BitIndeces   ? MeshDescription::Indeces16Bit   : 0;
	Result.Flags |= bPositionPacked ? MeshDescription::PositionPacked : 0;

	return Result;
}

void UploadIndexData(u8* CpuPtr, aiMesh* Mesh)
{
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

void UploadMeshData(u8* CpuPtr, aiMesh* Mesh, bool PositionPacked)
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
			Vec3 Extent = Max - Min;
			if (Extent.x < 0.001f) Extent.x = 1;
			if (Extent.y < 0.001f) Extent.y = 1;
			if (Extent.z < 0.001f) Extent.z = 1;

			Vec4 Normalized{
				(Mesh->mVertices[i].x - Min.x) / (Extent.x),
				(Mesh->mVertices[i].y - Min.y) / (Extent.y),
				(Mesh->mVertices[i].z - Min.z) / (Extent.z),
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
}
