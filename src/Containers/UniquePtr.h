#pragma once

#if 0
#include <EASTL/unique_ptr.h>
template <typename T>
using TUniquePtr = eastl::unique_ptr<T>;
#else

#include <Util/Util.h>

template <typename T>
struct TUniquePtr
{
    TUniquePtr() = default;
    TUniquePtr(T *In) { Ptr = In; }

    TUniquePtr(const TUniquePtr& Other) = delete;
    TUniquePtr& operator=(const TUniquePtr& Other) = delete;

    TUniquePtr(TUniquePtr&& Other) noexcept { *this = MOVE(Other); }

    TUniquePtr& operator=(TUniquePtr&& Other) noexcept
    {
        if (this == &Other) return *this;

        Ptr = Other.Ptr;
        Other.Ptr = nullptr;
        return *this;
    }

    ~TUniquePtr() { delete Ptr; }

    operator bool() const noexcept {return Ptr != nullptr;}
    operator T*() const noexcept {return Ptr;}
    T *operator->() const noexcept { return Ptr; }
    T* Ptr = nullptr;
};
#endif