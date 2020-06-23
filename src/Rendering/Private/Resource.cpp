#include "Rendering/Resource.h"
#include "Rendering/Device.h"
#include "Rendering/ResourceStateTracker.h"
#include "Util/Util.h"
#include "External/d3dx12.h"
#include <codecvt>

namespace Rendering
{

CResource::CResource(const String& name)
    : ResourceName(name)
{}

CResource::CResource(const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_CLEAR_VALUE* clearValue, const String& name)
{
    auto device = CDevice::Instance.Get();

    if ( clearValue )
    {
        ClearValue = *clearValue;
    }
    
    CD3DX12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    VALIDATE( device->CreateCommittedResource(
        &HeapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &ClearValue,
        IID_PPV_ARGS(&Resource)
    ) );

    CResourceStateTracker::AddGlobalResourceState(Resource.Get(), D3D12_RESOURCE_STATE_COMMON );

    SetName(name);
}

CResource::CResource(Microsoft::WRL::ComPtr<ID3D12Resource> resource, const String& name)
    : Resource(resource)
{
    SetName(name);
}

CResource::CResource(const CResource& copy)
    : Resource(copy.Resource)
    , ResourceName(copy.ResourceName)
    , ClearValue(copy.ClearValue)
{
}

CResource::CResource(CResource&& copy)
    : Resource(MOVE(copy.Resource))
    , ResourceName(MOVE(copy.ResourceName))
    , ClearValue(copy.ClearValue)
{
}

CResource& CResource::operator=(const CResource& other)
{
    if ( this != &other )
    {
        Resource = other.Resource;
        ResourceName = other.ResourceName;
		ClearValue = other.ClearValue;
    }

    return *this;
}

CResource& CResource::operator=(CResource&& other)
{
    if (this != &other)
    {
        Resource = other.Resource;
        ResourceName = other.ResourceName;
        ClearValue = other.ClearValue;

        other.Resource.Reset();
        other.ResourceName.clear();
    }

    return *this;
}


CResource::~CResource()
{
}

void CResource::SetD3D12Resource(Microsoft::WRL::ComPtr<ID3D12Resource> d3d12Resource, const D3D12_CLEAR_VALUE* clearValue )
{
    Resource = d3d12Resource;
	ClearValue = *clearValue;
    SetName(ResourceName);
}

void CResource::SetName(const String& name)
{
    ResourceName = name;
    if (Resource && !ResourceName.empty())
    {
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		std::wstring wide = converter.from_bytes(name.c_str());
        Resource->SetName(wide.c_str());
    }
}

void CResource::Reset()
{
    Resource.Reset();
}

}