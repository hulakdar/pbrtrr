#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winnt.h>
#include <iostream>
#include <wrl.h>
#include <shellapi.h>

#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "external/d3dx12.h"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Util.h"

bool CheckTearingSupport()
{
    BOOL allowTearing = FALSE;
 
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
                &allowTearing, sizeof(allowTearing))))
            {
                allowTearing = FALSE;
            }
        }
    }
 
    return allowTearing == TRUE;
}

auto CreateDescriptorHeap(Ptr<ID3D12Device2>& device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;
 
    Ptr<ID3D12DescriptorHeap> descriptorHeap;
    VALIDATE(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));
 
    return descriptorHeap;
}

template <int BufferCount>
void UpdateRenderTargetViews(Ptr<ID3D12Device2>& device, Ptr<IDXGISwapChain4>& swapChain, Ptr<ID3D12DescriptorHeap>& descriptorHeap, Ptr<ID3D12Resource> (&BackBuffers)[BufferCount])
{
    auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
 
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());
 
    for (int i = 0; i < BufferCount ; ++i)
    {
        Ptr<ID3D12Resource> backBuffer;
        VALIDATE(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
 
        device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
 
        BackBuffers[i] = backBuffer;
 
        rtvHandle.Offset(rtvDescriptorSize);
    }
}

uint64_t Signal(Ptr<ID3D12CommandQueue>& commandQueue, Ptr<ID3D12Fence>& fence, uint64_t& fenceValue)
{
    uint64_t fenceValueForSignal = ++fenceValue;
    VALIDATE(commandQueue->Signal(fence.Get(), fenceValueForSignal));
 
    return fenceValueForSignal;
}

void WaitForFenceValue(Ptr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent)
{
    if (fence->GetCompletedValue() < fenceValue)
    {
        VALIDATE(fence->SetEventOnCompletion(fenceValue, fenceEvent));
        ::WaitForSingleObject(fenceEvent, static_cast<DWORD>(-1));
    }
}

void Flush(Ptr<ID3D12CommandQueue> commandQueue, Ptr<ID3D12Fence> fence, uint64_t& fenceValue, HANDLE fenceEvent )
{
    uint64_t fenceValueForSignal = Signal(commandQueue, fence, fenceValue);
    WaitForFenceValue(fence, fenceValueForSignal, fenceEvent);
}

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE ,
                     LPSTR     ,
                     int       nCmdShow)
{
	EnableDebugLayer();

	uint32_t ClientWidth = 1280/2;
	uint32_t ClientHeight = 720/2;

	assert(glfwInit());

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	
	GLFWwindow *Window = glfwCreateWindow(ClientWidth, ClientHeight, "pbrtrr", nullptr, nullptr);

	// Window handle.
	HWND hWnd = glfwGetWin32Window(Window);

	Ptr<IDXGIAdapter4> Adapter;
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

	Ptr<ID3D12Device2> Device;
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
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
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

	Ptr<ID3D12CommandQueue> CommandQueue;
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type =		D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		desc.Flags =    D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 0;
	 
		VALIDATE(Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&CommandQueue)));
	}


	// The number of swap chain back buffers.
	const uint8_t NumFrames = 3;
	bool TearingSupported = CheckTearingSupport();
	
	Ptr<IDXGISwapChain4> SwapChain;
	{
		Ptr<IDXGIFactory4> dxgiFactory4;
		UINT createFactoryFlags = 0;
#ifndef RELEASE
		createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
	 
		VALIDATE(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
		swapChainDesc.Width = ClientWidth;
		swapChainDesc.Height = ClientHeight;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc = { 1, 0 };
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = NumFrames;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		swapChainDesc.Flags = TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		Ptr<IDXGISwapChain1> swapChain1;
		VALIDATE(dxgiFactory4->CreateSwapChainForHwnd(
			CommandQueue.Get(),
			hWnd,
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChain1)
		);
	 
		// Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
		// will be handled manually.
		VALIDATE(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));
	 
		VALIDATE(swapChain1.As(&SwapChain));
	}


	Ptr<ID3D12DescriptorHeap> RTVDescriptorHeap = CreateDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 32);

	Ptr<ID3D12Resource> BackBuffers[NumFrames];
	UpdateRenderTargetViews(Device, SwapChain, RTVDescriptorHeap, BackBuffers);

	Ptr<ID3D12CommandAllocator> CommandAllocators[NumFrames];
	for (int i = 0; i < NumFrames; i++)
		VALIDATE(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocators[i])));

	// Synchronization objects
	uint64_t FenceValue = 0;
	Ptr<ID3D12Fence> Fence;
	VALIDATE(Device->CreateFence(FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)));

	HANDLE FenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(FenceEvent);

	uint64_t FrameFenceValues[NumFrames] = {};

	// By default, enable V-Sync.
	bool VSync = true;

	UINT RTVDescriptorSize = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	UINT CurrentBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();

	auto& CommandAllocator = CommandAllocators[CurrentBackBufferIndex];
	auto& BackBuffer = BackBuffers[CurrentBackBufferIndex];

	Ptr<ID3D12GraphicsCommandList> CommandList;
    VALIDATE(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&CommandList)));
    VALIDATE(CommandList->Close());

	CommandAllocator->Reset();
	CommandList->Reset(CommandAllocator.Get(), nullptr);
	// Clear the render target.
	{
		CD3DX12_RESOURCE_BARRIER barrier =
			CD3DX12_RESOURCE_BARRIER::Transition( BackBuffer.Get(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		CommandList->ResourceBarrier(1, &barrier);

		FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        CurrentBackBufferIndex, RTVDescriptorSize);
 
		CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	}

	// Present
	{
		CD3DX12_RESOURCE_BARRIER barrier =
			CD3DX12_RESOURCE_BARRIER::Transition(BackBuffer.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		CommandList->ResourceBarrier(1, &barrier);

		VALIDATE(CommandList->Close());

		ID3D12CommandList* const commandLists[] = {
			CommandList.Get()
		};
		CommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
	}

	UINT syncInterval = VSync ? 1 : 0;
	UINT presentFlags = TearingSupported && !VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	VALIDATE(SwapChain->Present(syncInterval, presentFlags));
	 
	FrameFenceValues[CurrentBackBufferIndex] = Signal(CommandQueue, Fence, FenceValue);
	CurrentBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();

	WaitForFenceValue(Fence, FrameFenceValues[CurrentBackBufferIndex], FenceEvent);

    //Main message loop
    while(!glfwWindowShouldClose(Window))
	{
		glfwPollEvents();
		if (glfwGetKey(Window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(Window, GLFW_TRUE);
		}
    }
	// Make sure the command queue has finished all commands before closing.
    Flush(CommandQueue, Fence, FenceValue, FenceEvent);
 
    ::CloseHandle(FenceEvent);

	glfwTerminate();
}