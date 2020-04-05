#include "Util.h"
#include "Debug.h"

#include "external/d3dx12.h"
#include <iostream>
#include <assert.h>
#include <system_error>
#include <dxgi1_6.h>

bool IfErrorThenPrint(HRESULT Result)
{
	if (SUCCEEDED(Result))
		return true;

	std::string ErrorText = std::system_category().message(Result);
	Print("VALIDATE caught error: ", ErrorText);
	return false;
}

// for eastl
// maybe we could write our own custom allocator in future
void* operator new[](size_t size, const char*, int, unsigned, const char*, int)
{
	return new uint8_t[size];
}

void* operator new[](size_t size, size_t, size_t, const char*, int, unsigned, const char*, int) 
{
	return new uint8_t[size];
}  
