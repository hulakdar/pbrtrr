#pragma once

#if 0
#include <EASTL/string.h>

using String = eastl::string;
using WString = eastl::wstring;
#else
#include <string>

using String = std::string;
using WString = std::wstring;
#endif
