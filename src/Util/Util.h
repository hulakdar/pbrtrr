#pragma once
#include <wrl.h>
#include <EASTL/queue.h>
#include <EASTL/deque.h>
#include <EASTL/shared_ptr.h>

#define VALIDATE(x) if (!IfErrorThenPrint(x)) {__debugbreak();}

template <typename T, int count>
int ArraySize(T (&)[count]) { return count; }

void EnableDebugLayer();
bool IfErrorThenPrint(HRESULT Result);

// handy aliases

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

template<typename T>
using TQueue = eastl::queue<T>;

template<typename T>
using TDeque = eastl::deque<T>;

template<typename T>
using TUniquePtr = eastl::unique_ptr<T>;

template<typename T>
using TSharedPtr = eastl::shared_ptr<T>;

size_t operator "" _kb(size_t kilobytes) { return kilobytes * 1024; }
size_t operator "" _mb(size_t megabytes) { return megabytes * 1024 * 1024; }
size_t operator "" _gb(size_t gigabytes) { return gigabytes * 1024 * 1024 * 1024; }
