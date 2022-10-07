#pragma once
#include "Containers/String.h"
#include "Common.h"

#define COMPRESSED_MAX_SIZE(x) (x + x / 16 + 64 + 3)

using FileData = String;

FileData LoadWholeFile(StringView Path);

u64 Compress(u8* Src, u64 SrcLen, u8* Dst, u64 DstLen);
u64 Decompress(u8* Src, u64 SrcLen, u8* Dst, u64 DstLen);
