#pragma once

#include <EASTL/vector.h>

template<typename T, typename Allocator = EASTLAllocatorType>
using TArray = eastl::vector<T, Allocator>;

