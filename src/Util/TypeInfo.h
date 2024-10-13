#pragma once

#include "Common.h"
#include <imgui.h>

namespace Generated
{
    static String ToString(u8* In)
    {
        String Result = StringFromFormat("%d", (u32)*In);
        return Result;
    }

    static String ToString(u16* In)
    {
        String Result = StringFromFormat("%d", (u32)*In);
        return Result;
    }

    static String ToString(u32* In)
    {
        String Result = StringFromFormat("%d", (u32)*In);
        return Result;
    }

    static String ToString(u64* In)
    {
        String Result = StringFromFormat("%lld", (u32)*In);
        return Result;
    }

    static String ToString(void** In)
    {
        String Result = StringFromFormat("%p", *In);
        return Result;
    }

    static String ToString(void* In)
    {
        String Result = "no stringification";
        return Result;
    }

    static void ToUI(u8* In)
    {
        ImGui::Text("%d", (int)*In);
    }

    static void ToUI(u16* In)
    {
        ImGui::Text("%d", (int)*In);
    }

    static void ToUI(u32* In)
    {
        ImGui::Text("%d", *In);
    }

    static void ToUI(u64* In)
    {
        ImGui::Text("%lld", *In);
    }

    static void ToUI(void** In)
    {
        ImGui::Text("%p", *In);
    }

    static void ToUI(void* In)
    {
        ImGui::Text("no imguification");
    }
}

struct EnumToString
{
    u32 Value;
    StringView Text;
};

struct TypeInfoMember
{
    String (*Stringify)(void*);
    void (*Imguify)(void*);
    StringView MemberName;
    u32 MemberOffset;
    u32 MemberSize;
    u32 ArrayNum;
};