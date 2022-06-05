#pragma once

#include <iostream>

#define DEBUG_BREAK() \
		if (IsDebuggerPresent()) \
		{ \
			__debugbreak(); \
		}

#ifndef RELEASE
# define CHECK(x, msg) \
		if (!(x)) \
		{ \
			Debug::Print(msg); \
			DEBUG_BREAK(); \
		}

# define VALIDATE(x) \
		if (!Debug::ValidateImpl(x)) \
		{ \
			DEBUG_BREAK(); \
		}

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

