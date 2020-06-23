#include "Rendering/Device.h"
#include "Util/Util.h"

namespace Rendering {

CDevice	CDevice::Instance;

void CDevice::Init()
{
	Window.Init();
	CreateAdapter();
	CreateDevice();
	CmdQueue.Init(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	CreateSwapchain();
	CurrentBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();

	CheckTearingSupport();

	for (int i = 0; i < ArraySize(DescriptorSizes); i++)
	{
		DescriptorSizes[i] = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE(i));
	}

	RTVHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, BufferCount);
	UpdateRenderTargetViews();
}

void CDevice::CreateAdapter()
{
	UINT createFactoryFlags = 0;
#ifndef RELEASE
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ComPtr<IDXGIFactory4> dxgiFactory;
	VALIDATE(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	ComPtr<IDXGIAdapter1> dxgiAdapter1;

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
				D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr)) &&
			dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
		{
			maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
			VALIDATE(dxgiAdapter1.As(&Adapter));
		}
	}
}

void CDevice::CreateDevice()
{
	VALIDATE(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&Device)));
#ifndef RELEASE
	ComPtr<ID3D12InfoQueue> pInfoQueue;
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

void CDevice::CreateSwapchain()
{
	ComPtr<IDXGIFactory4> dxgiFactory4;
	UINT createFactoryFlags = 0;
#ifndef RELEASE
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	VALIDATE(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

	VALIDATE(dxgiFactory4->MakeWindowAssociation(Window.GetHWND(), DXGI_MWA_NO_ALT_ENTER));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
	swapChainDesc.Width = Window.Size.x;
	swapChainDesc.Height = Window.Size.y;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = FALSE;
	swapChainDesc.SampleDesc = { 1, 0 };
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = BufferCount;
	swapChainDesc.Scaling = DXGI_SCALING_NONE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags = TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain1> swapChain1;
	VALIDATE(dxgiFactory4->CreateSwapChainForHwnd(
		CmdQueue.Get(),
		Window.GetHWND(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain1)
	);

	VALIDATE(swapChain1.As(&SwapChain));
}

void CDevice::UpdateRenderTargetViews()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE RTVHandle(RTVHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < BufferCount; ++i)
	{
		ComPtr<ID3D12Resource> backBuffer;
		VALIDATE(SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		Device->CreateRenderTargetView(backBuffer.Get(), nullptr, RTVHandle);

		BackBuffers[i] = backBuffer;

		RTVHandle.Offset(DescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);
	}

}

ComPtr<ID3D12DescriptorHeap> CDevice::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;
	desc.Type = type;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	ComPtr<ID3D12DescriptorHeap> descriptorHeap;
	VALIDATE(Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

	return descriptorHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE CDevice::GetCurrentRTV()
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		RTVHeap->GetCPUDescriptorHandleForHeapStart(),
		CurrentBackBufferIndex,
		DescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]
	); 
}

void CDevice::Present()
{
	UINT syncInterval = VSync ? 1 : 0;
	UINT presentFlags = TearingSupported && !VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	VALIDATE(SwapChain->Present(syncInterval, presentFlags));
	CurrentBackBufferIndex = (CurrentBackBufferIndex + 1) % BufferCount;
}

void CDevice::CheckTearingSupport()
{
	// Rather than create the DXGI 1.5 factory interface directly, we create the
	// DXGI 1.4 interface and query for the 1.5 interface. This is to enable the 
	// graphics debugging tools which will not support the 1.5 factory interface 
	// until a future update.
	ComPtr<IDXGIFactory4> factory4;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
	{
		ComPtr<IDXGIFactory5> factory5;
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

}
