#include "Rendering/ResourceStateTracker.h"
#include "Rendering/CmdList.h"
#include <External\d3dx12.h>
#include <Util\Util.h>

namespace Rendering
{
	// Static definitions.
	Mutex CResourceStateTracker::GlobalMutex;
	bool CResourceStateTracker::IsLocked = false;
	CResourceStateTracker::ResourceStateMap CResourceStateTracker::GlobalResourceState;

	void CResourceStateTracker::ResourceBarrier(const D3D12_RESOURCE_BARRIER& barrier)
	{
		if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
		{
			const D3D12_RESOURCE_TRANSITION_BARRIER& transitionBarrier = barrier.Transition;
			// First check if there is already a known "final" state for the given resource.
			// If there is, the resource has been used on the command list before and
			// already has a known state within the command list execution.
			const auto iter = FinalResourceState.find(transitionBarrier.pResource);
			if (iter != FinalResourceState.end())
			{
				auto& resourceState = iter->second;
				// If the known final state of the resource is different...
				if ( transitionBarrier.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
					 !resourceState.SubresourceState.empty() )
				{
					// First transition all of the subresources if they are different than the StateAfter.
					for ( auto subresourceState : resourceState.SubresourceState )
					{
						if ( transitionBarrier.StateAfter != subresourceState.second )
						{
							D3D12_RESOURCE_BARRIER newBarrier = barrier;
							newBarrier.Transition.Subresource = subresourceState.first;
							newBarrier.Transition.StateBefore = subresourceState.second;
							ResourceBarriers.push_back( newBarrier );
						}
					}
				}
				else
				{
					auto finalState = resourceState.GetSubresourceState( transitionBarrier.Subresource );
					if ( transitionBarrier.StateAfter != finalState )
					{
						// Push a new transition barrier with the correct before state.
						D3D12_RESOURCE_BARRIER newBarrier = barrier;
						newBarrier.Transition.StateBefore = finalState;
						ResourceBarriers.push_back( newBarrier );
					}
				}
			}
			else // In this case, the resource is being used on the command list for the first time. 
			{
				// Add a pending barrier. The pending barriers will be resolved
				// before the command list is executed on the command queue.
				PendingResourceBarriers.push_back(barrier);
			}
			// Push the final known state (possibly replacing the previously known state for the subresource).
			FinalResourceState[transitionBarrier.pResource].SetSubresourceState(transitionBarrier.Subresource, transitionBarrier.StateAfter);
		}
		else
		{
			// Just push non-transition barriers to the resource barriers array.
			ResourceBarriers.push_back(barrier);
		}
	}

	void CResourceStateTracker::TransitionResource( ID3D12Resource* resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource )
	{
		if ( resource )
		{
			ResourceBarrier( CD3DX12_RESOURCE_BARRIER::Transition( resource, D3D12_RESOURCE_STATE_COMMON, stateAfter, subResource ) );
		}
	}
	 
#if 0
	void CResourceStateTracker::TransitionResource( const CResource& resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource )
	{
		TransitionResource( resource.Get(), stateAfter, subResource );
	}

	void CResourceStateTracker::UAVBarrier(const CResource* resource )
	{
		ID3D12Resource* pResource = resource != nullptr ? resource->GetD3D12Resource().Get() : nullptr;
	 
		ResourceBarrier(CD3DX12_RESOURCE_BARRIER::UAV(pResource));
	}

	void CResourceStateTracker::AliasBarrier(const CResource* resourceBefore, const CResource* resourceAfter)
	{
		ID3D12Resource* pResourceBefore = resourceBefore != nullptr ? resourceBefore->Get() : nullptr;
		ID3D12Resource* pResourceAfter = resourceAfter != nullptr ? resourceAfter->Get() : nullptr;
	 
		ResourceBarrier(CD3DX12_RESOURCE_BARRIER::Aliasing(pResourceBefore, pResourceAfter));
	}
#endif

	void CResourceStateTracker::FlushResourceBarriers(CCmdList& commandList)
	{
		UINT numBarriers = static_cast<UINT>(ResourceBarriers.size());
		if (numBarriers > 0 )
		{
			auto d3d12CommandList = commandList.Get();
			d3d12CommandList->ResourceBarrier(numBarriers, ResourceBarriers.data());
			ResourceBarriers.clear();
		}
	}
	uint32_t CResourceStateTracker::FlushPendingResourceBarriers(CCmdList& commandList)
	{
		CHECK(IsLocked, "Global state should be locked when calling this function");

		// Resolve the pending resource barriers by checking the global state of the 
		// (sub)resources. Add barriers if the pending state and the global state do
		//  not match.
		TArray<D3D12_RESOURCE_BARRIER> resourceBarriers;
		// Reserve enough space (worst-case, all pending barriers).
		resourceBarriers.reserve(PendingResourceBarriers.size());
		for (auto pendingBarrier : PendingResourceBarriers)
		{
			if (pendingBarrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)  // Only transition barriers should be pending...
			{
				auto pendingTransition = pendingBarrier.Transition;
				const auto& iter = GlobalResourceState.find(pendingTransition.pResource);
				if (iter != GlobalResourceState.end())
				{
					// If all subresources are being transitioned, and there are multiple
					// subresources of the resource that are in a different state...
					auto& resourceState = iter->second;
					if ( pendingTransition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
						 !resourceState.SubresourceState.empty() )
					{
						// Transition all subresources
						for ( auto subresourceState : resourceState.SubresourceState )
						{
							if ( pendingTransition.StateAfter != subresourceState.second )
							{
								D3D12_RESOURCE_BARRIER newBarrier = pendingBarrier;
								newBarrier.Transition.Subresource = subresourceState.first;
								newBarrier.Transition.StateBefore = subresourceState.second;
								resourceBarriers.push_back( newBarrier );
							}
						}
					}
					else
					{
						// No (sub)resources need to be transitioned. Just add a single transition barrier (if needed).
						auto globalState = ( iter->second ).GetSubresourceState( pendingTransition.Subresource );
						if ( pendingTransition.StateAfter != globalState )
						{
							// Fix-up the before state based on current global state of the resource.
							pendingBarrier.Transition.StateBefore = globalState;
							resourceBarriers.push_back( pendingBarrier );
						}
					}
				}
			}
		}
	 
		UINT numBarriers = static_cast<UINT>(resourceBarriers.size());
		if (numBarriers > 0 )
		{
			auto d3d12CommandList = commandList.Get();
			d3d12CommandList->ResourceBarrier(numBarriers, resourceBarriers.data());
		}
	 
		PendingResourceBarriers.clear();
	 
		return numBarriers;
	}

	void CResourceStateTracker::CommitFinalResourceStates()
	{
		CHECK(IsLocked, "Global state should be locked when calling this function");
	 
		// Commit final resource states to the global resource state array (map).
		for (const auto& resourceState : FinalResourceState)
		{
			GlobalResourceState[resourceState.first] = resourceState.second;
		}
	 
		FinalResourceState.clear();
	}

	void CResourceStateTracker::Reset()
	{
		// Reset the pending, current, and final resource states.
		PendingResourceBarriers.clear();
		ResourceBarriers.clear();
		FinalResourceState.clear();
	}

	void CResourceStateTracker::Lock()
	{
		GlobalMutex.Lock();
		IsLocked = true;
	}

	void CResourceStateTracker::Unlock()
	{
		IsLocked = false;
		GlobalMutex.Unlock();
	}

	void CResourceStateTracker::AddGlobalResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
	{
		if ( resource != nullptr )
		{
			ScopedLock lock(GlobalMutex);
			GlobalResourceState[resource].SetSubresourceState(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state);
		}
	}

	void CResourceStateTracker::RemoveGlobalResourceState(ID3D12Resource* resource)
	{
		if ( resource != nullptr )
		{
			ScopedLock lock(GlobalMutex);
			GlobalResourceState.erase(resource);
		}
	}
}
