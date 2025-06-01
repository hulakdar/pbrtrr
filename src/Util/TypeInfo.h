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

    static void ToUI(float* In) {
        ImGui::Text("%f", *In);
    }

    static void ToUI(void** In)
    {
        ImGui::Text("%p", *In);
    }

    template<typename T>
    static void ToUI(TArray<T>* In)
    {
		if (ImGui::TreeNodeEx((void*)In, ImGuiTreeNodeFlags_None, "TArray"))
        {
            for (u32 i = 0; i < In->size(); ++i)
            {
                ImGui::Text("[%d]:", i); ImGui::SameLine(); ToUI(&In->at(i));
            }
            ImGui::TreePop();
        }
    }

    static void ToUI(void* In)
    {
        ImGui::Text("no imguification");
    }

    template<typename T>
    static void ToUI(T** In)
    {
        ToUI(*In);
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

namespace Generated
{
    static String ToString(TypeInfoMember* Info, u64 NumMembers, void* In)
    {
		String Result;
		for (int i = 0; i < NumMembers;++i)
		{
			Result += String(Info[i].MemberName);
			Result += ':';
			Result += ' ';
			Result += Info[i].Stringify((void*)In);
			Result += '\n';
		}
		return Result;
    }

	static void ToUI(TypeInfoMember* Info, u64 NumMembers, void* In, const char* StructName)
	{
		if (ImGui::TreeNodeEx((void*)In, ImGuiTreeNodeFlags_None, StructName))
		{
			for (int i = 0; i < NumMembers;++i)
			{
				Info[i].Imguify(In);
			}
			ImGui::TreePop();
		}
	}
};