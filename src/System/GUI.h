#pragma once

namespace System {
	class Window;
}

struct ImGuiTexIDWrapper
{
	ImGuiTexIDWrapper(void* TexID)
	{
		Value = (u64)TexID;
	}

	ImGuiTexIDWrapper(u16 InSRV, u16 InMipIndex = 0, bool UseBilinear = false)
	{
		SRV = InSRV;
		Flags = InMipIndex;
		Flags |= u32(UseBilinear) << 31;
	}

	union {
		u64 Value;
		struct {
			u32 SRV;
			u32 Flags;
		};
	};

	operator void*() {return (void*)Value;}
};
