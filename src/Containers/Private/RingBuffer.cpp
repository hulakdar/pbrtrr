#include "Containers/RingBuffer.h"
#include "Util/Debug.h"
#include <wtypes.h>

// based on https://docs.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2
// with slight modifications
void* AllocateRingBuffer(u64 Size, void** SecondaryView)
{
    bool result;
    HANDLE section = nullptr;
    SYSTEM_INFO sysInfo;
    void* ringBuffer = nullptr;
    void* placeholder1 = nullptr;
    void* placeholder2 = nullptr;
    void* view1 = nullptr;
    void* view2 = nullptr;

    GetSystemInfo (&sysInfo);

    if ((Size % sysInfo.dwAllocationGranularity) != 0) {
        return nullptr;
    }

    //
    // Reserve a placeholder region where the buffer will be mapped.
    //

    placeholder1 = (PCHAR) VirtualAlloc2 (
        nullptr,
        nullptr,
        2 * Size,
        MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
        PAGE_NOACCESS,
        nullptr, 0
    );

    CHECK(placeholder1 != nullptr, "VirtualAlloc2 failed");

    //
    // Split the placeholder region into two regions of equal size.
    //

    result = VirtualFree (
        placeholder1,
        Size,
        MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER
    );

    CHECK(result != FALSE,"VirtualFreeEx failed");

    placeholder2 = (void*) ((ULONG_PTR) placeholder1 + Size);

    //
    // Create a pagefile-backed section for the buffer.
    //

    section = CreateFileMapping (
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        Size, nullptr
    );

	CHECK(section != nullptr, "CreateFileMapping failed");

    //
    // Map the section into the first placeholder region.
    //

    view1 = MapViewOfFile3 (
        section,
        nullptr,
        placeholder1,
        0,
        Size,
        MEM_REPLACE_PLACEHOLDER,
        PAGE_READWRITE,
        nullptr, 0
    );

    CHECK(view1 != nullptr, "MapViewOfFile3 failed");

    //
    // Ownership transferred, don’t free this now.
    //

    placeholder1 = nullptr;

    //
    // Map the section into the second placeholder region.
    //

    view2 = MapViewOfFile3 (
        section,
        nullptr,
        placeholder2,
        0,
        Size,
        MEM_REPLACE_PLACEHOLDER,
        PAGE_READWRITE,
        nullptr, 0
    );

    CHECK(view2 == nullptr, "MapViewOfFile3 failed");

    //
    // Success, return both mapped views to the caller.
    //

    ringBuffer = view1;
    if (SecondaryView)
    {
		*SecondaryView = view2;
    }

    placeholder2 = nullptr;
    view1 = nullptr;
    view2 = nullptr;

    return ringBuffer;
}
