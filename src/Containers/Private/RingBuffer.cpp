#include "Containers/RingBuffer.h"
#include "Util/Debug.h"
#include <wtypes.h>
#include <Threading/Worker.h>

// based on https://docs.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2
// with slight modifications
void* AllocateRingBuffer(u64 Size)
{
    {
		SYSTEM_INFO sysInfo;
		GetSystemInfo (&sysInfo);

        CHECK((Size % sysInfo.dwAllocationGranularity) == 0);
    }

    //
    // Reserve a placeholder region where the buffer will be mapped.
    //

    void* placeholder = (PCHAR) VirtualAlloc2 (
        nullptr,
        nullptr,
        2 * Size,
        MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
        PAGE_NOACCESS,
        nullptr, 0
    );

    CHECK(placeholder != nullptr, "VirtualAlloc2 failed");

    //
    // Split the placeholder region into two regions of equal size.
    //

    int result = VirtualFree (
        placeholder,
        Size,
        MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER
    );

    CHECK(result != 0,"VirtualFreeEx failed");
    if (result == 0)
    {
		std::string ErrorText = std::system_category().message(GetLastError());
        DEBUG_BREAK();
        //std::cout << ErrorText;
    }

    //
    // Create a pagefile-backed section for the buffer.
    //

    HANDLE section = CreateFileMapping (
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        DWORD(Size), nullptr
    );

	CHECK(section != nullptr, "CreateFileMapping failed");

    //
    // Map the section into the first placeholder region.
    //

    void* view1 = MapViewOfFile3 (
        section,
        nullptr,
        placeholder,
        0,
        Size,
        MEM_REPLACE_PLACEHOLDER,
        PAGE_READWRITE,
        nullptr, 0
    );

    CHECK(view1 != nullptr, "MapViewOfFile3 failed");

    //
    // Map the section into the second placeholder region.
    //

    void* view2 = MapViewOfFile3 (
        section,
        nullptr,
        (void*) ((ULONG_PTR) placeholder + Size),
        0,
        Size,
        MEM_REPLACE_PLACEHOLDER,
        PAGE_READWRITE,
        nullptr, 0
    );

    CHECK(view2 != nullptr, "MapViewOfFile3 failed");

    return view1;
}

void FreeRingBuffer(void* Buffer, u64 Size)
{
    UnmapViewOfFile(Buffer);
    UnmapViewOfFile((void*)(uintptr_t(Buffer) + Size));
    VirtualFree(Buffer, 0, MEM_RELEASE);
}

RingBufferGeneric::RingBufferGeneric(u64 InSize)
{
    Data = (u8*)AllocateRingBuffer(InSize);
    Size = InSize;
}

RingBufferGeneric::~RingBufferGeneric()
{
    FreeRingBuffer(Data, Size);
}

void* RingBufferGeneric::Aquire(u64 NumBytes)
{
    return Data + WriteOffset.fetch_add(NumBytes) % Size;
}
