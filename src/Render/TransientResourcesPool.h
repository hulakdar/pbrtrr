#pragma once

#include "RenderForwardDeclarations.h"

enum BufferType
{
	BUFFER_GENERIC,
	BUFFER_UPLOAD,
	BUFFER_STAGING,
};

struct PooledBuffer
{
	ID3D12Resource* Resource = nullptr;
	u16 Offset = 0;
	u16 Size = 0;
	u8  Type = 0;

	ID3D12Resource* operator->() const { return Resource; }
	ID3D12Resource* Get() const { return Resource; }
};

void GetTransientTexture(TextureData& TexData, D3D12_RESOURCE_FLAGS Flags, D3D12_RESOURCE_STATES InitialState, D3D12_CLEAR_VALUE* ClearValue);
void DiscardTransientTexture(TextureData& TexData);

void GetTransientBuffer(PooledBuffer& Result, u64 Size, BufferType Type);
void DiscardTransientBuffer(PooledBuffer& Buffer);

