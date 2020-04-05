#pragma once

#include "Util/Util.h"
#include "System/Window.h"
#include "Rendering/CmdQueue.h"

#include "external/d3dx12.h"
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
	Ptr<ID3D12Resource> BackBuffers[BufferCount];
	Ptr<ID3D12Resource> DepthBuffer;

	// this should be private?
	Ptr<IDXGIAdapter4>		Adapter;
	Ptr<IDXGISwapChain4>	SwapChain;
	Ptr<ID3D12Device2>		Device;

	ID3D12Device2* Get() {return Device.Get();}

	void Init()
	{
		Window.Init();
		CreateAdapter();
		CreateDevice();
		CmdQueue.Init(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
		CreateSwapchain();

		CheckTearingSupport();
		RTVDescriptorSize = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		DSVDescriptorSize = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	}

	// Window handle.
	void CreateAdapter()
	{
		UINT createFactoryFlags = 0;
#ifndef RELEASE
		createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

		Ptr<IDXGIFactory4> dxgiFactory;
		VALIDATE(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

		Ptr<IDXGIAdapter1> dxgiAdapter1;

		SIZE_T maxDedicatedVideoMemory = 0;
		for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
			dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

			// Check to see if the adapter can create a D3D12 device without actually 
			// creating it. The adapter with the largest dedicated video memory
			// is favored.
			if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
				SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
					D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
				dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
			{
				maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
				VALIDATE(dxgiAdapter1.As(&Adapter));
			}
		}
	}

	void CreateDevice()
	{
		VALIDATE(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)));
#ifndef RELEASE
		Ptr<ID3D12InfoQueue> pInfoQueue;
		if (SUCCEEDED(Device.As(&pInfoQueue)))
		{
			pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
			pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
		}

		// Suppress messages based on their severity level
		D3D12_MESSAGE_SEVERITY Severities[] =
		{
			D3D12_MESSAGE_SEVERITY_INFO
		};

		// Suppress individual messages by their ID
		D3D12_MESSAGE_ID DenyIds[] = {
			D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message. #todo seka: investigate
			D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
			D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
		};

		D3D12_INFO_QUEUE_FILTER NewFilter = {};
		NewFilter.DenyList.NumSeverities = _countof(Severities);
		NewFilter.DenyList.pSeverityList = Severities;
		NewFilter.DenyList.NumIDs = _countof(DenyIds);
		NewFilter.DenyList.pIDList = DenyIds;

		VALIDATE(pInfoQueue->PushStorageFilter(&NewFilter));
#endif
	}

	void CreateSwapchain()
	{
		Ptr<IDXGIFactory4> dxgiFactory4;
		UINT createFactoryFlags = 0;
#ifndef RELEASE
		createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

		VALIDATE(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
		swapChainDesc.Width = Window.Size.x;
		swapChainDesc.Height = Window.Size.y;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc = { 1, 0 };
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = BufferCount;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		swapChainDesc.Flags = TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		Ptr<IDXGISwapChain1> swapChain1;
		VALIDATE(dxgiFactory4->CreateSwapChainForHwnd(
			CmdQueue.Get(),
			Window.GetHWND(),
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChain1)
		);

		// Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
		// will be handled manually.
		VALIDATE(dxgiFactory4->MakeWindowAssociation(Window.GetHWND(), DXGI_MWA_NO_ALT_ENTER));

		VALIDATE(swapChain1.As(&SwapChain));
	}

	void UpdateBufferViews(Ptr<ID3D12DescriptorHeap>& RTVDescriptorHeap, Ptr<ID3D12DescriptorHeap>& DSVDescriptorHeap)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE RTVHandle(RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		for (int i = 0; i < BufferCount; ++i)
		{
			Ptr<ID3D12Resource> backBuffer;
			VALIDATE(SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

			Device->CreateRenderTargetView(backBuffer.Get(), nullptr, RTVHandle);

			BackBuffers[i] = backBuffer;

			RTVHandle.Offset(RTVDescriptorSize);
		}

	}

	auto CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = numDescriptors;
		desc.Type = type;

		Ptr<ID3D12DescriptorHeap> descriptorHeap;
		VALIDATE(Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

		return descriptorHeap;
	}

	void Present()
	{
		UINT syncInterval = VSync ? 1 : 0;
		UINT presentFlags = TearingSupported && !VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
		VALIDATE(SwapChain->Present(syncInterval, presentFlags));
	}

private:

	void CheckTearingSupport()
	{
		// Rather than create the DXGI 1.5 factory interface directly, we create the
		// DXGI 1.4 interface and query for the 1.5 interface. This is to enable the 
		// graphics debugging tools which will not support the 1.5 factory interface 
		// until a future update.
		Ptr<IDXGIFactory4> factory4;
		if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
		{
			Ptr<IDXGIFactory5> factory5;
			if (SUCCEEDED(factory4.As(&factory5)))
			{
				if (FAILED(factory5->CheckFeatureSupport(
					DXGI_FEATURE_PRESENT_ALLOW_TEARING,
					&TearingSupported, sizeof(TearingSupported))))
				{
					TearingSupported = FALSE;
				}
			}
		}
	}

};
}
