#pragma once
#include <wrl.h>
#include <assert.h>
#include <EASTL/queue.h>

#define VALIDATE(x) assert(IfErrorThenPrint(x));

template <typename T, int count>
int ArraySize(T (&)[count]) { return count; }

void EnableDebugLayer();
bool IfErrorThenPrint(HRESULT Result);

// handy aliases

template <typename T>
using Ptr = Microsoft::WRL::ComPtr<T>;

template<typename T>
using Queue = eastl::queue<T>;

