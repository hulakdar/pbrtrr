#pragma once
#include "Common.h"
#include "Util/Math.generated.h"

enum MeshFlags : u8 {
	PositionPacked   = 1 << 0,
	HasNormals       = 1 << 1,
	HasVertexColor   = 1 << 2,
	HasUV0           = 1 << 3,
	GUI              = 1 << 4,
};

struct MeshDescription
{
	Vec3 BoxMin;
	Vec3 BoxMax;

	u32 VertexCount{};
	u32 IndexCount{};
	u32 MaterialIndex{};
	u8 VertexSize{};
	u8 Flags{};
};
