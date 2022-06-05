#pragma once


#ifndef RELEASE
#include <windows.h>
static wchar_t g_ScratchNameBuffer[1024];

template<typename T, typename... TArgs>
void SetD3DName(T& Ptr, TArgs ...Args)
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
#else
template<typename T, typename... TArgs>
void SetD3DName(T& Ptr, TArgs ...Args) {}
#endif

#include <WinPixEventRuntime/pix3.h>

