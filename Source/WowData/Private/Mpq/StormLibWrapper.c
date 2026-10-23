#include <stdbool.h>

/*
 * WowUnreal passes UTF-8/ANSI archive paths into this C wrapper.
 * Force StormLib's ANSI API so the header matches StormLibRAD.lib.
 */
#if defined(_WIN32)
    #ifdef UNICODE
        #undef UNICODE
    #endif

    #ifdef _UNICODE
        #undef _UNICODE
    #endif
#endif

#ifndef __STORMLIB_NO_STATIC_LINK__
    #define __STORMLIB_NO_STATIC_LINK__ 1
#endif

// StormLib wrapper compiled as C to avoid UE PCH/TCHAR conflicts.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundef"
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "StormLib.h"
#pragma clang diagnostic pop

typedef void* STORMLIB_HANDLE;
typedef unsigned int STORMLIB_UINT32;

bool StormLib_OpenArchive(const char* Path, STORMLIB_HANDLE* OutHandle)
{
    HANDLE hMpq = NULL;
    bool Result = SFileOpenArchive(Path, 0, MPQ_OPEN_READ_ONLY, &hMpq);
    *OutHandle = hMpq;
    return Result;
}

void StormLib_CloseArchive(STORMLIB_HANDLE Handle)
{
    SFileCloseArchive((HANDLE)Handle);
}

bool StormLib_OpenFile(STORMLIB_HANDLE Archive, const char* FileName, STORMLIB_HANDLE* OutFile)
{
    HANDLE hFile = NULL;
    bool Result = SFileOpenFileEx((HANDLE)Archive, FileName, 0, &hFile);
    *OutFile = hFile;
    return Result;
}

void StormLib_CloseFile(STORMLIB_HANDLE File)
{
    SFileCloseFile((HANDLE)File);
}

STORMLIB_UINT32 StormLib_GetFileSize(STORMLIB_HANDLE File)
{
    return SFileGetFileSize((HANDLE)File, NULL);
}

bool StormLib_ReadFile(
    STORMLIB_HANDLE File,
    void* Buffer,
    STORMLIB_UINT32 Size,
    STORMLIB_UINT32* BytesRead)
{
    DWORD Read = 0;
    bool Result = SFileReadFile((HANDLE)File, Buffer, Size, &Read, NULL);

    if (BytesRead)
    {
        *BytesRead = (STORMLIB_UINT32)Read;
    }

    return Result;
}