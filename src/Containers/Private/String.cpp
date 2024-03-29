#include "../String.h"
#include "external/stb/stb_sprintf.h"

#include <corecrt_wstdio.h>

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

String StringFromFormat(const char* Format, ...)
{
	va_list ArgList;

	va_start(ArgList, Format);
	int CharLen = stbsp_vsnprintf(nullptr, 0, Format, ArgList);
	va_end(ArgList);

	String Result(CharLen + 1, '\0');

	va_start(ArgList, Format);
	stbsp_vsnprintf(Result.data(), (int)Result.length(), Format, ArgList);
	va_end(ArgList);

	return Result;
}

WString ToWide(const StringView& Narrow)
{
	WString Result(Narrow.size(), L'\0');
	mbstowcs( Result.data(), Narrow.data(), Result.size());
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
