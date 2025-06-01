#include <numeric>
#include <dstorage.h>

#include "Assets/Pak.generated.h"
#include "Render/RenderDX12.generated.h"
#include "Threading/Mutex.generated.h"

#include "Util/Debug.generated.h"
#include "Util/Semaphore.generated.h"
#include "Util/Math.h"
#include "Util/Util.generated.h"

#include <EASTL/sort.h>

#define PAK_MAGIC (*(u64*)"OVENPKV2")

namespace {
	u32 PackName(u32 Offset, u32 Size)
	{
		CHECK(Size < 1024); // 2^10
		CHECK(Offset < 4194304); // 2^22
		return Offset | (Size << 22);
	}

	void UnpackName(u32 NameOffsetAndSize, u32& Offset, u32& Size)
	{
		Offset = NameOffsetAndSize & 0x3FFFFF;
		Size = NameOffsetAndSize >> 22;
	}
}

static IDStorageCompressionCodec* gDirectStorageCompressionCodecs[8];
static Semaphore gDirectStorageSemaphore{8};

u32 PushExtraData(PakFileWriter& Pak, RawDataView Data)
{
	u64 Result = Pak.ExtraData.size();
	CHECK(Result < UINT32_MAX);

	Pak.ExtraData.resize(Pak.ExtraData.size() + Data.size());
	memcpy(&Pak.ExtraData[Result], Data.data(), Data.size());

	return (u32)Result;
}

void InitDirectStorage()
{
	InitDirectStorageFactory();
	for (int i = 0; i < ArrayCount(gDirectStorageCompressionCodecs); ++i)
	{
		DStorageCreateCompressionCodec(DSTORAGE_COMPRESSION_FORMAT_GDEFLATE, 0, IID_PPV_ARGS(&gDirectStorageCompressionCodecs[i]));
	}
}

PakFileWriter CreatePak(StringView FilePath)
{
	PakFileWriter Result;
	Result.File = fopen(FilePath.data(), "wb");

	String FileDSName = String(FilePath) + "ds";
	Result.FileDS = fopen(FileDSName.c_str(), "wb");
	CHECK(Result.File, "Couldn't open file");

	PakHeader Header{ 0 };
	size_t Written = fwrite(&Header, sizeof(Header), 1, Result.File);
	CHECK(Written == 1, "Failed to write header");

	return Result;
}

void InsertIntoPak(PakFileWriter& Pak, StringView FileName, RawDataView Data, u32 PrivateFlags, bool UseDirectStorage, bool bCompress)
{
	ZoneScoped;

	u32 FileNameHash = HashString32(FileName);
	i32 UncompressedDataSize = i32(Data.size());
	auto It = Pak.HashToItem.find(FileNameHash);
	if (It != Pak.HashToItem.end())
	{
		CHECK(Pak.Items[It->second].first == FileNameHash);
		{
			u32 NameOffset, NameSize;
			UnpackName(Pak.Items[It->second].second.FileNameOffsetAndSize, NameOffset, NameSize);
			if (NameSize == FileName.size())
			{
				if(strncmp(Pak.ExtraData.data() + NameOffset, FileName.data(), NameSize) != 0)
				{
					printf("File duplicate: %.*s", NameSize, FileName.data());
					return;
				}
			}
			//if (FileName == Name2)
			//{
				//Debug::Print("File duplicate: ", FileName.data());
				//return;
			//}
		}
	}
	else
	{
		Pak.HashToItem.emplace(FileNameHash, Pak.Items.size());
	}

	auto NewItemPair = Pak.Items.push_back();
	PakItem& Item = NewItemPair.second;
	NewItemPair.first = FileNameHash;
	u32 ExtraDataOffset = PushExtraData(Pak, RawDataView((const u8*)FileName.data(), FileName.size()));
	Item.FileNameOffsetAndSize = PackName(ExtraDataOffset, u32(FileName.size()));
	if (UseDirectStorage)
	{
		Item.UncompressedDataSize = -i32(UncompressedDataSize);
	}
	else
	{
		Item.UncompressedDataSize = i32(UncompressedDataSize);
	}
	Item.CompressedDataSize = 0;
	Item.PrivateFlags = PrivateFlags;
	if (UseDirectStorage)
	{
		Item.DataOffset = ftell(Pak.FileDS);
	}
	else
	{ 
		Item.DataOffset = ftell(Pak.File);
	}

	const u8 *Src = (const u8*)Data.data();
	size_t DstLen = Data.size();
	if (bCompress)
	{
		auto* Codec = gDirectStorageCompressionCodecs[gDirectStorageSemaphore.Aquire()];
		u64 CompressedSize = Codec->CompressBufferBound(UncompressedDataSize);
		u8 *Buffer = new u8[CompressedSize];

		Codec->CompressBuffer(
			(const void*) Data.data(),
			Data.size(),
			DSTORAGE_COMPRESSION_BEST_RATIO,
			Buffer,
			CompressedSize,
			&DstLen
		);
		gDirectStorageSemaphore.Release();

		if (DstLen < Data.size())
		{
			Src = Buffer;
			Item.CompressedDataSize = u32(DstLen);
		}
		else
		{
			//Debug::Print("Bad for compression : ", FileName.data(), " Size:", Data.size());
			DstLen = Data.size();
			bCompress = false;
			delete[] Buffer;
		}
	}
	if (UseDirectStorage)
	{
		size_t Written = fwrite(Src, 1, DstLen, Pak.FileDS);
		CHECK(Written == DstLen, "Failed to write data to direct storage file");
	}
	else
	{
		size_t Written = fwrite(Src, 1, DstLen, Pak.File);
		CHECK(Written == DstLen, "Failed to write data to base file");
	}
	if (bCompress)
	{
		delete[] Src;
	}
}

void FinalizePak(PakFileWriter& Pak)
{
	PakHeader Header;
	Header.Magic = PAK_MAGIC;

	eastl::sort(
		Pak.Items.begin(),
		Pak.Items.end(),
		[](const std::pair<u64, PakItem>& a, const std::pair<u64, PakItem>& b) {
			return a.first < b.first;
		});

	CHECK(eastl::is_sorted(Pak.Items.begin(), Pak.Items.end(), [](const std::pair<u64, PakItem>& a, const std::pair<u64, PakItem>& b) { return a.first < b.first; }));

	for (int i = 0; i < Pak.Items.size() - 1; ++i)
	{
		if (Pak.Items[i].first == Pak.Items[i + 1].first)
		{
			u32 Offset1, Size1, Offset2, Size2;
			UnpackName(Pak.Items[i].second.FileNameOffsetAndSize, Offset1, Size1);
			UnpackName(Pak.Items[i + 1].second.FileNameOffsetAndSize, Offset2, Size2);
			CHECK(Size1 == Size2 && memcmp(&Pak.ExtraData[Offset1], &Pak.ExtraData[Offset2], Size1) == 0);
		}
	}

	while (Pak.Items.size() % 16 != 0)
	{
		Pak.Items.push_back() = std::pair<u32,PakItem>(0, {0});
	}

	Header.NumberOfItems = Pak.Items.size();
	u64 HashesOld = ftell(Pak.File);
	u64 HashesAligned = AlignUp(HashesOld, 32);
	u8 Zeros[32] = {0};

	Header.HashesOffset = HashesAligned;
	fwrite(Zeros, 1, HashesAligned - HashesOld, Pak.File);

	CHECK(ftell(Pak.File) % 32 == 0);

	fwrite(Pak.Items.V1.data(), sizeof(u32), Header.NumberOfItems, Pak.File);

	Header.ItemsOffset = ftell(Pak.File);
	fwrite(Pak.Items.V2.data(), sizeof(PakItem), Header.NumberOfItems, Pak.File);

	Header.ExtraDataOffset = ftell(Pak.File);

	fwrite(Pak.ExtraData.data(), 1, Pak.ExtraData.size(), Pak.File);

	fseek(Pak.File, 0, SEEK_SET);
	fwrite(&Header, sizeof(Header), 1, Pak.File);
	fclose(Pak.File);
	fclose(Pak.FileDS);
	Pak.File = nullptr;
	Pak.Items.clear();
	Pak.ExtraData.clear();
}

PakHeader* GetHeader(const PakFileReader& Pak)
{
	return (PakHeader*)Pak.Mapping.BasePtr;
}

PakFileReader OpenPak(StringView FilePath)
{
	PakFileReader Result;
	Result.Mapping = MapFile(FilePath);

	String DSPath = String(FilePath) + "ds";
	Result.FileDS = CreateDSFile(DSPath);

	PakHeader* Header = GetHeader(Result);
	CHECK(Header->Magic == PAK_MAGIC, "Wrong file?");

	return Result;
}

TArrayView<u32> GetItemHashes(const PakFileReader& Pak)
{
	PakHeader* Header = GetHeader(Pak);

	u32* Ptr = (u32*)((uintptr_t)Pak.Mapping.BasePtr + Header->HashesOffset);
	return TArrayView<u32>(Ptr, Header->NumberOfItems);
}

TArrayView<PakItem> GetItems(const PakFileReader& Pak)
{
	PakHeader* Header = GetHeader(Pak);
	
	PakItem* Ptr = (PakItem*)((uintptr_t)Pak.Mapping.BasePtr + Header->ItemsOffset);
	return TArrayView<PakItem>(Ptr, Header->NumberOfItems);
}

StringView GetFileName(const PakFileReader& Pak, const PakItem& Item)
{
	PakHeader* Header = GetHeader(Pak);
	u32 NameOffset, NameSize;
	UnpackName(Item.FileNameOffsetAndSize, NameOffset, NameSize);
	return StringView((char*)Pak.Mapping.BasePtr + Header->ExtraDataOffset + NameOffset, NameSize);
}

TracyLockable(Mutex, k);
const PakItem* FindItem(const PakFileReader& Pak, StringView FileName)
{
	ScopedLock kek(k);

	auto Hashes = GetItemHashes(Pak);
	CHECK(Hashes.size());

	u32 FileNameHash = HashString32(FileName);

	CHECK(uintptr_t(Hashes.data()) % 32 == 0);
	__m256i Comparator = _mm256_set1_epi32(FileNameHash);

	u32 Start = 0, End = (u32)(Hashes.size() - 1);
	while (Start <= End)
	{
		u32 Index = (Start + End) / 2;
		Index = AlignDown(Index, 16u);

		__m256i HashLine1 = _mm256_load_si256((const __m256i*)&Hashes[Index]);
		__m256i HashLine2 = _mm256_load_si256((const __m256i*)&Hashes[Index + 8]);
		__m256i ComparisonResult1 = _mm256_cmpeq_epi32(HashLine1, Comparator);
		__m256i ComparisonResult2 = _mm256_cmpeq_epi32(HashLine2, Comparator);
		u32 Mask1 = _mm256_movemask_epi8(ComparisonResult1);
		u32 Mask2 = _mm256_movemask_epi8(ComparisonResult2);

		u64 Mask = u64(Mask1) | (u64(Mask2) << 32);

		if (Mask != 0)
		{
			CHECK(__popcnt64(Mask) == 4);
			u32 BitIndex = _tzcnt_u64(Mask);
			Index += BitIndex / 4;
			const PakItem* Result = &(GetItems(Pak))[Index];
			StringView Found = GetFileName(Pak, *Result);
			CHECK(Found.size() == FileName.size() && strncmp(Found.data(), FileName.data(), Found.size()) == 0);
			return Result;
		}

		if (Hashes[Index] < FileNameHash)
		{
			Start = Index + 16;
		}
		else
		{
			End = Index - 1;
		}
	}
	
	return nullptr;
}

void FillBuffer(const PakFileReader& Pak, const PakItem& Item, void* Address)
{
	auto* Data = (const u8*)Pak.Mapping.BasePtr + Item.DataOffset;
	if (Item.CompressedDataSize != 0)
	{
		size_t UncompressedOut;
		gDirectStorageCompressionCodecs[gDirectStorageSemaphore.Aquire()]->DecompressBuffer(Data, Item.CompressedDataSize, Address, Item.UncompressedDataSize, &UncompressedOut);
		gDirectStorageSemaphore.Release();
		CHECK(UncompressedOut == Item.UncompressedDataSize);
		//Decompress(Data, Item.CompressedDataSize, (u8*)Address, Item.UncompressedDataSize);
	}
	else
	{
		memcpy(Address, Data, Item.UncompressedDataSize);
	}
}

String GetFileData(const PakFileReader& Pak, const PakItem& Item)
{
	String Result;
	Result.resize(Item.UncompressedDataSize);

	FillBuffer(Pak, Item, Result.data());
	return Result;
}

void ClosePak(PakFileReader& Pak)
{
	UnmapFile(Pak.Mapping);
	Pak.FileDS->Release();
	Pak.FileDS = nullptr;
}
