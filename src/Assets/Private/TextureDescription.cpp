#include "Assets/TextureDescription.generated.h"

u8 GetTextureFormat(TextureDescription Desc)
{
	switch (Desc.Value & 0xf)
	{
	case 0: return DXGI_FORMAT_BC1_UNORM;
	case 1: return DXGI_FORMAT_BC2_UNORM;
	case 2: return DXGI_FORMAT_BC3_UNORM;
	case 3: return DXGI_FORMAT_BC4_UNORM;
	case 4: return DXGI_FORMAT_BC5_UNORM;
	}
	return 0;
}

u16 GetTextureSize(TextureDescription Desc)
{
	return 1 << ((Desc.Value >> 4) & 0xf);
}

u16 GetMipCount(TextureDescription Desc)
{
	return (Desc.Value >> 8) & 0xf;
}