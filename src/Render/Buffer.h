#include <Common.h>
#include <Containers/ComPtr.h>

struct ID3D12Resource;

struct Buffer
{
    TComPtr<ID3D12Resource> Resource;
    u32 Size;
	u16 SRV    = UINT16_MAX;
	u16 UAV    = UINT16_MAX;
    u8 Format;
};