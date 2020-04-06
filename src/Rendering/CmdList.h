#pragma once

#include "Util/Util.h"

#include "external/d3dx12.h"

class CCmdList
{
	Ptr<ID3D12GraphicsCommandList2> CmdList;

	    // Transition a resource
    void TransitionResource(Ptr<ID3D12Resource> resource,
        D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);
 
    // Clear a render target view.
    void ClearRTV(D3D12_CPU_DESCRIPTOR_HANDLE rtv, FLOAT* clearColor);
 
    // Clear the depth of a depth-stencil view.
    void ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, FLOAT depth = 1.0f );
 
    // Create a GPU buffer.
    void UpdateBufferResource(ID3D12Resource** pDestinationResource, ID3D12Resource** pIntermediateResource,
        size_t numElements, size_t elementSize, const void* bufferData, 
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE );
 
};