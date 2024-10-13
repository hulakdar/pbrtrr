#pragma once
#include "Common.h"

enum MaterialFlags : u32
{
	TwoSided = 1 << 0,
	PBR = 1 << 1,
	MetallicMap = 1 << 2,
	RoughnessMap = 1 << 3,
	EmissiveMap = 1 << 4,
	AOMap = 1 << 5,
};

struct MaterialDescription
{
	Color4 DiffuseColorAndOpacity{ 0 };
	Color4 ColorEmissive{ 0 };

	float EmissiveIntensity{ 0 };
	u32   Flags{ 0 };

	u16 DiffuseTexture{ (u16) - 1};
	u16 SpecularTexture {(u16) - 1};
	u16 EmissiveTexture{ (u16) - 1};
	u16 MetalicTexture{ (u16) -1 };
	u16 RoughnessTexture{ (u16) -1 };
	u16 NormalTexture{ (u16) -1 };
	u16 OpacityTexture{ (u16) -1 };
	u8  MetallicFactor{ 0 };
	u8  RoughnessFactor{ 0 };
};

