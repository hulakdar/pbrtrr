#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winnt.h>
#include <iostream>
#include <wrl.h>
#include <shellapi.h>

#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <algorithm>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <d3dcompiler.h>
#include "external/d3dx12.h"
#include "Util/Util.h"
#include "Rendering/Device.h"

int main(void)
{
	EnableDebugLayer();

	assert(DirectX::XMVerifyCPUSupport());

	Rendering::CDevice Device;

	Device.Init();

	uint64_t FenceValues[Rendering::CDevice::BufferCount];

	Ptr<ID3D12DescriptorHeap> RTVHeap = Device.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 32);
	Ptr<ID3D12DescriptorHeap> DSVHeap = Device.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 32);	

	Device.UpdateBufferViews(RTVHeap, DSVHeap);

	Ptr<ID3D12Resource> VertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW VertexBufferView;

	Ptr<ID3D12Resource> IndexBuffer;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView;

	Ptr<ID3D12RootSignature> RootSignature;
	Ptr<ID3D12PipelineState> PipelineState;

	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc;
		Desc.BlendState = 

		Device.Get()->CreateGraphicsPipelineState(&Desc, IID_PPV_ARGS(&PipelineState));
	}

	float FoV;
    Matrix ModelMatrix;
    Matrix ViewMatrix;
    Matrix ProjectionMatrix;

	// Vertex data for a colored cube.
	struct VertexPosColor
	{
		Vector3 Position;
		Vector3 Color;
	};

	const VertexPosColor Vertices[] = {
		{ Vector3(-1.0f, -1.0f, -1.0f), Vector3(0.0f, 0.0f, 0.0f) }, // 0
		{ Vector3(-1.0f,  1.0f, -1.0f), Vector3(0.0f, 1.0f, 0.0f) }, // 1
		{ Vector3( 1.0f,  1.0f, -1.0f), Vector3(1.0f, 1.0f, 0.0f) }, // 2
		{ Vector3( 1.0f, -1.0f, -1.0f), Vector3(1.0f, 0.0f, 0.0f) }, // 3
		{ Vector3(-1.0f, -1.0f,  1.0f), Vector3(0.0f, 0.0f, 1.0f) }, // 4
		{ Vector3(-1.0f,  1.0f,  1.0f), Vector3(0.0f, 1.0f, 1.0f) }, // 5
		{ Vector3( 1.0f,  1.0f,  1.0f), Vector3(1.0f, 1.0f, 1.0f) }, // 6
		{ Vector3( 1.0f, -1.0f,  1.0f), Vector3(1.0f, 0.0f, 1.0f) }  // 7
	};

	const WORD Indicies[] =
	{
		0, 1, 2, 0, 2, 3,
		4, 6, 5, 4, 7, 6,
		4, 5, 1, 4, 1, 0,
		3, 2, 6, 3, 6, 7,
		1, 5, 6, 1, 6, 2,
		4, 0, 3, 4, 3, 7
	};

    //Main message loop
    while(!glfwWindowShouldClose(Device.Window.Handle))
	{
		Device.Window.Update();
    }

	glfwTerminate();
}