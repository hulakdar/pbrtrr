#pragma once
#include "Containers/Array.generated.h"
#include "Containers/BitSet.generated.h"
#include "Assets/Mesh.generated.h"
#include "Assets/Material.generated.h"

struct Node
{
	Matrix4     Transform;

	u32         MeshIDStart{};

	u16         MeshCount{};
	u16         NumStaticChildren{};
	u16         OffsetBackToParent{};

	LocalBounds Bounds;
};

struct Camera
{
	Vec3  Position;
	float Fov = 90;
	Vec2  Angles;
	float Near = 1;
	float Aspect = 1.6;
	u64   NodeID{};
};

struct MeshBufferOffsets
{
	u32 VBufferOffset;
	u32 IBufferOffset;
};
