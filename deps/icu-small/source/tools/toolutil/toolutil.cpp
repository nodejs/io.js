// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 1999-2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  toolutil.c
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 1999nov19
*   created by: Markus W. Scherer
*
*	6/25/08 - Added Cygwin specific code in uprv_mkdir - Brian Rower
*	
*   This file contains utility functions for ICU tools like genccode.
*/

#include "unicode/platform.h"
#if U_PLATFORM == U_PF_MINGW
// *cough* - for struct stat
#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif
#endif

#include <stdio.h>
#include <sys/stat.h>
#include <fstream>
#include <time.h>
#include "unicode/utypes.h"

#ifndef U_TOOLUTIL_IMPLEMENTATION
#error U_TOOLUTIL_IMPLEMENTATION not set - must be set for all ICU source files in common/ - see https://unicode-org.github.io/icu/userguide/howtouseicu
#endif

#if U_PLATFORM_USES_ONLY_WIN32_API
#   define VC_EXTRALEAN
#   define WIN32_LEAN_AND_MEAN
#   define NOUSER
#   define NOSERVICE
#   define NOIME
#   define NOMCX
#   if U_PLATFORM == U_PF_MINGW
#     define __NO_MINGW_LFS /* gets around missing 'off64_t' */
#   endif
#   include <windows.h>
#   include <direct.h>
#else
#   include <sys/stat.h>
#   include <sys/types.h>
#endif

/* In MinGW environment, io.h needs to be included for _mkdir() */
#if U_PLATFORM == U_PF_MINGW
#include <io.h>
#endif

#include <errno.h>

#include <cstddef>

#include "unicode/errorcode.h"
#include "unicode/putil.h"
#include "unicode/uchar.h"
#include "unicode/umutablecptrie.h"
#include "unicode/ucptrie.h"
#include "cmemory.h"
#include "cstring.h"
#include "toolutil.h"
#include "uassert.h"

U_NAMESPACE_BEGIN

IcuToolErrorCode::~IcuToolErrorCode() {
    // Safe because our handleFailure() does not throw exceptions.
    if(isFailure()) { handleFailure(); }
}

void IcuToolErrorCode::handleFailure() const {
    fprintf(stderr, "error at %s: %s\n", location, errorName());
    exit(errorCode);
}

namespace toolutil {

void setCPTrieBit(UMutableCPTrie *mutableCPTrie,
                  UChar32 start, UChar32 end, int32_t shift, bool on, UErrorCode &errorCode) {
    uint32_t mask = U_MASK(shift);
    uint32_t value = on ? mask : 0;
    setCPTrieBits(mutableCPTrie, start, end, mask, value, errorCode);
}

void setCPTrieBits(UMutableCPTrie *mutableCPTrie,
                   UChar32 start, UChar32 end, uint32_t mask, uint32_t value,
                   UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) { return; }
    // The value must not have any bits set outside of the mask.
    if ((value & ~mask) != 0) {
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if (start == end) {
        uint32_t oldValue = umutablecptrie_get(mutableCPTrie, start);
        uint32_t newValue = (oldValue & ~mask) | value;
        if (newValue != oldValue) {
            umutablecptrie_set(mutableCPTrie, start, newValue, &errorCode);
        }
        return;
    }
    while (start <= end && U_SUCCESS(errorCode)) {
        uint32_t oldValue;
        UChar32 rangeEnd = umutablecptrie_getRange(
            mutableCPTrie, start, UCPMAP_RANGE_NORMAL, 0, nullptr, nullptr, &oldValue);
        if (rangeEnd > end) {
            rangeEnd = end;
        }
        uint32_t newValue = (oldValue & ~mask) | value;
        if (newValue != oldValue) {
            umutablecptrie_setRange(mutableCPTrie, start, rangeEnd, newValue, &errorCode);
        }
        start = rangeEnd + 1;
    }
}

int32_t getCPTrieSize(UMutableCPTrie *mt, UCPTrieType type, UCPTrieValueWidth valueWidth) {
    UErrorCode errorCode = U_ZERO_ERROR;
    UCPTrie *cpTrie = umutablecptrie_buildImmutable(mt, type, valueWidth, &errorCode);
    if (U_FAILURE(errorCode)) {
        fprintf(stderr,
                "toolutil/getCPTrieSize error: umutablecptrie_buildImmutable() failed: %s\n",
                u_errorName(errorCode));
        return -1;
    }
    uint8_t block[100000];
    int32_t size = ucptrie_toBinary(cpTrie, block, sizeof(block), &errorCode);
    ucptrie_close(cpTrie);
    if (U_FAILURE(errorCode) && errorCode != U_BUFFER_OVERFLOW_ERROR) {
        fprintf(stderr,
                "toolutil/getCPTrieSize error: ucptrie_toBinary() failed: %s (length %ld)\n",
                u_errorName(errorCode), static_cast<long>(size));
        return -1;
    }
    U_ASSERT((size & 3) == 0);  // multiple of 4 bytes
    return size;
}

}  // toolutil

U_NAMESPACE_END

static int32_t currentYear = -1;

U_CAPI int32_t U_EXPORT2 getCurrentYear() {
    if(currentYear == -1) {
        time_t now = time(nullptr);
        tm *fields = gmtime(&now);
        currentYear = 1900 + fields->tm_year;
    }
    return currentYear;
}


U_CAPI const char * U_EXPORT2
getLongPathname(const char *pathname) {
#if U_PLATFORM_USES_ONLY_WIN32_API
    /* anticipate problems with "short" pathnames */
    static WIN32_FIND_DATAA info;
    HANDLE file=FindFirstFileA(pathname, &info);
    if(file!=INVALID_HANDLE_VALUE) {
        if(info.cAlternateFileName[0]!=0) {
            /* this file has a short name, get and use the long one */
            const char *basename=findBasename(pathname);
            if(basename!=pathname) {
                /* prepend the long filename with the original path */
                uprv_memmove(info.cFileName+(basename-pathname), info.cFileName, uprv_strlen(info.cFileName)+1);
                uprv_memcpy(info.cFileName, pathname, basename-pathname);
            }
            pathname=info.cFileName;
        }
        FindClose(file);
    }
#endif
    return pathname;
}

U_CAPI const char * U_EXPORT2
findDirname(const char *path, char *buffer, int32_t bufLen, UErrorCode* status) {
  if(U_FAILURE(*status)) return nullptr;
  const char *resultPtr = nullptr;
  int32_t resultLen = 0;

  const char *basename=uprv_strrchr(path, U_FILE_SEP_CHAR);
#if U_FILE_ALT_SEP_CHAR!=U_FILE_SEP_CHAR
  const char *basenameAlt=uprv_strrchr(path, U_FILE_ALT_SEP_CHAR);
  if(basenameAlt && (!basename || basename<basenameAlt)) {
    basename = basenameAlt;
  }
#endif
  if(!basename) {
    /* no basename - return ''. */
    resultPtr = "";
    resultLen = 0;
  } else {
    resultPtr = path;
    resultLen = static_cast<int32_t>(basename - path);
    if(resultLen<1) {
      resultLen = 1; /* '/' or '/a' -> '/' */
    }
  }

  if((resultLen+1) <= bufLen) {
    uprv_strncpy(buffer, resultPtr, resultLen);
    buffer[resultLen]=0;
    return buffer;
  } else {
    *status = U_BUFFER_OVERFLOW_ERROR;
    return nullptr;
  }
}

U_CAPI const char * U_EXPORT2
findBasename(const char *filename) {
    const char *basename=uprv_strrchr(filename, U_FILE_SEP_CHAR);

#if U_FILE_ALT_SEP_CHAR!=U_FILE_SEP_CHAR
    //be lenient about pathname separators on Windows, like official implementation of C++17 std::filesystem in MSVC
    //would be convenient to merge this loop with the one above, but alas, there is no such solution in the standard library
    const char *alt_basename=uprv_strrchr(filename, U_FILE_ALT_SEP_CHAR);
    if(alt_basename>basename) {
        basename=alt_basename;
    }
#endif

    if(basename!=nullptr) {
        return basename+1;
    } else {
        return filename;
    }
}

U_CAPI void U_EXPORT2
uprv_mkdir(const char *pathname, UErrorCode *status) {

    int retVal = 0;
#if U_PLATFORM_USES_ONLY_WIN32_API
    retVal = _mkdir(pathname);
#else
    retVal = mkdir(pathname, S_IRWXU | (S_IROTH | S_IXOTH) | (S_IROTH | S_IXOTH));
#endif
    if (retVal && errno != EEXIST) {
#if U_PF_MINGW <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN
        /*if using Cygwin and the mkdir says it failed...check if the directory already exists..*/
        /* if it does...don't give the error, if it does not...give the error - Brian Rower - 6/25/08 */
        struct stat st;

        if(stat(pathname,&st) != 0)
        {
            *status = U_FILE_ACCESS_ERROR;
        }
#else
        *status = U_FILE_ACCESS_ERROR;
#endif
    }
}

#if !UCONFIG_NO_FILE_IO
U_CAPI UBool U_EXPORT2
uprv_fileExists(const char *file) {
  struct stat stat_buf;
  if (stat(file, &stat_buf) == 0) {
    return true;
  } else {
    return false;
  }
}
#endif

U_CAPI int32_t U_EXPORT2
uprv_compareGoldenFiles(
        const char* buffer, int32_t bufferLen,
        const char* goldenFilePath,
        bool overwrite) {

    if (overwrite) {
        std::ofstream ofs;
        ofs.open(goldenFilePath);
        ofs.write(buffer, bufferLen);
        ofs.close();
        return -1;
    }

    std::ifstream ifs(goldenFilePath, std::ifstream::in);
    int32_t pos = 0;
    char c;
    while (ifs.get(c) && pos < bufferLen) {
        if (c != buffer[pos]) {
            // Files differ at this position
            break;
        }
        pos++;
    }
    if (pos == bufferLen && ifs.eof()) {
        // Files are same lengths
        pos = -1;
    }
    ifs.close();
    return pos;
}

/*U_CAPI UDate U_EXPORT2
uprv_getModificationDate(const char *pathname, UErrorCode *status)
{
    if(U_FAILURE(*status)) {
        return;
    }
    //  TODO: handle case where stat is not available
    struct stat st;
    
    if(stat(pathname,&st) != 0)
    {
        *status = U_FILE_ACCESS_ERROR;
    } else {
        return st.st_mtime;
    }
}
*/

/* tool memory helper ------------------------------------------------------- */

struct UToolMemory {
    char name[64];
    int32_t capacity, maxCapacity, size, idx;
    void *array;
    alignas(std::max_align_t) char staticArray[1];
};

U_CAPI UToolMemory * U_EXPORT2
utm_open(const char *name, int32_t initialCapacity, int32_t maxCapacity, int32_t size) {
    UToolMemory *mem;

    if(maxCapacity<initialCapacity) {
        maxCapacity=initialCapacity;
    }

    mem=(UToolMemory *)uprv_malloc(sizeof(UToolMemory)+initialCapacity*size);
    if(mem==nullptr) {
        fprintf(stderr, "error: %s - out of memory\n", name);
        exit(U_MEMORY_ALLOCATION_ERROR);
    }
    mem->array=mem->staticArray;

    uprv_strcpy(mem->name, name);
    mem->capacity=initialCapacity;
    mem->maxCapacity=maxCapacity;
    mem->size=size;
    mem->idx=0;
    return mem;
}

U_CAPI void U_EXPORT2
utm_close(UToolMemory *mem) {
    if(mem!=nullptr) {
        if(mem->array!=mem->staticArray) {
            uprv_free(mem->array);
        }
        uprv_free(mem);
    }
}


U_CAPI void * U_EXPORT2
utm_getStart(UToolMemory *mem) {
    return (char *)mem->array;
}

U_CAPI int32_t U_EXPORT2
utm_countItems(UToolMemory *mem) {
    return mem->idx;
}


static UBool
utm_hasCapacity(UToolMemory *mem, int32_t capacity) {
    if(mem->capacity<capacity) {
        int32_t newCapacity;

        if(mem->maxCapacity<capacity) {
            fprintf(stderr, "error: %s - trying to use more than maxCapacity=%ld units\n",
                    mem->name, static_cast<long>(mem->maxCapacity));
            exit(U_MEMORY_ALLOCATION_ERROR);
        }

        /* try to allocate a larger array */
        if(capacity>=2*mem->capacity) {
            newCapacity=capacity;
        } else if(mem->capacity<=mem->maxCapacity/3) {
            newCapacity=2*mem->capacity;
        } else {
            newCapacity=mem->maxCapacity;
        }

        if(mem->array==mem->staticArray) {
            mem->array=uprv_malloc(newCapacity*mem->size);
            if(mem->array!=nullptr) {
                uprv_memcpy(mem->array, mem->staticArray, (size_t)mem->idx*mem->size);
            }
        } else {
            mem->array=uprv_realloc(mem->array, newCapacity*mem->size);
        }

        if(mem->array==nullptr) {
            fprintf(stderr, "error: %s - out of memory\n", mem->name);
            exit(U_MEMORY_ALLOCATION_ERROR);
        }
        mem->capacity=newCapacity;
    }

    return true;
}

U_CAPI void * U_EXPORT2
utm_alloc(UToolMemory *mem) {
    char *p=nullptr;
    int32_t oldIndex=mem->idx;
    int32_t newIndex=oldIndex+1;
    if(utm_hasCapacity(mem, newIndex)) {
        p=(char *)mem->array+oldIndex*mem->size;
        mem->idx=newIndex;
        uprv_memset(p, 0, mem->size);
    }
    return p;
}

U_CAPI void * U_EXPORT2
utm_allocN(UToolMemory *mem, int32_t n) {
    char *p=nullptr;
    int32_t oldIndex=mem->idx;
    int32_t newIndex=oldIndex+n;
    if(utm_hasCapacity(mem, newIndex)) {
        p=(char *)mem->array+oldIndex*mem->size;
        mem->idx=newIndex;
        uprv_memset(p, 0, n*mem->size);
    }
    return p;
}
