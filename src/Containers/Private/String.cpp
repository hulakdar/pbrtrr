#include "../String.h"
#include "external/stb/stb_sprintf.h"

String StringFromFormat(const char* Format, ...)
{
	va_list ArgList;

	va_start(ArgList, Format);
	int CharLen = stbsp_vsnprintf(nullptr, 0, Format, ArgList);
	va_end(ArgList);

	String Result(CharLen, '\0');

	va_start(ArgList, Format);
	stbsp_vsnprintf(Result.data(), Result.length(), Format, ArgList);
	va_end(ArgList);

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
