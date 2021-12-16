#pragma once
#include <EASTL/internal/move_help.h>
#include "Containers/String.h"
#include "Containers/Array.h"
#include "Containers/ComPtr.h"
#include "external/d3dx12.h"
#define MOVE(x) EASTL_MOVE_INLINE(x)

void WaitForFenceValue(ComPtr<ID3D12Fence>& Fence, uint64_t FenceValue, HANDLE Event);
void Flush(ComPtr<ID3D12CommandQueue>& CommandQueue, ComPtr<ID3D12Fence>& Fence, uint64_t& FenceValue, HANDLE FenceEvent);
uint64_t Signal(ComPtr<ID3D12CommandQueue>& CommandQueue, ComPtr<ID3D12Fence>& Fence, uint64_t& FenceValue);

template <typename T, int count>
int ArraySize(T (&)[count]) { return count; }

StringView LoadWholeFile(const char* Path);

static wchar_t g_ScratchNameBuffer[1024];

template<typename T, typename... TArgs>
void SetD3DName(ComPtr<T>& Ptr, TArgs ...Args)
{
	if constexpr (sizeof...(Args) == 1)
	{
		Ptr->SetName(Args...);
	}
	else
	{
		wsprintf(g_ScratchNameBuffer, Args...);
		Ptr->SetName(g_ScratchNameBuffer);
	}
}

// size custom literal
inline size_t operator "" _kb(size_t kilobytes) { return kilobytes * 1024; }
inline size_t operator "" _mb(size_t megabytes) { return megabytes * 1024_kb; }
inline size_t operator "" _gb(size_t gigabytes) { return gigabytes * 1024_mb; }
