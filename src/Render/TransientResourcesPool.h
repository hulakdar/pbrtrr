#pragma once

#include "RenderForwardDeclarations.h"

enum BufferType : u8
{
	BUFFER_GENERIC,
	BUFFER_UPLOAD,
	BUFFER_READBACK,
};

struct PooledBuffer
{
	ID3D12Resource* Resource = nullptr;
	u8* CPUPtr = nullptr;
	u32 Size = 0;
	u16 Offset = 0;
	u8  Type = 0;

	ID3D12Resource* operator->() const { return Resource; }
	ID3D12Resource* Get() const { return Resource; }
};
