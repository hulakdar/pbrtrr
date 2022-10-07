#pragma once
#include "Common.h"

struct TexID { u32 Value = 0; };

struct TextureData
{
	TexID ID;
	u16 Width  = MAXWORD;
	u16 Height = MAXWORD;
	u16 SRV    = MAXWORD;
	u16 UAV    = MAXWORD;
	u16 RTV    = MAXWORD;
	u16 DSV    = MAXWORD;
	u8  Format = 0;
	u8  Flags  = 0;
};

struct ID3D12Resource;

TexID           StoreTexture(ID3D12Resource* Resource, const char* Name = "");
ID3D12Resource* GetTextureResource(TexID Id);
void            FreeTextureResource(TexID Id);
const char*     GetTextureName(TexID Id);
void            ReleaseTextures();

