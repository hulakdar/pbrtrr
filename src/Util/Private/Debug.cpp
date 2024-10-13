#include <stb/stb_sprintf.h>

#include "Util/Debug.h"
#include "Util/Util.h"

#include "System/Win32.h"

#include "Containers/String.h"

void DebugPrint(const char* Format, ...)
{
	va_list ArgList;

	va_start(ArgList, Format);
	int CharLen = stbsp_vsnprintf(nullptr, 0, Format, ArgList);
	va_end(ArgList);

	char Tmp[1024];
	CHECK(CharLen < ArrayCount(Tmp));

	va_start(ArgList, Format);
	stbsp_vsnprintf(Tmp, ArrayCount(Tmp), Format, ArgList);
	va_end(ArgList);

	OutputDebugStringA(Tmp);
}

bool ValidateImpl(long Result)
{
	if (SUCCEEDED(Result))
		return true;

	//std::string ErrorText = std::system_category().message(Result);
	//printf("Validate failed: %.*s", (int)ErrorText.size(), ErrorText.data());
	//OutputDebugStringA(ErrorText.c_str());
	//Debug::Print("VALIDATE caught error: ", ErrorText);

#if 0
	if (Result == DXGI_ERROR_DEVICE_REMOVED)
	{
		Result = GetGraphicsDevice()->GetDeviceRemovedReason();
		ErrorText = std::system_category().message(Result);
		//Debug::Print("DeviceRemoval reason: ", ErrorText);

		TComPtr<ID3D12DeviceRemovedExtendedData> pDred;
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
