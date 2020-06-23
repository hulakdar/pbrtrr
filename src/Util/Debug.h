#pragma once

#include <sstream>
#include <iostream>
#include <windows.h>

#define DEBUG_BREAK() if (IsDebuggerPresent()) { __debugbreak(); }

void EnableDebug();

class DebugStream : public std::stringbuf
{
public:
    ~DebugStream() { sync(); }
    int sync()
    {
        ::OutputDebugStringA(str().c_str());
        str(std::string()); // Clear the string buffer
        return 0;
    }
	static DebugStream Instance;
};

class DebugStreamW : public std::wstringbuf
{
public:
    ~DebugStreamW() { sync(); }
    int sync()
    {
        ::OutputDebugStringW(str().c_str());
        str(std::wstring()); // Clear the string buffer
        return 0;
    }
	static DebugStreamW Instance;
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
