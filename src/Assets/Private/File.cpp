
#include "minilzo.h"
#include "Util/Debug.h"
#include "Assets/File.h"
#include <Tracy.hpp>
#include <fstream>

FileData LoadWholeFile(StringView Path)
{
	ZoneScoped;
	std::ifstream infile(Path.data(),std::ios::binary);
	if (!infile.good())
	{
		return {};
	}

	infile.seekg(0, std::ios::end);
	size_t file_size_in_byte = infile.tellg();
	FileData data;
	data.resize(file_size_in_byte);
	infile.seekg(0, std::ios::beg);
	infile.read(&data[0], file_size_in_byte);

	return data;
}

u64 Compress(u8* Src, u64 SrcLen, u8* Dst, u64 DstLen)
{
	void* WorkMem = new u8[LZO1X_1_MEM_COMPRESS];

	u64 OutDstLen = DstLen;
	CHECK(lzo1x_1_compress(Src, SrcLen, Dst, &OutDstLen, WorkMem) == LZO_E_OK, "Comression failed?!");

	delete[] WorkMem;

	CHECK(OutDstLen <= DstLen, "?");
	return OutDstLen;
}

u64 Decompress(u8* Src, u64 SrcLen, u8* Dst, u64 DstLen)
{
	u64 OutDstLen = DstLen;
	lzo1x_decompress_safe(Src, SrcLen, Dst, &OutDstLen, nullptr);

	CHECK(OutDstLen <= DstLen, "?");
	return OutDstLen;
}

