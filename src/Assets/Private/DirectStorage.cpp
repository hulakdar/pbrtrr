#include <dstorage.h>

static IDStorageFactory* gDirectStorageFactory;

IDStorageFactory* GetStorageFactory()
{
	return gDirectStorageFactory;
}

IDStorageFile* CreateDSFile(StringView Path)
{
	IDStorageFile* Result;

	WString DSPathWide = ToWide(String(Path));
	HRESULT DSResult = gDirectStorageFactory->OpenFile(DSPathWide.c_str(), IID_PPV_ARGS(&Result));
	CHECK(SUCCEEDED(DSResult));
	return Result;
}

void InitDirectStorageFactory()
{
	DStorageGetFactory(IID_PPV_ARGS(&gDirectStorageFactory));
	gDirectStorageFactory->SetStagingBufferSize(DSTORAGE_STAGING_BUFFER_SIZE_32MB * 2);
#if !RELEASE
	gDirectStorageFactory->SetDebugFlags(DSTORAGE_DEBUG_SHOW_ERRORS | DSTORAGE_DEBUG_BREAK_ON_ERROR);
#endif
}
