#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

using String = eastl::string;
using WString = eastl::wstring;
using StringView = eastl::string_view;
using WStringView = eastl::wstring_view;

static String StringFromFormat(const char* Format, ...)
{
	char Result[512];

	va_list ArgList;
	va_start(ArgList, Format);
	_vsprintf_l(Result, Format, NULL, ArgList);
	va_end(ArgList);

	return String(Result);
}

