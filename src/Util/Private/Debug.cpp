#include "Util/Debug.h"
#include "Util/Util.h"
#include "external/d3dx12.h"

#include <iostream>
#include <assert.h>

// statics
DebugStream DebugStream::Instance;
DebugStreamW DebugStreamW::Instance;

void EnableDebugLayer()
{
#ifndef SHIPPING
	// for printing to cout
	std::cout.rdbuf(&DebugStream::Instance);
	std::wcout.rdbuf(&DebugStreamW::Instance);

	// d3d12 debug layer
    ComPtr<ID3D12Debug> debugInterface;
	auto Result = D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface));
	assert(SUCCEEDED(Result));
    debugInterface->EnableDebugLayer();
#endif
}
