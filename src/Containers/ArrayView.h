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

	const T& operator[](uint64_t Index) { CHECK(Index < Size, "Out of bounds access"); return Data[Index]; }
	uint64_t size() { return Size; }
	const T* data() { return Data; }
private:
	const T* Data;
	uint64_t Size;

	struct ConstIterator
	{
		ConstIterator(const T* InPtr) : Ptr(InPtr) {}
		const T& operator * () const { return *Ptr; }
		const T* operator ->() const { return Ptr; }

		ConstIterator& operator ++()    { Ptr++; return *this; }
		ConstIterator& operator ++(int) { ConstIterator Tmp = *this; ++(*this); return Tmp; }

		friend bool operator == (const ConstIterator& a, const ConstIterator& b) { return a.Ptr == b.Ptr; };
		friend bool operator != (const ConstIterator& a, const ConstIterator& b) { return a.Ptr != b.Ptr; };
	private:
		const T* Ptr;
	};
public:
	ConstIterator begin() { return ConstIterator(Data); }
	ConstIterator end() { return ConstIterator(Data + Size); }
};

using BinaryBlob = TArrayView<uint8_t>;
