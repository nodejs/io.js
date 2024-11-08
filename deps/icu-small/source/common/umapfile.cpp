// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*
*   Copyright (C) 1999-2013, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
******************************************************************************/


/*----------------------------------------------------------------------------
 *
 *       Memory mapped file wrappers for use by the ICU Data Implementation
 *       All of the platform-specific implementation for mapping data files
 *         is here.  The rest of the ICU Data implementation uses only the
 *         wrapper functions.
 *
 *----------------------------------------------------------------------------*/
/* Defines _XOPEN_SOURCE for access to POSIX functions.
 * Must be before any other #includes. */
#include "uposixdefs.h"

#include "unicode/putil.h"
#include "unicode/ustring.h"
#include "udatamem.h"
#include "umapfile.h"

/* memory-mapping base definitions ------------------------------------------ */

#if MAP_IMPLEMENTATION==MAP_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#   define VC_EXTRALEAN
#   define NOUSER
#   define NOSERVICE
#   define NOIME
#   define NOMCX

#   if U_PLATFORM_HAS_WINUWP_API == 1
        // Some previous versions of the Windows 10 SDK don't expose various APIs for UWP applications
        // to use, even though UWP apps are allowed to call and use them.  Temporarily change the
        // WINAPI family partition below to Desktop, so that function declarations are visible for UWP.
#       include <winapifamily.h>
#       if !(WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM))
#           pragma push_macro("WINAPI_PARTITION_DESKTOP")
#           undef WINAPI_PARTITION_DESKTOP
#           define WINAPI_PARTITION_DESKTOP 1
#           define CHANGED_WINAPI_PARTITION_DESKTOP_VALUE
#       endif
#   endif

#   include <windows.h>

#   if U_PLATFORM_HAS_WINUWP_API == 1 && defined(CHANGED_WINAPI_PARTITION_DESKTOP_VALUE)
#       pragma pop_macro("WINAPI_PARTITION_DESKTOP")
#   endif

#   include "cmemory.h"

typedef HANDLE MemoryMap;

#   define IS_MAP(map) ((map)!=nullptr)

#elif MAP_IMPLEMENTATION==MAP_POSIX
    typedef size_t MemoryMap;

#   define IS_MAP(map) ((map)!=0)

#   include <unistd.h>
#   include <sys/mman.h>
#   include <sys/stat.h>
#   include <fcntl.h>

#   ifndef MAP_FAILED
#       define MAP_FAILED ((void*)-1)
#   endif
#elif MAP_IMPLEMENTATION==MAP_STDIO
#   include <stdio.h>
#   include "cmemory.h"

    typedef void *MemoryMap;

#   define IS_MAP(map) ((map)!=nullptr)
#endif

/*----------------------------------------------------------------------------*
 *                                                                            *
 *   Memory Mapped File support.  Platform dependent implementation of        *
 *                           functions used by the rest of the implementation.*
 *                                                                            *
 *----------------------------------------------------------------------------*/
#if MAP_IMPLEMENTATION==MAP_NONE
    U_CFUNC UBool
    uprv_mapFile(UDataMemory *pData, const char *path, UErrorCode *status) {
        if (U_FAILURE(*status)) {
            return false;
        }
        UDataMemory_init(pData); /* Clear the output struct. */
        return false;            /* no file access */
    }

    U_CFUNC void uprv_unmapFile(UDataMemory *pData) {
        /* nothing to do */
    }
#elif MAP_IMPLEMENTATION==MAP_WIN32
    U_CFUNC UBool
    uprv_mapFile(
         UDataMemory *pData,    /* Fill in with info on the result doing the mapping. */
                                /*   Output only; any original contents are cleared.  */
         const char *path,      /* File path to be opened/mapped.                     */
         UErrorCode *status     /* Error status, used to report out-of-memory errors. */
         )
    {
        if (U_FAILURE(*status)) {
            return false;
        }

        HANDLE map = nullptr;
        HANDLE file = INVALID_HANDLE_VALUE;

        UDataMemory_init(pData); /* Clear the output struct.        */

        /* open the input file */
#if U_PLATFORM_HAS_WINUWP_API == 0
        // Note: In the non-UWP code-path (ie: Win32), the value of the path variable might have come from 
        // the CRT 'getenv' function, and would be therefore be encoded in the default ANSI code page.
        // This means that we can't call the *W version of API below, whereas in the UWP code-path
        // there is no 'getenv' call, and thus the string will be only UTF-8/Invariant characters.
        file=CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_RANDOM_ACCESS, nullptr);
#else
        // Convert from UTF-8 string to UTF-16 string.
        wchar_t utf16Path[MAX_PATH];
        int32_t pathUtf16Len = 0;
        u_strFromUTF8(reinterpret_cast<char16_t*>(utf16Path), static_cast<int32_t>(UPRV_LENGTHOF(utf16Path)), &pathUtf16Len, path, -1, status);

        if (U_FAILURE(*status)) {
            return false;
        }
        if (*status == U_STRING_NOT_TERMINATED_WARNING) {
            // Report back an error instead of a warning.
            *status = U_BUFFER_OVERFLOW_ERROR;
            return false;
        }

        file = CreateFileW(utf16Path, GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
#endif
        if (file == INVALID_HANDLE_VALUE) {
            // If we failed to open the file due to an out-of-memory error, then we want
            // to report that error back to the caller.
            if (HRESULT_FROM_WIN32(GetLastError()) == E_OUTOFMEMORY) {
                *status = U_MEMORY_ALLOCATION_ERROR;
            }
            return false;
        }

        // Note: We use nullptr/nullptr for lpAttributes parameter below.
        // This means our handle cannot be inherited and we will get the default security descriptor.
        /* create an unnamed Windows file-mapping object for the specified file */
        map = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);

        CloseHandle(file);
        if (map == nullptr) {
            // If we failed to create the mapping due to an out-of-memory error, then 
            // we want to report that error back to the caller.
            if (HRESULT_FROM_WIN32(GetLastError()) == E_OUTOFMEMORY) {
                *status = U_MEMORY_ALLOCATION_ERROR;
            }
            return false;
        }

        /* map a view of the file into our address space */
        pData->pHeader = reinterpret_cast<const DataHeader *>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0));
        if (pData->pHeader == nullptr) {
            CloseHandle(map);
            return false;
        }
        pData->map = map;
        return true;
    }

    U_CFUNC void
    uprv_unmapFile(UDataMemory *pData) {
        if (pData != nullptr && pData->map != nullptr) {
            UnmapViewOfFile(pData->pHeader);
            CloseHandle(pData->map);
            pData->pHeader = nullptr;
            pData->map = nullptr;
        }
    }



#elif MAP_IMPLEMENTATION==MAP_POSIX
    U_CFUNC UBool
    uprv_mapFile(UDataMemory *pData, const char *path, UErrorCode *status) {
        int fd;
        int length;
        struct stat mystat;
        void *data;

        if (U_FAILURE(*status)) {
            return false;
        }

        UDataMemory_init(pData); /* Clear the output struct.        */

        /* determine the length of the file */
        if(stat(path, &mystat)!=0 || mystat.st_size<=0) {
            return false;
        }
        length=mystat.st_size;

        /* open the file */
        fd=open(path, O_RDONLY);
        if(fd==-1) {
            return false;
        }

        /* get a view of the mapping */
#if U_PLATFORM != U_PF_HPUX
        data=mmap(nullptr, length, PROT_READ, MAP_SHARED, fd, 0);
#else
        data=mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
#endif
        close(fd); /* no longer needed */
        if(data==MAP_FAILED) {
            // Possibly check the errno value for ENOMEM, and report U_MEMORY_ALLOCATION_ERROR?
            return false;
        }

        pData->map = (char *)data + length;
        pData->pHeader=(const DataHeader *)data;
        pData->mapAddr = data;
#if U_PLATFORM == U_PF_IPHONE
        posix_madvise(data, length, POSIX_MADV_RANDOM);
#endif
        return true;
    }

    U_CFUNC void
    uprv_unmapFile(UDataMemory *pData) {
        if(pData!=nullptr && pData->map!=nullptr) {
            size_t dataLen = (char *)pData->map - (char *)pData->mapAddr;
            if(munmap(pData->mapAddr, dataLen)==-1) {
            }
            pData->pHeader=nullptr;
            pData->map=nullptr;
            pData->mapAddr=nullptr;
        }
    }



#elif MAP_IMPLEMENTATION==MAP_STDIO
    /* copy of the filestrm.c/T_FileStream_size() implementation */
    static int32_t
    umap_fsize(FILE *f) {
        int32_t savedPos = ftell(f);
        int32_t size = 0;

        /*Changes by Bertrand A. D. doesn't affect the current position
        goes to the end of the file before ftell*/
        fseek(f, 0, SEEK_END);
        size = (int32_t)ftell(f);
        fseek(f, savedPos, SEEK_SET);
        return size;
    }

    U_CFUNC UBool
    uprv_mapFile(UDataMemory *pData, const char *path, UErrorCode *status) {
        FILE *file;
        int32_t fileLength;
        void *p;

        if (U_FAILURE(*status)) {
            return false;
        }

        UDataMemory_init(pData); /* Clear the output struct.        */
        /* open the input file */
        file=fopen(path, "rb");
        if(file==nullptr) {
            return false;
        }

        /* get the file length */
        fileLength=umap_fsize(file);
        if(ferror(file) || fileLength<=20) {
            fclose(file);
            return false;
        }

        /* allocate the memory to hold the file data */
        p=uprv_malloc(fileLength);
        if(p==nullptr) {
            fclose(file);
            *status = U_MEMORY_ALLOCATION_ERROR;
            return false;
        }

        /* read the file */
        if(fileLength!=fread(p, 1, fileLength, file)) {
            uprv_free(p);
            fclose(file);
            return false;
        }

        fclose(file);
        pData->map=p;
        pData->pHeader=(const DataHeader *)p;
        pData->mapAddr=p;
        return true;
    }

    U_CFUNC void
    uprv_unmapFile(UDataMemory *pData) {
        if(pData!=nullptr && pData->map!=nullptr) {
            uprv_free(pData->map);
            pData->map     = nullptr;
            pData->mapAddr = nullptr;
            pData->pHeader = nullptr;
        }
    }
#else
#   error MAP_IMPLEMENTATION is set incorrectly
#endif
