#include "Util/Debug.h"
#include "Util/Util.h"
#include "Containers/ComPtr.h"
#include "external/d3dx12.h"
#include "Render/Context.h"

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
#if !defined (RELEASE)
	{
		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
		VALIDATE(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)));

		// Turn on auto-breadcrumbs and page fault reporting.
		pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
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
		return false;
	}
}
