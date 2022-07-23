#include "Util/Debug.h"
#include "Util/Util.h"
#include "Containers/ComPtr.h"
#include "external/d3dx12.h"

#include <iostream>
#include <sstream>
#include <dxgi1_6.h>

namespace {
	class Stream : public std::stringbuf
	{
	public:
		~Stream() { sync(); }
		virtual int sync() override
		{
			::OutputDebugStringA(str().c_str());
			printf(str().c_str());
			str(std::string()); // Clear the string buffer
			return 0;
		}
	};

	class WStream : public std::wstringbuf
	{
	public:
		~WStream() { sync(); }
		virtual int sync() override
		{
			::OutputDebugStringW(str().c_str());
			wprintf(str().c_str());
			str(std::wstring()); // Clear the string buffer
			return 0;
		}
	};
	Stream  gStream;
	WStream gWStream;
}

void StartDebugSystem()
{
	// for printing to cout
	std::cout.rdbuf(&gStream);
	std::wcout.rdbuf(&gWStream);
#if !defined(RELEASE) && !defined(PROFILE)
	// d3d12 debug layer
	{
		ComPtr<ID3D12Debug3> debugInterface;
		VALIDATE(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
		debugInterface->EnableDebugLayer();
		debugInterface->SetEnableGPUBasedValidation(true);
		debugInterface->SetEnableSynchronizedCommandQueueValidation(true);
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
