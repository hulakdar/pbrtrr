#include <d3d12.h>

#include "Common.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Containers/ComPtr.h"
#include "Render/Texture.h"
#include "Threading/Mutex.h"

static DXGI_FORMAT GetTypelessFormat(DXGI_FORMAT Format)
{
	switch (Format)
	{
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		return DXGI_FORMAT_R32G32B32A32_TYPELESS;
	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		return DXGI_FORMAT_R32G32B32_TYPELESS;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
		return DXGI_FORMAT_R16G16B16A16_TYPELESS;
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
		return DXGI_FORMAT_R32G32_TYPELESS;
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		return DXGI_FORMAT_R32G8X24_TYPELESS;
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
		return DXGI_FORMAT_R10G10B10A2_TYPELESS;
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
		return DXGI_FORMAT_R8G8B8A8_TYPELESS;
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
		return DXGI_FORMAT_R16G16_TYPELESS;
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
		return DXGI_FORMAT_R32_TYPELESS;
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
		return DXGI_FORMAT_R24G8_TYPELESS;
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
		return DXGI_FORMAT_R8G8_TYPELESS;
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
		return DXGI_FORMAT_R16_TYPELESS;
	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
		return DXGI_FORMAT_R8_TYPELESS;
	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
		return DXGI_FORMAT_BC1_TYPELESS;
	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
		return DXGI_FORMAT_BC2_TYPELESS;
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
		return DXGI_FORMAT_BC3_TYPELESS;
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		return DXGI_FORMAT_BC4_TYPELESS;
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
		return DXGI_FORMAT_BC5_TYPELESS;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8A8_TYPELESS;
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8X8_TYPELESS;
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
		return DXGI_FORMAT_BC6H_TYPELESS;
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return DXGI_FORMAT_BC7_TYPELESS;
	default:
		DEBUG_BREAK();
		return Format;
	}
}

static TMap<u64, TArray<TexID>> gFreeTextures;
static TMap<u32, TArray<TextureData>> gResourceViews;

struct TransientTextureDescription {
	u16 Width; // 64k max
	u16 Height; // 64k max
	u8 Format;
	u8 Flags;
	u8 NumMips;

	u64 Hash()
	{
		return (u64)Width | ((u64)Height << 16) | ((u64)Format << 24) | ((u64)Flags << 32) | ((u64)NumMips << 40);
	}
};

void GetTransientTexture(TextureData& TexData, D3D12_RESOURCE_FLAGS Flags, D3D12_RESOURCE_STATES InitialState, D3D12_CLEAR_VALUE* ClearValue)
{
	TransientTextureDescription Desc = {};

	Desc.Width  = TexData.Width;
	Desc.Height = TexData.Height;
	Desc.Format = (u8)GetTypelessFormat((DXGI_FORMAT)TexData.Format);
	Desc.Flags =  (u8)Flags;
	Desc.NumMips = TexData.NumMips;

	TArray<TexID>& CompatibleResources = gFreeTextures[Desc.Hash()];
	TexID Result;
	if (!CompatibleResources.empty())
	{
		Result = CompatibleResources.back();
		CompatibleResources.pop_back();
	}
	else
	{
		CreateResourceForTexture(TexData, Flags, InitialState, ClearValue);
		Result = TexData.ID;
	}

	TArray<TextureData>& ResourceViews = gResourceViews[Result.Value];
	u8 Index = TexData.Format - Desc.Format;
	if (Index < ResourceViews.size())
	{
		TexData = ResourceViews[Index];
		return;
	}
	else
	{
		ResourceViews.resize(Index + 1);
	}

	TextureData& View = ResourceViews[Index];
	View.ID     = Result;
	View.Width  = Desc.Width;
	View.Height = Desc.Height;
	View.Format = Desc.Format + Index;
	View.Flags  = (u8)Desc.Flags;
	View.NumMips = Desc.NumMips;
	if (Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET && View.RTV == MAXWORD)
	{
		CreateRTV(View);
	}
	if (Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL && View.DSV == MAXWORD)
	{
		CreateDSV(View);
	}
	if (Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS && View.UAV == MAXWORD)
	{
		CreateUAV(View);
	}
	if (!(Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) && View.SRV == MAXWORD)
	{
		CreateSRV(View, false);
	}
	TexData = View;
}

void DiscardTransientTexture(TextureData& TexData)
{
	TransientTextureDescription Desc = {};
	Desc.Width  = TexData.Width;
	Desc.Height = TexData.Height;
	Desc.Format = (u8)GetTypelessFormat((DXGI_FORMAT)TexData.Format);
	Desc.Flags =  TexData.Flags;
	Desc.NumMips = TexData.NumMips;
	TArray<TexID>& CompatibleResources = gFreeTextures[Desc.Hash()];
	CompatibleResources.push_back(TexData.ID);
}

static const u64 PoolPageSize = 1_kb;
static const u64 MaxPooledSize = PoolPageSize * 64;

static TracyLockable(Mutex, gPoolsLock);

struct PooledPageResource
{
	TComPtr<ID3D12Resource> Resource;
	u8* CPUPtr;
};

static TDeque<PooledPageResource> gBufferPools[3];
static TDeque<u64> gPoolAvailabilityMasks[3];

static TracyLockable(Mutex, gLargeBuffersLock);
static TArray<TComPtr<ID3D12Resource>> gLargeBuffers;

void GetTransientBuffer(PooledBuffer& Result, u64 Size, BufferType Type)
{
	CHECK(Size, "wtf?");
	Result.Size = (u32)Size;
	Result.Type = (u8)Type;
	if (Size >= MaxPooledSize - PoolPageSize)
	{
		ZoneScopedN("Allocate large buffer");
		TComPtr<ID3D12Resource> LargeBuffer = CreateBuffer(Size, Type);
		Result.Resource = LargeBuffer.Get();
		Result.Resource->Map(0, nullptr, (void**)&Result.CPUPtr);
		CHECK(Result.CPUPtr);
		gLargeBuffersLock.lock();
		gLargeBuffers.push_back(MOVE(LargeBuffer));
		gLargeBuffersLock.unlock();
		return;
	}
	u64 PoolIndex = Type;

	auto& BufferPool = gBufferPools[PoolIndex];
	auto& PoolAvailabilityMask = gPoolAvailabilityMasks[PoolIndex];

	u64 MaskWidth = (Size + PoolPageSize - 1) / PoolPageSize;
	u64 Mask = ~0ULL >> (64 - MaskWidth);

	for (u64 i = 0; i < PoolAvailabilityMask.size(); ++i)
	{
		u64 AvailabilityMask = PoolAvailabilityMask[i];
		u64 AvailabilityMaskForBitscan = ~AvailabilityMask;
		if (_mm_popcnt_u64(AvailabilityMaskForBitscan) < MaskWidth)
		{
			continue;
		}
		unsigned long BitIndex = 0;
		bool Continue = _BitScanForward64(&BitIndex, AvailabilityMaskForBitscan);
		while (Continue && __popcnt64(AvailabilityMaskForBitscan) >= MaskWidth)
		{
			u64 ShiftedMask = (Mask << BitIndex);
			u64 Test = (ShiftedMask & AvailabilityMaskForBitscan);
			bool TestPassed = Test == ShiftedMask;
			if (TestPassed)
			{
				CHECK((ShiftedMask & AvailabilityMask) == 0);
				u64 OldValue = _InterlockedCompareExchange(&PoolAvailabilityMask[i], ShiftedMask | AvailabilityMask, AvailabilityMask);
				bool OldValueCoresponds = OldValue == AvailabilityMask;
				if (OldValueCoresponds)
				{
					Result.Resource = BufferPool[i].Resource.Get();
					Result.Offset = (u16)BitIndex * PoolPageSize;
					Result.CPUPtr = BufferPool[i].CPUPtr + Result.Offset;
					return;
				}
			}
			AvailabilityMask = PoolAvailabilityMask[i];
			u64 HowMuch = _tzcnt_u64(AvailabilityMask >> BitIndex);
			AvailabilityMaskForBitscan = ~(AvailabilityMask | (~0ULL >> (64 - BitIndex - HowMuch)));
			Continue = _BitScanForward64(&BitIndex, AvailabilityMaskForBitscan);
		}
	}
	TComPtr<ID3D12Resource> Resource = CreateBuffer(MaxPooledSize, Type);
	Result.Resource = Resource.Get();
	Resource->Map(0, nullptr, (void**)&Result.CPUPtr);

	CHECK(Result.CPUPtr);

	gPoolsLock.lock();
	PooledPageResource& R = BufferPool.push_back();
	R.Resource = MOVE(Resource);
	R.CPUPtr = Result.CPUPtr;
	PoolAvailabilityMask.push_back(Mask);
	gPoolsLock.unlock();
}

void DiscardTransientBuffer(PooledBuffer& Buffer)
{
	if (Buffer.Size < MaxPooledSize - PoolPageSize)
	{
		u8 PoolIndex = Buffer.Type;

		auto& BufferPool = gBufferPools[PoolIndex];
		auto& PoolAvailabilityMask = gPoolAvailabilityMasks[PoolIndex];

		u64 MaskWidth = (Buffer.Size + PoolPageSize - 1) / PoolPageSize;
		u64 MaskShift = Buffer.Offset / PoolPageSize;
		u64 Mask = ~0ULL >> (64 - MaskWidth);
		auto It = std::find_if(BufferPool.begin(), BufferPool.end(),
			[&Buffer](const PooledPageResource& Ptr) {
				return Ptr.Resource.Get() == Buffer.Get();
			}
		);
		CHECK(It != BufferPool.end(), "Unknown transient buffer");
		_InterlockedAnd(&PoolAvailabilityMask[It - BufferPool.begin()], ~(Mask << MaskShift));
	}
	else
	{
		gLargeBuffersLock.lock();
		auto It = std::find_if(gLargeBuffers.begin(), gLargeBuffers.end(),
			[&Buffer](const TComPtr<ID3D12Resource>& Ptr) {
				return Ptr.Get() == Buffer.Get();
			}
		);
		CHECK(It != gLargeBuffers.end(), "Unknown large buffer");
		gLargeBuffers.erase_unsorted(It);
		gLargeBuffersLock.unlock();
	}
	Buffer.Resource = nullptr;
}

