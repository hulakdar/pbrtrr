#define WIN32_LEAN_AND_MEAN
#include <d3dcompiler.h>

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

	Rendering::CDevice& Device = Rendering::CDevice::Instance;
	auto WindowSize = Device.Window.Size;

	Device.Init();

	uint64_t FenceValues[Rendering::CDevice::BufferCount];


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
		0, 2, 1, 0, 3, 2,
		4, 5, 6, 4, 6, 7,
		4, 1, 5, 4, 0, 1,
		3, 6, 2, 3, 7, 6,
		1, 6, 5, 1, 2, 6,
		4, 3, 0, 4, 7, 3
	};

	auto CmdList = Device.CmdQueue.GetCommandList();
	ComPtr<ID3D12Resource> VertexBuffer;
	ComPtr<ID3D12Resource> intermediateVertexBuffer;
	CmdList.UpdateBufferResource(Device.Get(), &VertexBuffer, &intermediateVertexBuffer,
		ArraySize(Vertices), sizeof(VertexPosColor), Vertices);

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
	VertexBufferView.BufferLocation = VertexBuffer->GetGPUVirtualAddress();
	VertexBufferView.SizeInBytes = sizeof(Vertices);
	VertexBufferView.StrideInBytes = sizeof(VertexPosColor);

	ComPtr<ID3D12Resource> IndexBuffer;
	ComPtr<ID3D12Resource> intermediateIndexBuffer;
	CmdList.UpdateBufferResource(Device.Get(), &IndexBuffer, &intermediateIndexBuffer,
		ArraySize(Indicies), sizeof(WORD), Indicies);
	uint64_t FenceValue = Device.CmdQueue.Execute(CmdList);

	D3D12_INDEX_BUFFER_VIEW IndexBufferView;

	IndexBufferView.BufferLocation = IndexBuffer->GetGPUVirtualAddress();
	IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
	IndexBufferView.SizeInBytes = sizeof(Indicies);

	
	// Load the vertex shader.
	ComPtr<ID3DBlob> vertexShaderBlob;
	VALIDATE(D3DReadFileToBlob(L"content/cooked/SimpleVS.cso", &vertexShaderBlob));
	 
	// Load the pixel shader.
	ComPtr<ID3DBlob> pixelShaderBlob;
	VALIDATE(D3DReadFileToBlob(L"content/cooked/SimplePS.cso", &pixelShaderBlob));


	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	ComPtr<ID3D12RootSignature> RootSignature;
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		if (FAILED(Device.Get()->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		// A single 32-bit constant root parameter that is used by the vertex shader.
		CD3DX12_ROOT_PARAMETER1 rootParameters[1] = {};
		rootParameters[0].InitAsConstants(sizeof(Matrix) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
		rootSignatureDescription.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

		// Serialize the root signature.
		ComPtr<ID3DBlob> rootSignatureBlob;
		ComPtr<ID3DBlob> errorBlob;
		VALIDATE(D3DX12SerializeVersionedRootSignature(&rootSignatureDescription,
			featureData.HighestVersion, &rootSignatureBlob, &errorBlob));

		// Create the root signature.
		VALIDATE(Device.Get()->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
			rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&RootSignature)));
	}

	ComPtr<ID3D12PipelineState> PipelineState;
	{
		D3D12_RT_FORMAT_ARRAY rtvFormats = {};
		rtvFormats.NumRenderTargets = 1;
		rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

		struct PipelineStateStream
		{
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
			CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
			CD3DX12_PIPELINE_STATE_STREAM_VS VS;
			CD3DX12_PIPELINE_STATE_STREAM_PS PS;
			CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
			CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		} pipelineStateStream;

		pipelineStateStream.pRootSignature = RootSignature.Get();
		pipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
		pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
		pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
		pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pipelineStateStream.RTVFormats = rtvFormats;

		D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
			sizeof(PipelineStateStream), &pipelineStateStream
		};
		VALIDATE(Device.Get()->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&PipelineState)));
	}

	ComPtr<ID3D12DescriptorHeap> DSVHeap = Device.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
	ComPtr<ID3D12Resource> DepthBuffer;
	{
		D3D12_CLEAR_VALUE optimizedClearValue = {};
		optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
		optimizedClearValue.DepthStencil = { 1.0f, 0 };

		auto HeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto TexDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, WindowSize.x, WindowSize.y,
			1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

		VALIDATE(Device.Get()->CreateCommittedResource(
			&HeapProps,
			D3D12_HEAP_FLAG_NONE,
			&TexDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&optimizedClearValue,
			IID_PPV_ARGS(&DepthBuffer)
		));

        D3D12_DEPTH_STENCIL_VIEW_DESC Desc = {};
        Desc.Format = DXGI_FORMAT_D32_FLOAT;
        Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
 
        Device.Get()->CreateDepthStencilView(DepthBuffer.Get(), &Desc, DSVHeap->GetCPUDescriptorHandleForHeapStart());
	}

	Device.CmdQueue.WaitForFenceValue(FenceValue);

    //Main message loop
    while(!glfwWindowShouldClose(Device.Window.Handle))
	{
		Device.Window.Update();

		double TotalTime = glfwGetTime();

		Vector3 rotationAxis(0, 1, 1);
		Matrix Model = Matrix::CreateFromAxisAngle(rotationAxis, (float)TotalTime);

		Vector3 eyePosition(0, 0, -5);
		Vector3 focusPoint(0, 0, 0);
		Vector3 upDirection(0, 1, 0);
		Matrix View = Matrix::CreateLookAt(eyePosition, focusPoint, upDirection);

		float aspectRatio = float(WindowSize.x) / float(WindowSize.y);
		Matrix Projection = Matrix::CreatePerspective(0.2, 0.2, 0.1f, 100.0f);
		Projection = Matrix::CreatePerspectiveFieldOfView(Math::Radians(90.f), aspectRatio, 0.1, 100.0f);

		CmdList = Device.CmdQueue.GetCommandList();

		UINT currentBackBufferIndex = Device.CurrentBackBufferIndex;
		auto backBuffer = Device.BackBuffers[currentBackBufferIndex];
		auto rtv = Device.GetCurrentRTV();
		auto dsv = DSVHeap->GetCPUDescriptorHandleForHeapStart();

		{
			CmdList.TransitionResource(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		 
			FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
		 
			CmdList.ClearRTV(rtv, clearColor);
			CmdList.ClearDepth(dsv);
		}

		CmdList.Get()->SetPipelineState(PipelineState.Get());
		CmdList.Get()->SetGraphicsRootSignature(RootSignature.Get());

		CmdList.Get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		CmdList.Get()->IASetVertexBuffers(0, 1, &VertexBufferView);
		CmdList.Get()->IASetIndexBuffer(&IndexBufferView);

		CmdList.Get()->RSSetViewports(1, &Device.Window.Viewport);
		CmdList.Get()->RSSetScissorRects(1, &Device.Window.ScissorRect);

		CmdList.Get()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

		Matrix MVP = Model * View;
		MVP = MVP * Projection;
		CmdList.Get()->SetGraphicsRoot32BitConstants(0, sizeof(Matrix) / 4, &MVP, 0);

		CmdList.Get()->DrawIndexedInstanced(ArraySize(Indicies), 1, 0, 0, 0);

		// Present
		{
			CmdList.TransitionResource(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			FenceValues[currentBackBufferIndex] = Device.CmdQueue.Execute(CmdList);
			Device.Present();
			Device.CmdQueue.WaitForFenceValue(FenceValues[currentBackBufferIndex]);
		}

		Device.FrameCount++;
    }

	Device.CmdQueue.Flush();

	glfwTerminate();
}

