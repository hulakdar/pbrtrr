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
	CHECK(Id.Value == 0 || (Id.Value < gTexturesValid.size() && gTexturesValid[Id.Value]), "Texture ID invalid");
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

