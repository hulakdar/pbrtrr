#pragma once
#include "Common.h"

namespace pbrtrr {
template <typename T> struct remove_reference     { using type = T; };
template <typename T> struct remove_reference<T&> { using type = T; };
template <typename T> struct remove_reference<T&&>{ using type = T; };
}

#define MOVE(x) static_cast<typename pbrtrr::remove_reference<decltype(x)>::type&&>(x)

template <typename T>
T ReadAndAdvance(u8*& DataPtr)
{
	T Result = *(T*)DataPtr;
	DataPtr += sizeof(T);
	return Result;
}

template <typename T>
void WriteAndAdvance(u8*& DataPtr, const T& Data)
{
	T* Dest = (T*)DataPtr;
	memcpy(Dest, &Data, sizeof(T));
	DataPtr += sizeof(T);
}

template <typename T>
void WriteAndAdvanceContainer(const T& Container, u8*& Memory)
{
	u64 Hash = typeid(T).hash_code();
	WriteAndAdvance<u64>(Memory, Hash);

	u64 Size = Container.size();
	WriteAndAdvance<u64>(Memory, Size);
	for (const auto& Element : Container)
	{
		WriteAndAdvance(Memory, Element);
	}
}

template <typename T>
void WriteContainer(const T& Container, u8* Memory)
{
	WriteAndAdvanceContainer(Container, Memory);
}

template <typename T>
u64 MemorySizeForContainer(const T& Container)
{
	return Container.size() * sizeof(*Container.begin()) + sizeof(u64) * 2;
}

//template <typename T, int Count>
//constexpr int ArrayCount(T (&)[Count]) { return Count; }

#define ArrayCount(x) (sizeof(x) / sizeof(x[0]))

//#define ArrayCount(x) _countof(x)

// size custom literal
constexpr u64 operator ""_kb(unsigned long long int kilobytes) { return kilobytes * 1024; }
constexpr u64 operator ""_mb(unsigned long long int megabytes) { return megabytes * 1024*1024; }
constexpr u64 operator ""_gb(unsigned long long int gigabytes) { return gigabytes * 1024*1024*1024; }