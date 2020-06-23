#include "Rendering/UploadBuffer.h"
#include "Rendering/Device.h"

namespace Rendering
{
	SPage::SPage(size_t sizeInBytes) : PageSize(sizeInBytes) {
		auto device = Rendering::CDevice::Instance.Get();

		CD3DX12_HEAP_PROPERTIES Props(D3D12_HEAP_TYPE_UPLOAD);
		auto BuffDesc = CD3DX12_RESOURCE_DESC::Buffer(PageSize);

		VALIDATE(device->CreateCommittedResource(
			&Props,
			D3D12_HEAP_FLAG_NONE,
			&BuffDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&ResourceHandle)
		));

		GPUPtr = ResourceHandle->GetGPUVirtualAddress();
		ResourceHandle->Map(0, nullptr, &CPUPtr);
	}

	bool SPage::HasSpace(size_t sizeInBytes, size_t alignment) const
	{
		size_t alignedSize = Math::AlignUp(sizeInBytes, alignment);
		size_t alignedOffset = Math::AlignUp(Offset, alignment);

		return alignedOffset + alignedSize <= PageSize;
	}

	SAllocation SPage::Allocate(size_t sizeInBytes, size_t alignment)
	{
		size_t alignedSize = Math::AlignUp(sizeInBytes, alignment);
		Offset = Math::AlignUp(Offset, alignment);

		SAllocation allocation;
		allocation.CPU = static_cast<uint8_t*>(CPUPtr) + Offset;
		allocation.GPU = GPUPtr + Offset;

		Offset += alignedSize;

		return allocation;
	}

	SAllocation CUploadBuffer::Allocate(size_t sizeInBytes, size_t alignment)
	{
		CHECK(sizeInBytes <= PageSize, "UploadBuffer's PageSize was not enough for requested allocation.");

		if (!CurrentPage || !CurrentPage->HasSpace(sizeInBytes, alignment))
		{
			CurrentPage = RequestPage();
		}
	 
		return CurrentPage->Allocate(sizeInBytes, alignment);
	}

	TUniquePtr<SPage> CUploadBuffer::RequestPage()
	{
		if (!AvailablePages.empty())
		{
			auto page = MOVE(AvailablePages.front());
			AvailablePages.erase(AvailablePages.begin());
			return page;
		}

		return TUniquePtr<SPage>(new SPage(PageSize));
	}

	void CUploadBuffer::Reset()
	{
		CurrentPage.reset();

		AvailablePages = MOVE(PagePool);
	 
		for ( auto& page : AvailablePages )
		{
			// Reset the page for new allocations.
			page->Reset();
		}
	}
}
