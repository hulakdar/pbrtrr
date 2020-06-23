#pragma once
#include <EASTL/unique_ptr.h>
#include <EASTL/internal/move_help.h>

template<typename T>
using TUniquePtr = eastl::unique_ptr<T>;

template<typename T, typename... Ts>
TUniquePtr<T> MakeUnique(Ts&& ...Args)
{
	return TUniquePtr<T>(new T(eastl::forward(Args)...));
}
