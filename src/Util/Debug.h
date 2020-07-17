#pragma once

#include <sstream>
#include <iostream>
#include <windows.h>

#define DEBUG_BREAK() if (IsDebuggerPresent()) { __debugbreak(); }

#ifndef RELEASE
# define CHECK(x, msg) if (!(x)) { Debug::Print(msg); DEBUG_BREAK();}
# define CHECK_RETURN(x, msg, ret) if (!(x)) { Debug::Print(msg); DEBUG_BREAK(); return ret;}
# define VALIDATE(x) if (!Debug::ValidateImpl(x)) {DEBUG_BREAK();}
#else
# define CHECK(x, msg) ((void)(x))
# define CHECK_RETURN(x, msg, ret) if (!(x)) { return ret; }
# define VALIDATE(x) (x)
#endif

# define DISABLE_OPTIMIZATION __pragma(optimize("", off))
# define ENABLE_OPTIMIZATION __pragma(optimize("", on))

namespace Debug {
	class Stream : public std::stringbuf
	{
	public:
		~Stream() { sync(); }
		int sync()
		{
			::OutputDebugStringA(str().c_str());
			str(std::string()); // Clear the string buffer
			return 0;
		}
	};

	class WStream : public std::wstringbuf
	{
	public:
		~WStream() { sync(); }
		int sync()
		{
			::OutputDebugStringW(str().c_str());
			str(std::wstring()); // Clear the string buffer
			return 0;
		}
	};

	class Scope
	{
		Stream mStream;
		WStream mWStream;
	public:
		Scope();
		~Scope();
	};

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
