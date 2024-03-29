#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

using String = eastl::string;
using WString = eastl::wstring;
using StringView = eastl::string_view;
using WStringView = eastl::wstring_view;

String  StringFromFormat(const char   * Format, ...);
WString StringFromFormat(const wchar_t* Format, ...);

WString ToWide(const StringView& Narrow);
WString ToWide(const String& Narrow);

bool EndsWith(StringView Word, StringView Suffix);

