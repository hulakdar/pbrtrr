#include "Util/Debug.h"
#include "Util/Util.h"
#include "Containers/ComPtr.h"
#include "external/d3dx12.h"

#include <iostream>
#include <dxgi1_6.h>

// statics
DebugStream DebugStream::Instance;
DebugStreamW DebugStreamW::Instance;

void EnableDebugLayer()
{
#ifndef RELEASE
	// for printing to cout
	std::cout.rdbuf(&DebugStream::Instance);
	std::wcout.rdbuf(&DebugStreamW::Instance);

	// d3d12 debug layer
	{
		ComPtr<ID3D12Debug> debugInterface;
		auto Result = D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface));
		CHECK(SUCCEEDED(Result), "DebugInterface not initialized correctly");
		debugInterface->EnableDebugLayer();
	}
#endif
}
