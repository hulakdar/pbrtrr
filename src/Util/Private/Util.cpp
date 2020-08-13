#include <Tracy.hpp>
#include <stdint.h>
#include <fstream>
#include "Containers/String.h"
#include "Containers/Map.h"
#include "Util/Util.h"
#include <Util/Debug.h>

TMap<const char*, String> FileCache;

void WaitForFenceValue(ComPtr<ID3D12Fence>& Fence, uint64_t FenceValue, HANDLE Event)
{
	if (Fence->GetCompletedValue() < FenceValue)
	{
		VALIDATE(Fence->SetEventOnCompletion(FenceValue, Event));
		::WaitForSingleObject(Event, MAXDWORD);
	}
}

uint64_t Signal(ComPtr<ID3D12CommandQueue>& CommandQueue, ComPtr<ID3D12Fence>& Fence, uint64_t& FenceValue)
{
	uint64_t FenceValueForSignal = ++FenceValue;
	VALIDATE(CommandQueue->Signal(Fence.Get(), FenceValueForSignal));

	return FenceValueForSignal;
}

void Flush(ComPtr<ID3D12CommandQueue>& CommandQueue, ComPtr<ID3D12Fence>& Fence, uint64_t& FenceValue, HANDLE FenceEvent)
{
	uint64_t fenceValueForSignal = Signal(CommandQueue, Fence, FenceValue);
	WaitForFenceValue(Fence, fenceValueForSignal, FenceEvent);
}

StringView LoadWholeFile(const char *Path)
{
	auto CachedValue = FileCache.find(Path);
	if (CachedValue != FileCache.end())
	{
		return CachedValue->second;
	}

	std::ifstream infile(Path);
	infile.seekg(0, std::ios::end);
	size_t file_size_in_byte = infile.tellg();
	String data;
	data.resize(file_size_in_byte);
	infile.seekg(0, std::ios::beg);
	infile.read(&data[0], file_size_in_byte);

	return FileCache[Path] = MOVE(data);
}

// for eastl
// maybe we could write our own custom allocator in future
void* operator new[](size_t size, const char*, int, unsigned, const char*, int)
{
	uint8_t *ptr = new uint8_t[size];
	TracyAlloc (ptr , size);
	return ptr;
}

void* operator new[](size_t size, size_t, size_t, const char*, int, unsigned, const char*, int) 
{
	uint8_t *ptr = new uint8_t[size];
	TracyAlloc (ptr , size);
	return ptr;
}  


void* operator new(std :: size_t count)
{
	auto ptr = malloc(count);
	TracyAlloc (ptr , count);
	return ptr;
}
void operator delete(void* ptr) noexcept
{
	TracyFree (ptr);
	free(ptr);
}

