#pragma once

#include <EASTL/vector.h>

template<typename T, typename Allocator = EASTLAllocatorType>
using TArray = eastl::vector<T, Allocator>;

template <typename T>
class TArrayView
{
public:
	TArrayView(TArray<T>& Initializer)
	{
		Data = Initializer.data();
		Size = Initializer.size();
	}

	TArrayView(T* InData, uint64_t InSize)
	{
		Data = InData;
		Size = InSize;
	}

	TArrayView(T* InData)
	{
		Data = InData;
		Size = 1;
	}

	T& operator[](uint64_t Index) { CHECK(Index < Size, "Out of bounds access"); return Data[Index]; }
	uint64_t size() { return Size; }
	T* data() { return Data; }
private:
	T*			Data;
	uint64_t	Size;

	struct Iterator
	{
		Iterator(T* InPtr) : Ptr(InPtr) {}
		T& operator*() const { return *Ptr; }
		T* operator->() const { return Ptr; }

		Iterator& operator++() { Ptr++; return *this; }
		Iterator& operator++(int) { Iterator Tmp = *this; operator++(); return Tmp; }

		using iterator_category = std::forward_iterator_tag;
		using difference_type   = std::ptrdiff_t;
		//using value_type        = T;
		//using pointer           = T*;  // or also value_type*
		//using reference         = T&;  // or also value_type&

		friend bool operator == (const Iterator& a, const Iterator& b) { return a.Ptr == b.Ptr; };
		friend bool operator != (const Iterator& a, const Iterator& b) { return a.Ptr != b.Ptr; };
	private:
		T* Ptr;
	};

public:

	Iterator begin() { return Iterator(Data); }
	Iterator end() { return Iterator(Data + Size); }
};
