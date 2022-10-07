#pragma once
#include "Common.h"
#include "Containers/Array.h"
#include "Containers/String.h"

struct VertexStreamDescription
{
	String SemanticName;
	u8     SemanticIndex;
	u8     Format;
	u8     InputSlot;
	u8     ByteOffset;
};

struct MeshDescription
{
	enum Flags : u8 {
		Indeces16Bit   = 1 << 0,
		PositionPacked = 1 << 1,
	};
	TArray<VertexStreamDescription> VertexDeclaration;
	u32 VertexBufferSize;
	u32 IndexBufferSize;
	u8  VertexSize;
	u8  Flags;
};

MeshDescription ExtractMeshDescription(struct aiMesh* Mesh);

void UploadMeshData(u8* CpuPtr, aiMesh* Mesh, bool PositionPacked);
void UploadIndexData(u8* CpuPtr, aiMesh* Mesh);
