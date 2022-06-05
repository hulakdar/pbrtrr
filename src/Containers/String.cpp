#include "String.h"

String StringFromFormat(const char* Format, ...)
{
	String Result(16, '\0');

	int CharNumber = -1;
	do {
		va_list ArgList;
		va_start(ArgList, Format);
		CharNumber = vsprintf_s(Result.data(), Result.length(), Format, ArgList);
		va_end(ArgList);
	} while (CharNumber == -1 || CharNumber > Result.length());


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
