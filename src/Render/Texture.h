#pragma once

// from docs.microsoft.com
#define DDS_MAGIC 0x20534444
#define DDPF_ALPHAPIXELS 0x1
#define DDPF_ALPHA       0x2
#define DDPF_FOURCC      0x4
#define DDPF_RGB         0x40
#define DDPF_YUV         0x200
#define DDPF_LUMINANCE   0x20000

struct DDS_PIXELFORMAT
{
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwFourCC;
  DWORD dwRGBBitCount;
  DWORD dwRBitMask;
  DWORD dwGBitMask;
  DWORD dwBBitMask;
  DWORD dwABitMask;
};

struct DDS_HEADER
{
  DWORD           dwSize;
  DWORD           dwFlags;
  DWORD           dwHeight;
  DWORD           dwWidth;
  DWORD           dwPitchOrLinearSize;
  DWORD           dwDepth;
  DWORD           dwMipMapCount;
  DWORD           dwReserved1[11];
  DDS_PIXELFORMAT ddspf;
  DWORD           dwCaps;
  DWORD           dwCaps2;
  DWORD           dwCaps3;
  DWORD           dwCaps4;
  DWORD           dwReserved2;
};

enum D3D10_RESOURCE_DIMENSION {
  D3D10_RESOURCE_DIMENSION_UNKNOWN,
  D3D10_RESOURCE_DIMENSION_BUFFER,
  D3D10_RESOURCE_DIMENSION_TEXTURE1D,
  D3D10_RESOURCE_DIMENSION_TEXTURE2D,
  D3D10_RESOURCE_DIMENSION_TEXTURE3D
};

struct DDS_HEADER_DXT10
{
  DXGI_FORMAT              dxgiFormat;
  D3D10_RESOURCE_DIMENSION resourceDimension;
  UINT                     miscFlag;
  UINT                     arraySize;
  UINT                     miscFlags2;
};

struct TextureData
{
	ComPtr<ID3D12Resource>	Resource;

	String		Name = {};
	IVector2	Size = {};
	DXGI_FORMAT	Format = DXGI_FORMAT_UNKNOWN;
	UINT		SRVIndex = UINT_MAX;
	UINT		UAVIndex = UINT_MAX;
	UINT		RTVIndex = UINT_MAX;
	UINT		DSVIndex = UINT_MAX;
	uint8_t*	RawData = nullptr;
};

