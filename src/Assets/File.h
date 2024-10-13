#pragma once

#include <EASTL/string.h>

#include "Containers/StringView.h"
#include "Common.h"

#define COMPRESSED_MAX_SIZE(x) (x + x / 16 + 64 + 3)


template<typename T>
RawDataView ContainerToView(const T& Container)
{
	u64 Size = Container.size() * sizeof(*Container.begin());
	return RawDataView((const u8*)Container.data(), Size);
}

struct FileMapping
{
	void* File;
	void* Mapping;
	void* BasePtr;
	u64   FileSize;
};
