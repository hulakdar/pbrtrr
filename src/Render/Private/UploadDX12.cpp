#include <tracy/TracyD3D12.hpp>
#include <tracy/Tracy.hpp>

#include "Containers/Array.generated.h"
#include "Render/TransientResourcesPool.generated.h"

#include <Threading/Mutex.generated.h>

#include <Render/CommandListPool.generated.h>
#include <Render/RenderDX12.generated.h>

static D3D12CmdList  gUploadCmdList;
static TracyD3D12Ctx gCopyProfilingCtx;

static TArray<PooledBuffer> gUploadBuffers;
static TArray<D3D12_RESOURCE_BARRIER> gUploadTransitions;

static TracyLockable(Mutex, gUploadMutex);

void InitUpload()
{
	gUploadCmdList = GetCommandList(D3D12_COMMAND_LIST_TYPE_COPY, L"Upload cmd list");

	gCopyProfilingCtx = TracyD3D12Context(GetGraphicsDevice(), GetGPUQueue(D3D12_COMMAND_LIST_TYPE_COPY));

	TracyD3D12ContextName(gCopyProfilingCtx, "Copy", 4);
	TracyD3D12NewFrame(gCopyProfilingCtx);
}

void UploadOnPresent()
{
	ScopedLock Lock(gUploadMutex);
	TracyD3D12Collect(gCopyProfilingCtx);
	TracyD3D12NewFrame(gCopyProfilingCtx);
}

void UploadTextureData(TextureData& TexData, const u8 *RawData, u32 RawDataSize)
{
	//ZoneScoped;

	D3D12_SUBRESOURCE_DATA SrcData = {};
	SrcData.pData = RawData;

	if (!IsBlockCompressedFormat((DXGI_FORMAT)TexData.Format))
	{
		u32 Components = ComponentCountFromFormat((DXGI_FORMAT)TexData.Format);
		SrcData.RowPitch = TexData.Width * Components;
		SrcData.SlicePitch = TexData.Width * TexData.Height * Components;
	}
	else
	{
		u32 BlockSize = BlockSizeFromFormat((DXGI_FORMAT)TexData.Format);
		SrcData.RowPitch = std::max(1, ((TexData.Width + 3) / 4)) * BlockSize;
		SrcData.SlicePitch = RawDataSize;
	}

	ID3D12Resource* Resource = GetTextureResource(TexData.ID);
	D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		Resource,
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);

	if (gDeviceCaps.CacheCoherentUMA)
	{
		Resource->Map(0, nullptr, nullptr);
		Resource->WriteToSubresource(0, nullptr, RawData, (u32)SrcData.RowPitch, RawDataSize);
		Resource->Unmap(0, nullptr);

		ScopedLock Lock(gUploadMutex);
		gUploadTransitions.push_back(Barrier);
		return;
	}
	PooledBuffer TextureUploadBuffer;

	u64 UploadBufferSize = GetRequiredIntermediateSize(Resource, 0, 1);
	GetTransientBuffer(TextureUploadBuffer, UploadBufferSize, BUFFER_UPLOAD);

	ScopedLock Lock(gUploadMutex);
	TracyD3D12Zone(gCopyProfilingCtx, gUploadCmdList.Get(), "Upload texture data");
	gUploadBuffers.push_back(TextureUploadBuffer);
	UpdateSubresources<15>(gUploadCmdList.Get(), Resource, TextureUploadBuffer.Get(), TextureUploadBuffer.Offset, 0, 1, &SrcData);
	gUploadTransitions.push_back(Barrier);
}

void FlushUpload()
{
	ZoneScoped;

	if (!gUploadTransitions.empty())
	{
		TArray<D3D12_RESOURCE_BARRIER> LocalBarriers;
		TArray<PooledBuffer> LocalBuffers;
		D3D12CmdList LocalCmdList = GetCommandList(D3D12_COMMAND_LIST_TYPE_COPY, L"Upload cmdlist");

		{
			ScopedLock Lock(gUploadMutex);
			LocalBarriers = MOVE(gUploadTransitions);
			LocalBuffers = MOVE(gUploadBuffers);
			std::swap(LocalCmdList, gUploadCmdList);
		}

		Submit(LocalCmdList);

		TicketGPU UploadDone = Signal(LocalCmdList.Type);

		EnqueueDelayedWork(
			[
				Transitions = MOVE(LocalBarriers),
				Buffers = MOVE(LocalBuffers)
			]() mutable {
			auto TransitionsCommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Resource transitions");
			TransitionsCommandList->ResourceBarrier((u32)Transitions.size(), Transitions.data());
			Submit(TransitionsCommandList);
			for (auto& Buffer : Buffers)
			{
				DiscardTransientBuffer(Buffer);
			}
		}, UploadDone);
	}
	if (gDirectStorageNeedsFlush)
	{
		gDirectStorageQueue->EnqueueSignal(gFences[D3D12_COMMAND_LIST_TYPE_BUNDLE].Get(), gFenceValues[D3D12_COMMAND_LIST_TYPE_BUNDLE].fetch_add(1, std::memory_order_relaxed));
		gDirectStorageQueue->EnqueueStatus(gDirectStorageStatusArray, StatusArrayIndex++);
		gDirectStorageQueue->Submit();
		gDirectStorageNeedsFlush = false;
	}
}


void UploadBufferData(ID3D12Resource* Destination, u64 Size, D3D12_RESOURCE_STATES TargetState, TFunction<void(void*, u64)> UploadFunction)
{
	D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		Destination,
		D3D12_RESOURCE_STATE_COMMON,
		TargetState
	);

	if (gDeviceCaps.CacheCoherentUMA)
	{
		void* Address = nullptr;
		Destination->Map(0, nullptr, &Address);
		UploadFunction(Address, Size);
		Destination->Unmap(0, nullptr);

		ScopedLock Lock(gUploadMutex);
		gUploadTransitions.push_back(Barrier);
		return;
	}
	PooledBuffer UploadBuffer;
	GetTransientBuffer(UploadBuffer, Size, BUFFER_UPLOAD);
	D3D12_RANGE AccessRange;
	AccessRange.Begin = UploadBuffer.Offset;
	AccessRange.End   = UploadBuffer.Offset + Size;
	CHECK(UploadBuffer.CPUPtr);

	UploadFunction(UploadBuffer.CPUPtr, Size);

	ScopedLock Lock(gUploadMutex);
	gUploadCmdList->CopyBufferRegion(Destination, 0, UploadBuffer.Get(), UploadBuffer.Offset, Size);
	gUploadTransitions.push_back(Barrier);
}

void UploadBufferData(ID3D12Resource* Destination, const void* Data, u64 Size, D3D12_RESOURCE_STATES TargetState)
{
	UploadBufferData(Destination, Size, TargetState,
		[Data](void *GPUAddress, u64 Size) {
			memcpy(GPUAddress, Data, Size);
		}
	);
}

TicketGPU UploadTextureDirectStorage(ID3D12Resource* Destination, VirtualTexture& VTex, u64 UncompressedSize, u32 NumMips, IDStorageFile* File, u64 Offset, u64 CompressedSize, const char* FileName)
{
	TicketGPU Result;
	Result.QueueType = D3D12_COMMAND_LIST_TYPE_BUNDLE; // there is no bundle queue, so we just mark direct storage tickets as bundle tickets
	Result.Value = gFenceValues[D3D12_COMMAND_LIST_TYPE_BUNDLE].load(std::memory_order_relaxed);

    auto Desc = Destination->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint[15]{};
	UINT Rows[15];
	UINT64 RowSizeInBytes[15];
	UINT64 RequiredSize;
    GetGraphicsDevice()->GetCopyableFootprints(&Desc, VTex.NumStreamedMips, NumMips - VTex.NumStreamedMips, 0, Footprint, Rows, RowSizeInBytes, &RequiredSize);

	CHECK(RequiredSize <= UncompressedSize);

	DSTORAGE_REQUEST Request{};
	Request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_GDEFLATE;

	Request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
	Request.Source.File = DSTORAGE_SOURCE_FILE{ File, Offset, (u32)CompressedSize };
	Request.Name = FileName;

	if (CompressedSize == 0)
	{
		Request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
		Request.Source.File.Size = UncompressedSize;
	}

	if (VTex.NumTilesForPacked)
	{
		Request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES;
		Request.Destination.MultipleSubresources = DSTORAGE_DESTINATION_MULTIPLE_SUBRESOURCES{Destination, VTex.NumStreamedMips};
		Request.UncompressedSize = VTex.NumTilesForPacked * 64_kb;
	}
	else
	{
		Request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES;
		Request.Destination.MultipleSubresources = DSTORAGE_DESTINATION_MULTIPLE_SUBRESOURCES{Destination, 0};
		Request.UncompressedSize = UncompressedSize;
	}

	gDirectStorageQueue->EnqueueRequest(&Request);
	gDirectStorageNumRequests++;
	gDirectStorageNeedsFlush = true;

	return Result;
}

struct HeapMagazine
{
	TComPtr<ID3D12Heap> BackingBuffer;
	u64 Occupancy;
};

static TracyLockable(Mutex, gHeapsLock);
static TDeque<HeapMagazine> gVirtualTexturesHeaps;

u16 AllocateTiles(u32 NumTiles)
{
	CHECK(NumTiles <= 64);

	u64 Mask = (~0ULL) >> (64 - NumTiles);
	for (int i = 0; i < gVirtualTexturesHeaps.size(); ++i)
	{
		HeapMagazine& Mag = gVirtualTexturesHeaps[i];
		
		u64 AvailabilityMask = Mag.Occupancy;
		u64 AvailabilityMaskForBitscan = ~AvailabilityMask;

		if (__popcnt64(AvailabilityMaskForBitscan) < NumTiles) continue;

		unsigned long BitIndex = 0;
		bool Continue = _BitScanForward64(&BitIndex, AvailabilityMaskForBitscan);
		while (Continue && __popcnt64(AvailabilityMaskForBitscan) >= NumTiles)
		{
			u64 ShiftedMask = (Mask << BitIndex);
			u64 Test = (ShiftedMask & AvailabilityMaskForBitscan);
			bool TestPassed = Test == ShiftedMask;
			if (TestPassed)
			{
				CHECK((ShiftedMask & AvailabilityMask) == 0);
				u64 OldValue = InterlockedCompareExchange(&Mag.Occupancy, ShiftedMask | AvailabilityMask, AvailabilityMask);
				bool OldValueCoresponds = OldValue == AvailabilityMask;
				if (OldValueCoresponds)
				{
					CHECK(BitIndex + i * 64 < UINT16_MAX);
					return BitIndex + i * 64;
				}
			}
			AvailabilityMask = Mag.Occupancy;
			u64 HowMuch = _tzcnt_u64(AvailabilityMask >> BitIndex);
			AvailabilityMaskForBitscan = ~(AvailabilityMask | (~0ULL >> (64 - BitIndex - HowMuch)));
			Continue = _BitScanForward64(&BitIndex, AvailabilityMaskForBitscan);
		}
	}

	u64 Result = ~0;

	D3D12_HEAP_DESC HeapDesc{};
	HeapDesc.SizeInBytes = 64 * 64_kb;
	HeapDesc.Properties = DefaultHeapProps;
	HeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	HeapDesc.Flags = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;

	TComPtr<ID3D12Heap> BackingBuffer;
	gDevice->CreateHeap(&HeapDesc, IID_PPV_ARGS(BackingBuffer.GetAddressOf()));

	gHeapsLock.lock();
	Result = gVirtualTexturesHeaps.size() * 64;
	HeapMagazine& Mag = gVirtualTexturesHeaps.push_back();
	Mag.BackingBuffer = MOVE(BackingBuffer);
	Mag.Occupancy = Mask;
	CHECK(Result < UINT16_MAX);
	gHeapsLock.unlock();
	
	return (u16)Result;
}

void FreeTiles(u32 TileStart, u32 NumTiles)
{
	u32 HeapIndex = TileStart / 64;
	u32 TileIndex = TileStart % 64;
	
	u64 Mask = (~0ULL) >> (64 - NumTiles);
	Mask <<= TileIndex;
	HeapMagazine& Mag = gVirtualTexturesHeaps[HeapIndex];
	u64 AvailabilityMask = Mag.Occupancy;
	CHECK((AvailabilityMask & Mask) == Mask);
	InterlockedAnd(&Mag.Occupancy, ~Mask);
}


TicketGPU MapVirtualTextureMip(VirtualTexture& VTex, u32 MipIndex)
{
	TicketGPU Result = CurrentFrameTicket();

	ID3D12Resource *Res = GetTextureResource(VTex.TexData.ID);
	auto Desc = Res->GetDesc();

	UINT NumSubresourceTiling = 1;
	D3D12_SUBRESOURCE_TILING SubResourceTiling;

	GetGraphicsDevice()->GetResourceTiling(Res, nullptr, nullptr, nullptr, &NumSubresourceTiling, MipIndex, &SubResourceTiling);

	u32 NumTiles = SubResourceTiling.WidthInTiles * SubResourceTiling.HeightInTiles * SubResourceTiling.DepthInTiles;

	VTex.StreamedTileIds[MipIndex] = AllocateTiles(NumTiles);

	auto* Q = GetGPUQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

	D3D12_TILED_RESOURCE_COORDINATE Coord{};
	Coord.Subresource = MipIndex;

	D3D12_TILE_REGION_SIZE Size{};
	Size.NumTiles = NumTiles;

	UINT RangeStartOffsets = VTex.StreamedTileIds[MipIndex] % 64;
	UINT RangeTileCounts = NumTiles;

	Q->UpdateTileMappings(Res, 1, &Coord, &Size, gVirtualTexturesHeaps[VTex.StreamedTileIds[MipIndex] / 64].BackingBuffer.Get(), 1, nullptr, &RangeStartOffsets, &RangeTileCounts, D3D12_TILE_MAPPING_FLAG_NONE);
	return Result;
}

void UnmapVirtualTextureMip(VirtualTexture& VTex, u32 MipIndex)
{
	u32 TileId = VTex.StreamedTileIds[MipIndex];
	VTex.StreamedTileIds[MipIndex] = 0;

	ID3D12Resource *Res = GetTextureResource(VTex.TexData.ID);
	auto Desc = Res->GetDesc();

	UINT NumSubresourceTiling = 1;
	D3D12_SUBRESOURCE_TILING SubResourceTiling;

	GetGraphicsDevice()->GetResourceTiling(Res, nullptr, nullptr, nullptr, &NumSubresourceTiling, MipIndex, &SubResourceTiling);

	u32 NumTiles = SubResourceTiling.WidthInTiles * SubResourceTiling.HeightInTiles * SubResourceTiling.DepthInTiles;

	D3D12_TILED_RESOURCE_COORDINATE Coord{};
	Coord.Subresource = MipIndex;

	D3D12_TILE_REGION_SIZE Size{};
	Size.NumTiles = NumTiles;

	UINT RangeTileCounts = NumTiles;
	D3D12_TILE_RANGE_FLAGS Flags = D3D12_TILE_RANGE_FLAG_NULL;

	GetGPUQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->UpdateTileMappings(Res, 1, &Coord, &Size, nullptr, 1, &Flags, nullptr, &RangeTileCounts, D3D12_TILE_MAPPING_FLAG_NONE);

	FreeTiles(TileId, NumTiles);
}

TicketGPU UpdateVirtualTextureDirectStorage(VirtualTexture& VTex, u32 MipIndex, u64 UncompressedSize, IDStorageFile* File, u64 Offset, u64 CompressedSize, const char* FileName)
{
	TicketGPU Result;
	Result.QueueType = D3D12_COMMAND_LIST_TYPE_BUNDLE;
	Result.Value = gFenceValues[D3D12_COMMAND_LIST_TYPE_BUNDLE].load(std::memory_order_relaxed);

	DSTORAGE_REQUEST Request{};
	Request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_GDEFLATE;

	Request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
	Request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_TEXTURE_REGION;

	Request.Source.File = DSTORAGE_SOURCE_FILE{ File, Offset, (u32)CompressedSize };

	if (CompressedSize == 0)
	{
		Request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
		Request.Source.File.Size = UncompressedSize;
	}

	ID3D12Resource *Res = GetTextureResource(VTex.TexData.ID);
	auto Desc = Res->GetDesc();

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
	UINT Rows;
	UINT64 RowSizeInBytes, RequiredSize;

    GetGraphicsDevice()->GetCopyableFootprints(&Desc, MipIndex, 1, 0, &Footprint, &Rows, &RowSizeInBytes, &RequiredSize);

	Request.Destination.Texture = DSTORAGE_DESTINATION_TEXTURE_REGION{ Res };
	Request.Destination.Texture.SubresourceIndex = MipIndex;
	Request.Destination.Texture.Region = D3D12_BOX{0, 0, 0, Footprint.Footprint.Width, Footprint.Footprint.Height, Footprint.Footprint.Depth};

	Request.UncompressedSize = UncompressedSize;
	Request.Name = FileName;

	gDirectStorageQueue->EnqueueRequest(&Request);
	gDirectStorageNumRequests++;
	gDirectStorageNeedsFlush = true;

	return Result;
}

TicketGPU UploadBufferDirectStorage(ID3D12Resource* Destination, u64 UncompressedSize, IDStorageFile* File, u64 Offset, u64 CompressedSize)
{
	TicketGPU Result;
	Result.QueueType = D3D12_COMMAND_LIST_TYPE_BUNDLE; // there is no bundle queue, so we just mark direct storage tickets as bundle tickets
	Result.Value = gFenceValues[D3D12_COMMAND_LIST_TYPE_BUNDLE].load(std::memory_order_relaxed);

	DSTORAGE_REQUEST Request{};
	Request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_GDEFLATE;
	Request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
	Request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_BUFFER;
	Request.Source.File = DSTORAGE_SOURCE_FILE{ File, Offset, (u32)CompressedSize };
	Request.Destination.Buffer = DSTORAGE_DESTINATION_BUFFER{ Destination, 0, (u32)UncompressedSize };
	Request.UncompressedSize = UncompressedSize;

	gDirectStorageQueue->EnqueueRequest(&Request);
	gDirectStorageNumRequests++;
	gDirectStorageNeedsFlush = true;

	return Result;
}

static void SetDefaultTileMapping(ID3D12Resource* TiledResource, VirtualTexture& VTex)
{
	auto* Q = GetGPUQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

	D3D12_TILED_RESOURCE_COORDINATE Coord{};
	Coord.Subresource = VTex.NumStreamedMips;

	VTex.PackedId = AllocateTiles(1);

	UINT RangeStartOffsets = VTex.PackedId % 64;
	UINT RangeTileCounts = VTex.NumTilesForPacked;

	Q->UpdateTileMappings(TiledResource, 1, &Coord, nullptr, gVirtualTexturesHeaps[VTex.PackedId / 64].BackingBuffer.Get(), 1, nullptr, &RangeStartOffsets, &RangeTileCounts, D3D12_TILE_MAPPING_FLAG_NONE);
}

TicketGPU CreateVirtualResourceForTexture(VirtualTexture& VTex, D3D12_RESOURCE_FLAGS Flags, D3D12_RESOURCE_STATES InitialState)
{
	TicketGPU Result{};

	TextureData& TexData = VTex.TexData;
	//ZoneScoped;
	D3D12_RESOURCE_DESC TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		(DXGI_FORMAT)TexData.Format,
		TexData.Width, TexData.Height,
		1, TexData.NumMips,
		TexData.SampleCount, 0,
		Flags, D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE
	);

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint[15]{};
	UINT Rows[15];
	UINT64 RowSizeInBytes[15];
	UINT64 RequiredSize;

    gDevice->GetCopyableFootprints(&TextureDesc, 0, VTex.TexData.NumMips, 0, Footprint, Rows, RowSizeInBytes, &RequiredSize);

	const D3D12_HEAP_PROPERTIES* HeapProps = &DefaultHeapProps;
	if (gDeviceCaps.CacheCoherentUMA)
	{
		HeapProps = &UmaHeapProperties;
	}

	bool ShouldUseVirtual = true;
	if (RequiredSize <= 64_kb)
	{
		ShouldUseVirtual = false;
		TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	}

	TComPtr<ID3D12Resource> Resource;
	if (ShouldUseVirtual)
	{
		Resource = CreateVirtualResource(&TextureDesc, HeapProps, InitialState);

		D3D12_PACKED_MIP_INFO PackedMipInfo{};

		gDevice->GetResourceTiling(Resource.Get(), nullptr, &PackedMipInfo, nullptr, nullptr, 0, nullptr);

		CHECK(PackedMipInfo.NumTilesForPackedMips == 1);

		VTex.NumTilesForPacked = PackedMipInfo.NumTilesForPackedMips;

		CHECK(PackedMipInfo.NumStandardMips < 8);

		VTex.NumStreamedMips = PackedMipInfo.NumStandardMips;
		VTex.NumStreamedIn = 0;

		Result = CurrentFrameTicket();
		SetDefaultTileMapping(Resource.Get(), VTex);
	}
	else
	{
		Resource = CreateResource(&TextureDesc, HeapProps, InitialState, nullptr);
	}

	TexData.ID = StoreTexture(Resource.Get(), "");
	TexData.Flags = (u8)Flags;
	
	return Result;
}
