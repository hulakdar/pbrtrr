#pragma once

#include "external/d3dx12.h"
#include "Util/Util.h"
#include "System/Window.h"
#include "Rendering/CmdQueue.h"

#include <dxgi1_6.h>

namespace Rendering
{

class CDevice
{
public:
	System::CWindow	Window;
	CCmdQueue		CmdQueue;

	uint64_t		RTVDescriptorSize = 0;
	uint64_t		DSVDescriptorSize = 0;
	BOOL			TearingSupported = FALSE;
	UINT			CurrentBackBufferIndex = (UINT)-1;
	bool			VSync = true;

	static const uint8_t BufferCount = 3;
	ComPtr<ID3D12Resource> BackBuffers[BufferCount];
	ComPtr<ID3D12DescriptorHeap> RTVHeap;

	ID3D12Device2* Get() {return Device.Get();}

	void Init();

	void CreateAdapter();
	void CreateDevice();
	void CreateSwapchain();
	void UpdateRenderTargetViews();
	void Present();

	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV();

private:

	void CheckTearingSupport();

	ComPtr<IDXGIAdapter4>		Adapter;
	ComPtr<IDXGISwapChain4>	SwapChain;
	ComPtr<ID3D12Device2>		Device;
};

}
