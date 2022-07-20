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
enum DXGI_FORMAT;

TexID           StoreTexture(ID3D12Resource* Resource, const char* Name = "");
ID3D12Resource* GetTextureResource(TexID Id);
void            FreeTextureResource(TexID Id);
const char*     GetTextureName(TexID Id);
void            ReleaseTextures();

// from docs.microsoft.com
#define DDS_MAGIC 0x20534444
#define DDPF_ALPHAPIXELS 0x1
#define DDPF_ALPHA       0x2
#define DDPF_FOURCC      0x4
#define DDPF_RGB         0x40
#define DDPF_YUV         0x200
#define DDPF_LUMINANCE   0x20000

#define MAGIC(x) (*(uint32_t*)(#x))

struct DDS_PIXELFORMAT
{
  u32 dwSize;
  u32 dwFlags;
  u32 dwFourCC;
  u32 dwRGBBitCount;
  u32 dwRBitMask;
  u32 dwGBitMask;
  u32 dwBBitMask;
  u32 dwABitMask;
};

struct DDS_HEADER
{
  u32          dwSize;
  u32          dwFlags;
  u32          dwHeight;
  u32          dwWidth;
  u32          dwPitchOrLinearSize;
  u32          dwDepth;
  u32          dwMipMapCount;
  u32          dwReserved1[11];
  DDS_PIXELFORMAT ddspf;
  u32          dwCaps;
  u32          dwCaps2;
  u32          dwCaps3;
  u32          dwCaps4;
  u32          dwReserved2;
};

enum DDS_RESOURCE_DIMENSION {
  D3D10_RESOURCE_DIMENSION_UNKNOWN,
  D3D10_RESOURCE_DIMENSION_BUFFER,
  D3D10_RESOURCE_DIMENSION_TEXTURE1D,
  D3D10_RESOURCE_DIMENSION_TEXTURE2D,
  D3D10_RESOURCE_DIMENSION_TEXTURE3D
};

struct DDS_HEADER_DXT10
{
  DXGI_FORMAT            dxgiFormat;
  DDS_RESOURCE_DIMENSION resourceDimension;
  u32                 miscFlag;
  u32                 arraySize;
  u32                 miscFlags2;
};

DXGI_FORMAT FormatFromFourCC(u32 FourCC);

