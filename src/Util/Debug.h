#pragma once

#include <iostream>
#include <Windows.h>

#define DEBUG_BREAK() \
		if (IsDebuggerPresent()) \
		{ \
			__debugbreak(); \
		}

#if !defined(RELEASE)
# define CHECK(x, msg) \
		do {           \
			if (!(x))  \
			{          \
				Debug::Print("Assertion failed: '", #x, "'. ",msg); \
				DEBUG_BREAK(); \
			}          \
		} while (false)

# define VALIDATE(x) \
		do {         \
			if (!Debug::ValidateImpl(x)) \
			{ \
				DEBUG_BREAK(); \
			} \
		} while (false)

#else
# define CHECK(x, msg) ((void)(x))
# define VALIDATE(x) (x)
#endif

# define DISABLE_OPTIMIZATION __pragma(optimize("", off))
# define ENABLE_OPTIMIZATION __pragma(optimize("", on))

void StartDebugSystem();

namespace Debug {
	template <typename T>
	void PrintImpl(T Arg)
	{
		using namespace std;
		if constexpr (is_same<T, wchar_t[]>() || is_same<T, wchar_t*>() || is_same<T, wstring>())
		{
			wcout << Arg << " ";
			wcout.flush();
		}
		else
		{
			cout << Arg << " ";
			cout.flush();
		}
	}

	template <typename ...Ts>
	void Print(Ts&& ...Args)
	{
		(PrintImpl(Args), ...);
		std::cout << std::endl;
	}

	bool ValidateImpl(long Result);
}

