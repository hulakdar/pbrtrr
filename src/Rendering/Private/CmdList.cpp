#include "Rendering/CmdList.h"
#include "Rendering/Resource.h"
#include "Rendering/UploadBuffer.h"
#include "Rendering/DynamicDescriptorHeap.h"
#include "Util/Util.h"


namespace Rendering {

// Transition a resource
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

	CD3DX12_HEAP_PROPERTIES ResourceHeapProps(D3D12_HEAP_TYPE_DEFAULT);
	auto ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags);

    VALIDATE(device->CreateCommittedResource(
        &ResourceHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &ResourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(pDestinationResource))
    );

    if (bufferData)
    {
        CD3DX12_HEAP_PROPERTIES SubresourceHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        auto SubresourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

        VALIDATE(device->CreateCommittedResource(
            &SubresourceHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &SubresourceDesc,
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

void CCmdList::TransitionBarrier( const CResource& resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource, bool flushBarriers )
{
    auto d3d12Resource = resource.Get();
    if ( d3d12Resource )
    {
        // The "before" state is not important. It will be resolved by the resource state tracker.
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition( d3d12Resource, D3D12_RESOURCE_STATE_COMMON, stateAfter, subResource );
        ResourceStateTracker->ResourceBarrier( barrier );
    }
 
    if ( flushBarriers )
    {
        FlushResourceBarriers();
    }
}

void CCmdList::CopyResource( CResource& dstRes, const CResource& srcRes )
{
    TransitionBarrier( dstRes, D3D12_RESOURCE_STATE_COPY_DEST );
    TransitionBarrier( srcRes, D3D12_RESOURCE_STATE_COPY_SOURCE );
 
    FlushResourceBarriers();
 
    CmdList->CopyResource( dstRes.Get(), srcRes.Get() );
 
    TrackResource(dstRes);
    TrackResource(srcRes);
}

void CCmdList::SetGraphicsDynamicConstantBuffer( uint32_t rootParameterIndex, size_t sizeInBytes, const void* bufferData )
{
    // Constant buffers must be 256-byte aligned.
	auto heapAllococation = UploadBuffer->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	memcpy(heapAllococation.CPU, bufferData, sizeInBytes);
 
	CmdList->SetGraphicsRootConstantBufferView(rootParameterIndex, heapAllococation.GPU);
}

void CCmdList::SetShaderResourceView( uint32_t rootParameterIndex,
                                      uint32_t descriptorOffset,
                                      const CResource& resource,
                                      D3D12_RESOURCE_STATES stateAfter,
                                      UINT firstSubresource,
                                      UINT numSubresources,
                                      const D3D12_SHADER_RESOURCE_VIEW_DESC* srv)
{
    if (numSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        for (uint32_t i = 0; i < numSubresources; ++i)
        {
            TransitionBarrier(resource, stateAfter, firstSubresource + i);
        }
    }
    else
    {
        TransitionBarrier(resource, stateAfter);
    }
 
    DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors( rootParameterIndex, descriptorOffset, 1, resource.GetShaderResourceView( srv ) );
 
    TrackResource(resource);
}

void CCmdList::Draw( uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance )
{
    FlushResourceBarriers();
 
    for ( int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i )
    {
        DynamicDescriptorHeap[i]->CommitStagedDescriptorsForDraw( *this );
    }
 
    CmdList->DrawInstanced( vertexCount, instanceCount, startVertex, startInstance );
}

}
