#pragma once
#include "Containers/String.h"
#include "Common.h"

struct TextureDescription
{
	int Slot;
	String Path;
};

struct MaterialDescription
{
	String Name;
	TArray<TextureDescription> Textures;
};
