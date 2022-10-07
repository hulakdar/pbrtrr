#pragma once
#include "Common.h"
#include "Containers/Array.h"

#include <stdio.h>

#define PAK_MAGIC (*(u64*)"PBRTRRPK")

struct PakHeader
{
	u64 Magic;
	u64 ItemsOffset;
	u64 NumberOfItems;
};

struct PakItem
{
	enum Flags
	{
		Compressed = 1 << 0,
	};

	u64 FileName;
	u64 DataOffset;
	u64 UncompressedDataSize;
	u64 CompressedDataSize;
	u64 Flags;
};

struct PakFile
{
	FILE* File;
	TArray<PakItem> Items;
};

PakFile CreatePak(const char* FilePath);
void FinalizePak(PakFile& Pak);

void InsertIntoPak(PakFile& Pak, const char* FileName, u8* Data, u64 Size, bool bCompress);
