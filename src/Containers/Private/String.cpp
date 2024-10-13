#include "../String.h"
#include "../StringView.h"

#include <corecrt_wstdio.h>
#include <Util/Math.h>
//#include <EASTL/functional.h>
#include <stb/stb_sprintf.h>

#include <tracy/Tracy.hpp>
#include <nmmintrin.h>

WString StringFromFormat(const wchar_t* Format, ...)
{
	va_list ArgList;

	va_start(ArgList, Format);
	int CharLen = _vsnwprintf(nullptr, 0, Format, ArgList);
	va_end(ArgList);

	WString Result(CharLen, '\0');

	va_start(ArgList, Format);
	_vsnwprintf(Result.data(), (size_t)Result.length(), Format, ArgList);
	va_end(ArgList);
	return WString();
}

u32 HashString32(const char* In, u64 Size)
{
	ZoneScoped;

	//ZoneNameF("Hash32 '%.*s'", (int)Size, In);

	u32 Result = ~0U;
	while (Size >= 8)
	{
		Result = _mm_crc32_u64(Result, *(u64*)In);
		In += 8;
		Size -=8;
	}
	while (Size >= 4)
	{
		Result = _mm_crc32_u32(Result, *(u32*)In);
		In += 4;
		Size -= 4;
	}
	while (Size >= 2)
	{
		Result = _mm_crc32_u16(Result, *(u16*)In);
		In += 2;
		Size -= 2;
	}
	while (Size >= 1)
	{
		Result = _mm_crc32_u8(Result, *In);
		In += 1;
		Size -= 1;
	}
	return Result;
}

u32 HashString32(StringView In)
{
	return HashString32(In.data(), In.size());
}

String StringFromFormat(const char* Format, ...)
{
	String FormatPadded(Format);
	FormatPadded.append(4,'\0');

	va_list ArgList;

	va_start(ArgList, Format);
	int CharLen = stbsp_vsnprintf(nullptr, 0, FormatPadded.c_str(), ArgList);
	va_end(ArgList);

	String Result(CharLen, ' ');

	va_start(ArgList, Format);
	stbsp_vsnprintf(Result.data(), (int)CharLen + 1, FormatPadded.c_str(), ArgList);
	va_end(ArgList);

	return Result;
}

WString ToWide(const String& Narrow)
{
	WString Result(Narrow.size(), L'\0');
	mbstowcs( Result.data(), Narrow.c_str(), Result.size());
	return Result;
}

bool EndsWith(StringView Word, StringView Suffix)
{
	if (Word.length() < Suffix.length())
	{
		return false;
	}

	for (size_t i = Word.length() - Suffix.length(), j = 0; i < Word.length();)
	{
		if (Word[i++] != Suffix[j++])
		{
			return false;
		}
	}
	return true;
}
