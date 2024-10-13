#include "Assets/Mesh.generated.h"

u32 GetVertexBufferSize(const MeshDescription& Description)
{
	return Description.VertexCount * (u32)Description.VertexSize;
}

u32 GetIndexBufferSize(const MeshDescription& Description)
{
	return Description.IndexCount * (Description.VertexCount < UINT16_MAX ? 2 : 4);
}

