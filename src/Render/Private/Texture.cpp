#include "Render/RenderDebug.h"
#include "Render/Texture.h"
#include "Containers/ComPtr.h"
#include "Containers/String.h"
#include "Containers/Array.h"
#include "Threading/Mutex.h"
#include "Util/Math.h"
#include "Util/Debug.h"

#include <dxgiformat.h>
#include <limits.h>

struct TextureDataInternal
{
	ComPtr<ID3D12Resource> Resource;
	String Name;
};

TracySharedLockable(RWLock, gTextureMutex);
TArray<TextureDataInternal> gTextures = { TextureDataInternal{nullptr,""}};
TArray<bool>                gTexturesValid = { false };

inline TextureDataInternal& GetTexData(TexID Id)
{
	CHECK(gTextures.size()==gTexturesValid.size(), "Arrays out of sync");
	CHECK(Id.Value < gTexturesValid.size() && gTexturesValid[Id.Value], "Texture ID invalid");
	return gTextures[Id.Value];
}

TexID StoreTexture(ID3D12Resource* Resource, const char* Name)
{
	WriteLock AutoLock(gTextureMutex);
	u32 Index = (u32)gTextures.size();
	gTextures.push_back({ Resource, Name });
	gTexturesValid.push_back(true);
	CHECK(gTextures.size()==gTexturesValid.size(), "Arrays out of sync?");

	SetD3DName(Resource, L"%S", Name);
	return TexID{ Index };
}

void FreeTextureResource(TexID Id)
{
	CHECK(gTextures.size()==gTexturesValid.size(), "Arrays out of sync?");

	WriteLock AutoLock(gTextureMutex);
	if (Id.Value < gTexturesValid.size() && gTexturesValid[Id.Value])
	{
		TextureDataInternal& Data = GetTexData(Id);
		Data.Name.resize(0);
		Data.Resource.Reset();
		gTexturesValid[Id.Value] = false;
	}
}

void ReleaseTextures()
{
	gTextures.resize(0);
}

ID3D12Resource* GetTextureResource(TexID Id)
{
	ReadLock AutoLock(gTextureMutex);
	return GetTexData(Id).Resource.Get();
}

const char* GetTextureName(TexID Id)
{
	ReadLock AutoLock(gTextureMutex);
	return GetTexData(Id).Name.c_str();
}

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
