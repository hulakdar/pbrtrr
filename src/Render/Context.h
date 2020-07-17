
#include "Containers/ComPtr.h"
#include "System/Window.h"
#include "Util/Debug.h"
#include "Util/Util.h"
#include "external/d3dx12.h"

#include <dxgi1_6.h>
namespace Render {

inline bool CheckTearingSupport(ComPtr<IDXGIFactory4> dxgiFactory)
{
    BOOL allowTearing = FALSE;
 
	ComPtr<IDXGIFactory5> factory5;
	if (SUCCEEDED(dxgiFactory.As(&factory5)))
	{
		if (FAILED(factory5->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING, 
			&allowTearing, sizeof(allowTearing))))
		{
			allowTearing = FALSE;
		}
	}
 
    return allowTearing == TRUE;
}

class Context
{
public:
	bool Init(System::Window& Window)
	{
		mDXGIFactory = CreateFactory();
		mDevice = CreateDevice();

		for (int i = 0; i < ArraySize(mDescriptorSizes); ++i)
		{
			mDescriptorSizes[i] = mDevice->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)i);
		}
		mGraphicsQueue = CreateCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

		DXGI_SWAP_CHAIN_FLAG SwapChainFlag = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		mTearingSupported = CheckTearingSupport(mDXGIFactory);
		if (mTearingSupported)
			SwapChainFlag = (DXGI_SWAP_CHAIN_FLAG)(SwapChainFlag | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
		mSwapChain = CreateSwapChain(Window, SwapChainFlag);

		mRTVDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3);
		UpdateRenderTargetViews();

		if (mTearingSupported)
			mPresentFlags |= DXGI_PRESENT_ALLOW_TEARING;

		mSyncInterval = !mTearingSupported;
		return true;
	}

	void Present()
	{
		VALIDATE(mSwapChain->Present(mSyncInterval, mPresentFlags));
	}

	ComPtr<ID3D12RootSignature> CreateRootSignature(ComPtr<ID3DBlob> RootBlob)
	{
		ComPtr<ID3D12RootSignature> Result;

		VALIDATE(
			mDevice->CreateRootSignature(
				0,
				RootBlob->GetBufferPointer(),
				RootBlob->GetBufferSize(),
				IID_PPV_ARGS(&Result)
			)
		);
		return Result;
	}

	ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32_t NumDescriptors, D3D12_DESCRIPTOR_HEAP_FLAGS Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE	)
	{
		ComPtr<ID3D12DescriptorHeap> Result;
	 
		D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
		Desc.NumDescriptors = NumDescriptors;
		Desc.Type = Type;
		Desc.Flags = Flags;
	 
		VALIDATE(mDevice->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Result)));
	 
		return Result;
	}

	ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE Type)
	{
		ComPtr<ID3D12CommandAllocator> Result;
		VALIDATE(mDevice->CreateCommandAllocator(Type, IID_PPV_ARGS(&Result)));
	 
		return Result;
	}

	ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12CommandAllocator>& CommandAllocator, D3D12_COMMAND_LIST_TYPE Type)
	{
		ComPtr<ID3D12GraphicsCommandList> Result;
		VALIDATE(mDevice->CreateCommandList(0, Type, CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&Result)));

		VALIDATE(Result->Close());
		
		return Result;
	}

	ComPtr<ID3D12Fence> CreateFence()
	{
		ComPtr<ID3D12Fence> Result;
	 
		VALIDATE(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Result)));
	 
		return Result;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandleForBackBuffer(UINT CurrentBackBufferIndex)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			CurrentBackBufferIndex, mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);

		return rtv;
	}

	void Execute(ID3D12CommandList *CmdList)
	{
		ID3D12CommandList* const CommandLists[] = { CmdList };
		mGraphicsQueue->ExecuteCommandLists(ArraySize(CommandLists), CommandLists);
	}


	bool mTearingSupported = false;
	UINT mSyncInterval = 1;
	UINT mPresentFlags = 0;
	ComPtr<ID3D12Device>		mDevice;
	ComPtr<ID3D12CommandQueue>	mGraphicsQueue;
	ComPtr<IDXGISwapChain1>		mSwapChain;
	ComPtr<ID3D12Resource>		BackBuffers[3];

	UINT		mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

private:
	ComPtr<IDXGIFactory4> mDXGIFactory;
	ComPtr<ID3D12DescriptorHeap> mRTVDescriptorHeap;

	ComPtr<IDXGISwapChain1> CreateSwapChain(System::Window Window, DXGI_SWAP_CHAIN_FLAG Flags)
	{
		ComPtr<IDXGISwapChain1> Result;

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = (UINT)Window.mViewport.Width;
		swapChainDesc.Height = (UINT)Window.mViewport.Height;
		swapChainDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
		swapChainDesc.Scaling = DXGI_SCALING_NONE;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 3;
		swapChainDesc.Flags = Flags;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

		VALIDATE(mDXGIFactory->CreateSwapChainForHwnd(mGraphicsQueue.Get(), Window.mHwnd, &swapChainDesc, nullptr, nullptr, &Result));

		return Result;
	}

	void UpdateRenderTargetViews()
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	 
		for (int i = 0; i < 3; ++i)
		{
			ComPtr<ID3D12Resource> backBuffer;
			VALIDATE(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
	 
			mDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
	 
			BackBuffers[i] = backBuffer;
	 
			rtvHandle.Offset(mDescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);
		}
	}

	ComPtr<ID3D12CommandQueue> CreateCommandQueue(D3D12_COMMAND_LIST_TYPE Type)
	{
		ComPtr<ID3D12CommandQueue> Result;

		D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
		QueueDesc.Type = Type;
		QueueDesc.NodeMask = 1;
		mDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Result));

		return Result;
	}

	ComPtr<ID3D12Device> CreateDevice()
	{
		ComPtr<ID3D12Device> Result;

		SIZE_T MaxSize = 0;
		ComPtr<IDXGIAdapter1> Adapter;
	#ifndef TEST_WARP
		for (uint32_t Idx = 0; DXGI_ERROR_NOT_FOUND != mDXGIFactory->EnumAdapters1(Idx, &Adapter); ++Idx)
		{
			DXGI_ADAPTER_DESC1 desc;
			Adapter->GetDesc1(&desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				continue;

			if (desc.DedicatedVideoMemory > MaxSize && SUCCEEDED(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Result))))
			{
				Adapter->GetDesc1(&desc);
				Debug::Print("D3D12-capable hardware found:", desc.Description, desc.DedicatedVideoMemory >> 20);
				MaxSize = desc.DedicatedVideoMemory;
			}
		}
	#endif

		if (Result.Get() == nullptr)
		{
			Debug::Print("Failed to find a hardware adapter.  Falling back to WARP.\n");
			VALIDATE(mDXGIFactory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter)));
			VALIDATE(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Result)));
		}

		return Result;
	}

	ComPtr<IDXGIFactory4> CreateFactory()
	{
		ComPtr<IDXGIFactory4> dxgiFactory;
		UINT FactoryFlags = 0;
	#ifdef DEBUG
		FactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	#endif
		VALIDATE(CreateDXGIFactory2(FactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

		return dxgiFactory;
	}

};

}

