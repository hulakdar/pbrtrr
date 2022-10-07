#pragma once

#include <EASTL/vector.h>

template<typename T, typename Allocator = EASTLAllocatorType>
using TArray = eastl::vector<T, Allocator>;

#include <EASTL/deque.h>
template<typename T, typename Allocator = EASTLAllocatorType>
using TDeque = eastl::deque<T, Allocator>;
