#pragma once

#include "Common.h"
#include "Containers/Array.h"
#include "Containers/ParallelArray.h"
#include "Containers/ArrayView.h"
#include "Containers/String.h"
#include "Containers/Map.h"
#include "Assets/File.h"
#include "Render/RenderForwardDeclarations.h"

#include <stdio.h>

/*
	PAK FILE
	
	PakHeader
	-- Magic
	-- Items offset in bytes (from start of file)
	Data (N number of files, sometimes compressed)
	Extra data (small and uncompressed, like strings etc.)
	Hashes
	Items. Data description (N number of items)
	-- Pointer to file name
	-- Pointer to file data
	-- Uncompressed data size in bytes
	-- Compressed data size in bytes
	-- Flags
*/

struct PakHeader
{
	u64 Magic;
	u64 HashesOffset;
	u64 ItemsOffset;
	u64 NumberOfItems;
	u64 ExtraDataOffset;
};

struct PakItem
{
	u64 DataOffset;
	i32 UncompressedDataSize;
	u32 CompressedDataSize;
	u32 PrivateFlags;
	u32 FileNameOffsetAndSize;
};

struct PakFileWriter
{
	FILE* File;
	FILE* FileDS;
	String         ExtraData;
	TMap<u32,u64>   HashToItem;
	TParallelArray<u32, PakItem> Items;
};

struct PakFileReader
{
	FileMapping Mapping;
	IDStorageFile* FileDS;
};

void          InsertIntoPak(PakFileWriter& Pak, StringView FileName, RawDataView Data, u32 PrivateFlags = 0x0, bool UseDirectStorage = false, bool bCompress = true);

template<typename T>
T GetFileDataTyped(const PakFileReader& Pak, const PakItem& Item)
{
	T Result;
	CHECK(Item.UncompressedDataSize == sizeof(T));

	FillBuffer(Pak, Item, &Result);
	return Result;
}


template<typename T>
TArray<T> GetFileDataTypedArray(const PakFileReader& Pak, const PakItem& Item)
{
	TArray<T> Result;
	CHECK(Item.UncompressedDataSize % sizeof(T) == 0);
	Result.resize(Item.UncompressedDataSize / sizeof(T));

	FillBuffer(Pak, Item, Result.data());
	return Result;
}
