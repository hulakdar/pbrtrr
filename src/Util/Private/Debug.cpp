#include "Util/Debug.h"
#include "Util/Util.h"
#include "Containers/ComPtr.h"
#include "external/d3dx12.h"
#include "Render/Context.h"

#include <iostream>
#include <sstream>
#include <dxgi1_6.h>
#include <Threading/Mutex.h>

TracyLockable(Mutex, gPrintMutex);

void LockPrint()
{
	gPrintMutex.lock();
}

void UnlockPrint()
{
	gPrintMutex.unlock();
}

namespace {
	class Stream : public std::stringbuf
	{
	public:
		~Stream() { sync(); }
		virtual int sync() override
		{
			auto s = str();
			const char* cs = s.c_str();
			::OutputDebugStringA(cs);
			printf("%s", cs);
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
			auto s = str();
			const wchar_t* cs = s.c_str();
			::OutputDebugStringW(cs);
			wprintf(L"%s", cs);
			str(std::wstring()); // Clear the string buffer
			return 0;
		}
	};
	Stream  gStream;
	WStream gWStream;
}

void InitDebug()
{
	// for printing to cout
	std::cout.rdbuf(&gStream);
	std::wcout.rdbuf(&gWStream);
}

namespace Debug {
	bool ValidateImpl(long Result)
	{
		if (SUCCEEDED(Result))
			return true;

		std::string ErrorText = std::system_category().message(Result);
		Debug::Print("VALIDATE caught error: ", ErrorText);

#if 0
		if (Result == DXGI_ERROR_DEVICE_REMOVED)
		{
			Result = GetGraphicsDevice()->GetDeviceRemovedReason();
			ErrorText = std::system_category().message(Result);
			Debug::Print("DeviceRemoval reason: ", ErrorText);

			ComPtr<ID3D12DeviceRemovedExtendedData> pDred;
			VALIDATE(GetGraphicsDevice()->QueryInterface(IID_PPV_ARGS(&pDred)));
			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT DredAutoBreadcrumbsOutput;
			D3D12_DRED_PAGE_FAULT_OUTPUT DredPageFaultOutput;
			VALIDATE(pDred->GetAutoBreadcrumbsOutput(&DredAutoBreadcrumbsOutput));
			VALIDATE(pDred->GetPageFaultAllocationOutput(&DredPageFaultOutput));
			if (DredAutoBreadcrumbsOutput.pHeadAutoBreadcrumbNode)
			{
				DEBUG_BREAK();
			}
			if (DredPageFaultOutput.PageFaultVA)
			{
				DEBUG_BREAK();
			}
		}
#endif
		return false;
	}
}
