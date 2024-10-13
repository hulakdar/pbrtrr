#pragma once
#include "Common.h"

#include "RenderForwardDeclarations.h"
#include "Util/TypeInfo.h"

#include <dxgiformat.h>

struct TexID { u32 Value = 0; };

struct TextureData
{
	TexID ID;
	u16 Width  = UINT16_MAX;
	u16 Height = UINT16_MAX;
	u16 SRV    = UINT16_MAX;
	u16 UAV    = UINT16_MAX;
	u16 RTV    = UINT16_MAX;
	u16 DSV    = UINT16_MAX;
	u8  Format = 0;
	u8  NumMips = 0;
	u8  Flags  = 0;
	u8  SampleCount = 1;
};

struct VirtualTexture
{
	TextureData TexData;
	u16 StreamedTileIds[8]{};
	u16 PackedId = (u16)~0U;

	u16 NumStreamedMips : 3;
	u16 NumStreamedIn : 3;

	u16 NumTilesForPacked : 1;
	u16 StreamingInProgress : 1;
};
