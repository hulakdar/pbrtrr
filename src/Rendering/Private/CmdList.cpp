#include "Rendering/CmdList.h"

// Transition a resource

namespace Rendering {

void CCmdList::TransitionResource(ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), beforeState, afterState);

    CmdList->ResourceBarrier(1, &barrier);
}

// Clear a render target view.

void CCmdList::ClearRTV(D3D12_CPU_DESCRIPTOR_HANDLE rtv, FLOAT* clearColor)
{
    CmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
}

// Clear the depth of a depth-stencil view.

void CCmdList::ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, FLOAT depth)
{
    CmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
}

// Create a GPU buffer.

void CCmdList::UpdateBufferResource(ID3D12Device2* device, ID3D12Resource** pDestinationResource, ID3D12Resource** pIntermediateResource, size_t numElements, size_t elementSize, const void* bufferData, D3D12_RESOURCE_FLAGS flags)
{
    size_t bufferSize = numElements * elementSize;

    VALIDATE(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(pDestinationResource))
    );

    if (bufferData)
    {
        VALIDATE(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(pIntermediateResource))
        );

        D3D12_SUBRESOURCE_DATA subresourceData = {};
        subresourceData.pData = bufferData;
        subresourceData.RowPitch = bufferSize;
        subresourceData.SlicePitch = subresourceData.RowPitch;

        UpdateSubresources(CmdList.Get(), *pDestinationResource, *pIntermediateResource, 0, 0, 1, &subresourceData);
    }
}

}
