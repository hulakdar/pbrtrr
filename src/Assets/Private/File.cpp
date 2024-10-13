#include "Assets/File.generated.h"
#include "System/Win32.generated.h"

FileMapping MapFile(StringView FilePath)
{
	ZoneScoped;

	FileMapping Result;

	i32 Tries = 0;
	while (true)
	{
		Result.File = CreateFileA(FilePath.data(), GENERIC_READ,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

		if (Result.File != INVALID_HANDLE_VALUE)
		{
			break;
		}
		else if (++Tries > 500)
		{
			const char* err = strerror(GetLastError());
			printf("%s\n", err);
			Sleep(100);
		}
	}

	LARGE_INTEGER liFileSize;
	BOOL Success = GetFileSizeEx(Result.File, &liFileSize);
	CHECK(Success);
	if (!Success || liFileSize.QuadPart == 0)
	{
		CloseHandle(Result.File);
		return Result;
	}

	Result.Mapping = CreateFileMapping(Result.File, NULL, PAGE_READONLY, 0, 0, NULL);

	CHECK(Result.Mapping != 0);
	if (Result.Mapping == 0)
	{
		CloseHandle(Result.File);
		return Result;
	}

	Result.BasePtr = MapViewOfFile(Result.Mapping, FILE_MAP_READ, 0, 0, 0);

	CHECK(Result.BasePtr != NULL);
	if (Result.BasePtr == NULL)
	{
		CloseHandle(Result.Mapping);
		CloseHandle(Result.File);
		return Result;
	}

	Result.FileSize = liFileSize.QuadPart;
	return Result;
}

RawDataView GetView(const FileMapping& Mapping)
{
	return RawDataView((u8*)Mapping.BasePtr, Mapping.FileSize);
}

StringView GetAsStringView(const FileMapping& Mapping)
{
	return StringView((char*)Mapping.BasePtr, Mapping.FileSize);
}

bool IsValid(const FileMapping& Mapping)
{
	return Mapping.BasePtr;
}

void UnmapFile(FileMapping& Mapping)
{
	if (Mapping.File == INVALID_HANDLE_VALUE)
	{
		delete[] Mapping.BasePtr;
	}
	else
	{
		UnmapViewOfFile((const void*)Mapping.BasePtr);
		CloseHandle(Mapping.Mapping);
		CloseHandle(Mapping.File);
	}

	Mapping.BasePtr  = nullptr;
	Mapping.Mapping  = nullptr;
	Mapping.File     = nullptr;
	Mapping.FileSize = 0;
}
