#include "Util/Debug.h"
#include "Util/Util.h"
#include "Containers/ComPtr.h"
#include "external/d3dx12.h"

#include <iostream>
#include <sstream>
#include <dxgi1_6.h>
#include <directxmath.h>

namespace {
	class Stream : public std::stringbuf
	{
	public:
		~Stream() { sync(); }
		int sync()
		{
			::OutputDebugStringA(str().c_str());
			str(std::string()); // Clear the string buffer
			return 0;
		}
	};

	class WStream : public std::wstringbuf
	{
	public:
		~WStream() { sync(); }
		int sync()
		{
			::OutputDebugStringW(str().c_str());
			str(std::wstring()); // Clear the string buffer
			return 0;
		}
	};
	Stream mStream;
	WStream mWStream;
}

void StartDebugSystem()
{
	CHECK(DirectX::XMVerifyCPUSupport(), "This CPU does not support DirectXMath (or instruction set selected while compiling)");
#ifndef RELEASE
	// for printing to cout
	std::cout.rdbuf(&mStream);
	std::wcout.rdbuf(&mWStream);

	// d3d12 debug layer
	{
		ComPtr<ID3D12Debug> debugInterface;
		VALIDATE(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
		debugInterface->EnableDebugLayer();
	}
#endif
}

namespace Debug {
	bool ValidateImpl(long Result)
	{
		if (SUCCEEDED(Result))
			return true;

		std::string ErrorText = std::system_category().message(Result);
		Debug::Print("VALIDATE caught error: ", ErrorText);
		return false;
	}
}
