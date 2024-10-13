#pragma once

#include "Common.h"
#include "Util/Debug.h"

#include <EASTL/iterator.h>

template <typename T1, typename T2>
using TPair = std::pair<T1, T2>;

template <typename T1, typename T2>
struct TParallelArray
{
	struct Iterator
	{
		Iterator(TParallelArray<T1, T2> *InContainer, u64 InIndex) : Container(InContainer), Index(InIndex) {}

		using iterator_category = std::random_access_iterator_tag;
		using difference_type = i64;
		using value_type = std::pair<T1, T2>;

		using pointer = std::pair<T1*, T2*>;
		using reference = std::pair<T1&, T2&>;

		reference operator*() const { return reference(Container->V1[Index], Container->V2[Index]); }

		Iterator& operator++() { Index++; return *this; }
		Iterator& operator++(int) { Iterator tmp = *this; Index++; return tmp; }

		Iterator& operator--() { Index--; return *this; }
		Iterator& operator--(int) { Iterator tmp = *this; Index--; return tmp; }

		Iterator operator+(i64 Offset) { CHECK(Index + Offset <= Container->size()); return Iterator{ Container, Index + Offset }; }
		Iterator operator-(i64 Offset) { CHECK(Index - Offset >= 0); return Iterator{ Container, Index - Offset }; }

		difference_type operator<(const Iterator& Other) { return Index < Other.Index; }
		difference_type operator>=(const Iterator& Other) { return Index >= Other.Index; }

		difference_type operator-(const Iterator& Other) { return Index - Other.Index; }

		bool operator==(const Iterator& Other) { return Container == Other.Container && Index == Other.Index; }
		bool operator!=(const Iterator& Other) { return Container != Other.Container || Index != Other.Index; }

		TParallelArray<T1, T2>* Container;
		u64 Index;
	};

	Iterator begin() { return Iterator(this, 0); }
	Iterator end() { return Iterator(this, V1.size()); }
	
	TPair<T1&, T2&> operator[](u64 i) { return { V1[i], V2[i] }; }
	TPair<T1&, T2&> push_back() { return { V1.push_back(), V2.push_back() }; }

	u64 size() { CHECK(V1.size() == V2.size()); return V1.size(); }
	void clear() { V1.clear(); V2.clear(); }

	TArray<T1> V1;
	TArray<T2> V2;
};
