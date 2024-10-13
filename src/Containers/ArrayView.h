#pragma once

#include "Util/Debug.h"
#include "Containers/Array.h"

template <typename T>
class TArrayView
{
public:
	TArrayView(const TArray<T>& Initializer)
	{
		Data = Initializer.data();
		Size = Initializer.size();
	}

	TArrayView(std::initializer_list<T> IL)
	{
		Data = (T*)IL.begin();
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

	const T& operator[](uint64_t Index) { CHECK(Index < Size, "Out of bounds access"); return Data[Index]; }
	uint64_t size() { return Size; }
	const T* data() { return Data; }
private:
	const T* Data;
	uint64_t Size;

	struct RandomAccessIterator
	{
		using iterator_category = std::random_access_iterator_tag;
		using difference_type = i64;
		using value_type = T;

		using pointer = T*;
		using reference = T&;

		RandomAccessIterator(const T* InPtr) : Ptr(InPtr) {}
		const T& operator * () const { return *Ptr; }
		const T* operator ->() const { return Ptr; }

		RandomAccessIterator& operator ++()    { Ptr++; return *this; }
		RandomAccessIterator& operator ++(int) { RandomAccessIterator Tmp = *this; ++(*this); return Tmp; }

		RandomAccessIterator& operator +=(difference_type N) {Ptr += N; return *this; }

		friend bool operator == (const RandomAccessIterator& a, const RandomAccessIterator& b) { return a.Ptr == b.Ptr; };
		friend bool operator != (const RandomAccessIterator& a, const RandomAccessIterator& b) { return a.Ptr != b.Ptr; };
		friend difference_type operator - (const RandomAccessIterator& a, const RandomAccessIterator& b) { return a.Ptr - b.Ptr; };
	private:
		const T* Ptr;
	};
public:
	RandomAccessIterator begin() { return RandomAccessIterator(Data); }
	RandomAccessIterator end() { return RandomAccessIterator(Data + Size); }
};

using BinaryBlob = TArrayView<uint8_t>;
