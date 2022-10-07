#include "Assets/DDS.h"
#include <dxgiformat.h>

DXGI_FORMAT FormatFromFourCC(u32 FourCC)
{
	if (FourCC == MAGIC(DXT1))
	{
		return DXGI_FORMAT_BC1_UNORM;
	}
	else if (FourCC == MAGIC(DXT3))
	{
		return DXGI_FORMAT_BC2_UNORM;
	}
	else if (FourCC == MAGIC(DXT5))
	{
		return DXGI_FORMAT_BC3_UNORM;
	}
	else if (FourCC == MAGIC(BC4U))
	{
		return DXGI_FORMAT_BC4_UNORM;
	}
	else if (FourCC == MAGIC(BC4S))
	{
		return DXGI_FORMAT_BC4_SNORM;
	}
	else if (FourCC == MAGIC(ATI2))
	{
		return DXGI_FORMAT_BC5_UNORM;
	}
	else if (FourCC == MAGIC(BC5S))
	{
		return DXGI_FORMAT_BC5_SNORM;
	}
	else if (FourCC == MAGIC(RGBG))
	{
		return DXGI_FORMAT_R8G8_B8G8_UNORM;
	}
	else if (FourCC == MAGIC(GRGB))
	{
		return DXGI_FORMAT_G8R8_G8B8_UNORM;
	}
	switch (FourCC)
	{
	case 36:
		return DXGI_FORMAT_R16G16B16A16_UNORM;
	case 110:
		return DXGI_FORMAT_R16G16B16A16_SNORM;
	case 111:
		return DXGI_FORMAT_R16_FLOAT;
	case 112:
		return DXGI_FORMAT_R16G16_FLOAT;
	case 113:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case 114:
		return DXGI_FORMAT_R32_FLOAT;
	case 115:
		return DXGI_FORMAT_R32G32_FLOAT;
	case 116:
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}
