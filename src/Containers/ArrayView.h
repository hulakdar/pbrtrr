#pragma once

#include "Util/Debug.h"
#include "Containers/Array.h"

template <typename T>
class TArrayView
{
public:
	TArrayView(TArray<T>& Initializer)
	{
		Data = Initializer.data();
		Size = Initializer.size();
	}

	TArrayView(std::initializer_list<T> IL)
	{
		Data = (T*)std::data(IL);
		Size = IL.size();
	}

	TArrayView(T* InData, uint64_t InSize)
	{
		Data = InData;
		Size = InSize;
	}

	TArrayView(T* InData)
	{
		Data = InData;
		Size = InData != nullptr;
	}

	T& operator[](uint64_t Index) { CHECK(Index < Size, "Out of bounds access"); return Data[Index]; }
	uint64_t size() { return Size; }
	T* data() { return Data; }
private:
	T*	Data;
	uint64_t	Size;

	struct Iterator
	{
		Iterator(T* InPtr) : Ptr(InPtr) {}
		T& operator*() const { return *Ptr; }
		T* operator->() const { return Ptr; }

		Iterator& operator++() { Ptr++; return *this; }
		Iterator& operator++(int) { Iterator Tmp = *this; operator++(); return Tmp; }

		//using iterator_category = std::forward_iterator_tag;
		//using difference_type   = std::ptrdiff_t;

		friend bool operator == (const Iterator& a, const Iterator& b) { return a.Ptr == b.Ptr; };
		friend bool operator != (const Iterator& a, const Iterator& b) { return a.Ptr != b.Ptr; };
	private:
		T* Ptr;
	};

public:

	Iterator begin() { return Iterator(Data); }
	Iterator end() { return Iterator(Data + Size); }
};
