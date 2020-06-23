#pragma once

#include "external/d3dx12.h"
#include "System/Window.h"
#include "Rendering/CmdQueue.h"
#include "Containers/ComPtr.h"

#include <dxgi1_6.h>

namespace Rendering
{

class CDevice
{
public:

	static CDevice	Instance;

	CDevice() {}
	CDevice(CDevice& Other) = delete;
	CDevice(CDevice&& Other) = delete;

	CDevice& operator=(CDevice& Other) = delete;
	CDevice& operator=(CDevice&& Other) = delete;

	System::CWindow	Window;
	CCmdQueue		CmdQueue;

	uint64_t	FrameCount = 0;

	UINT	DescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = { 0 };

	UINT	GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE type) { return DescriptorSizes[(int)type]; }

	BOOL	TearingSupported = FALSE;
	UINT	CurrentBackBufferIndex = (UINT)-1;
	bool	VSync = true;

	static const uint64_t BufferCount = 3;
	ComPtr<ID3D12Resource> BackBuffers[BufferCount];
	ComPtr<ID3D12DescriptorHeap> RTVHeap;

	ID3D12Device2* Get() { return Device.Get(); }

	void Init();

	void CreateAdapter();
	void CreateDevice();
	void CreateSwapchain();
	void UpdateRenderTargetViews();
	void Present();

	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV();

private:

	void CheckTearingSupport();

	ComPtr<IDXGIAdapter4>	Adapter;
	ComPtr<IDXGISwapChain4>	SwapChain;
	ComPtr<ID3D12Device2>	Device;
};

}
