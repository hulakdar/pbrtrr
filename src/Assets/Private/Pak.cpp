#include "Assets/Pak.h"
#include "Assets/File.h"
#include "Util/Debug.h"

#include <minilzo.h>

PakFile CreatePak(const char* FilePath)
{
	PakFile Result;
	Result.File = fopen(FilePath, "wb");
	CHECK(Result.File, "Couldn't open file");

	PakHeader Header{ 0 };
	size_t Written = fwrite(&Header, sizeof(Header), 1, Result.File);
	CHECK(Written == 1, "Failed to write header");

	return Result;
}

void FinalizePak(PakFile& Pak)
{
	PakHeader Header;
	Header.Magic = PAK_MAGIC;
	Header.ItemsOffset = ftell(Pak.File);
	Header.NumberOfItems = Pak.Items.size();
	fwrite(Pak.Items.data(), sizeof(PakItem), Header.NumberOfItems, Pak.File);

	fseek(Pak.File, 0, SEEK_SET);
	fwrite(&Header, sizeof(Header), 1, Pak.File);
	fclose(Pak.File);
	Pak.File = nullptr;
	Pak.Items.clear();
}

void InsertIntoPak(PakFile& Pak, const char* FileName, u8* Data, u64 Size, bool bCompress)
{
	PakItem& Item = Pak.Items.push_back();
	long Position = ftell(Pak.File);
	fwrite(FileName, 1, strlen(FileName) + 1, Pak.File);

	Item.Flags = 0;
	Item.FileName = Position;
	Item.UncompressedDataSize = Size;
	Item.CompressedDataSize = Size;
	Item.DataOffset = ftell(Pak.File);

	u8 *Src = Data;
	u64 DstLen = Size;
	if (bCompress)
	{
		u64 CompressedSize = COMPRESSED_MAX_SIZE(DstLen);
		u8 *Buffer = new u8[CompressedSize];
		DstLen = Compress(Data, Size, Buffer, CompressedSize);
		if (DstLen < Size)
		{
			Src = Buffer;
			Item.Flags |= PakItem::Compressed;
			Item.CompressedDataSize = DstLen;
		}
		else
		{
			DstLen = Size;
			bCompress = false;
			delete[] Buffer;
		}
	}
	size_t Written = fwrite(Src, 1, DstLen, Pak.File);
	CHECK(Written == DstLen, "Failed to write data");
	if (bCompress)
	{
		delete[] Src;
	}
}

