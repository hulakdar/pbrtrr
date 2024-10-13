#pragma once

#if 0
#include <EASTL/string_view.h>

using StringView = eastl::string_view;
using WStringView = eastl::wstring_view;
#else
#include <string>

using StringView = std::string_view;
using WStringView = std::wstring_view;
#endif

#include "Common.h"

struct RawDataView
{
    const u8* Ptr;
    u64 Size;

    RawDataView(const u8* InData, u64 InSize)
    {
        Ptr = InData;
        Size = InSize;
    }

    template <typename T>
    RawDataView(const T& Container)
    {
        Ptr = (u8*)Container.data();
        Size = Container.size() * sizeof(*Container.begin());
    }

    const u8* data() const {return Ptr;}
    u64 size() const {return Size;}
    bool empty() const { return Size == 0; }
};

#define VIEW_PRINT(x) (int)x.size(), x.data()
