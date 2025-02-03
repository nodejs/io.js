// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 1999-2013, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  toolutil.h
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 1999nov19
*   created by: Markus W. Scherer
*
*   This file defines utility functions for ICU tools like genccode.
*/

#ifndef __TOOLUTIL_H__
#define __TOOLUTIL_H__

#include "unicode/utypes.h"

#ifdef __cplusplus

#include "unicode/errorcode.h"
#include "unicode/umutablecptrie.h"

U_NAMESPACE_BEGIN

/**
 * ErrorCode subclass for use in ICU command-line tools.
 * The destructor calls handleFailure() which calls exit(errorCode) when isFailure().
 */
class U_TOOLUTIL_API IcuToolErrorCode : public ErrorCode {
public:
    /**
     * @param loc A short string describing where the IcuToolErrorCode is used.
     */
    IcuToolErrorCode(const char *loc) : location(loc) {}
    virtual ~IcuToolErrorCode();
protected:
    virtual void handleFailure() const override;
private:
    const char *location;
};

namespace toolutil {

/**
 * Sets one bit in the trie values of the start..end range,
 * without changing the other bits in the trie values of that range.
 */
U_TOOLUTIL_API void
setCPTrieBit(UMutableCPTrie *mutableCPTrie,
             UChar32 start, UChar32 end, int32_t shift, bool on, UErrorCode &errorCode);

/**
 * Sets a bit set (defined by the mask) in the trie values of the start..end range,
 * without changing the other bits in the trie values of that range.
 * The given value must not have any bits set outside of the mask.
 */
U_TOOLUTIL_API void
setCPTrieBits(UMutableCPTrie *mutableCPTrie,
              UChar32 start, UChar32 end, uint32_t mask, uint32_t value, UErrorCode &errorCode);

U_TOOLUTIL_API int32_t
getCPTrieSize(UMutableCPTrie *mt, UCPTrieType type, UCPTrieValueWidth valueWidth);

}  // toolutil

U_NAMESPACE_END

#endif

/*
 * For Windows, a path/filename may be the short (8.3) version
 * of the "real", long one. In this case, the short one
 * is abbreviated and contains a tilde etc.
 * This function returns a pointer to the original pathname
 * if it is the "real" one itself, and a pointer to a static
 * buffer (not thread-safe) containing the long version
 * if the pathname is indeed abbreviated.
 *
 * On platforms other than Windows, this function always returns
 * the input pathname pointer.
 *
 * This function is especially useful in tools that are called
 * by a batch file for loop, which yields short pathnames on Win9x.
 */
U_CAPI const char * U_EXPORT2
getLongPathname(const char *pathname);

/**
 * Find the basename at the end of a pathname, i.e., the part
 * after the last file separator, and return a pointer
 * to this part of the pathname.
 * If the pathname only contains a basename and no file separator,
 * then the pathname pointer itself is returned.
 **/
U_CAPI const char * U_EXPORT2
findBasename(const char *filename);

/**
 * Find the directory name of a pathname, that is, everything
 * up to but not including the last file separator. 
 *
 * If successful, copies the directory name into the output buffer along with
 * a terminating NULL. 
 *
 * If there isn't a directory name in the path, it returns an empty string.
 * @param path the full pathname to inspect. 
 * @param buffer the output buffer
 * @param bufLen the output buffer length
 * @param status error code- may return U_BUFFER_OVERFLOW_ERROR if bufLen is too small.
 * @return If successful, a pointer to the output buffer. If failure or bufLen is too small, NULL.
 **/
U_CAPI const char * U_EXPORT2
findDirname(const char *path, char *buffer, int32_t bufLen, UErrorCode* status);

/*
 * Return the current year in the Gregorian calendar. Used for copyright generation.
 */
U_CAPI int32_t U_EXPORT2
getCurrentYear(void);

/*
 * Creates a directory with pathname.
 *
 * @param status Set to an error code when mkdir failed.
 */
U_CAPI void U_EXPORT2
uprv_mkdir(const char *pathname, UErrorCode *status);

#if !UCONFIG_NO_FILE_IO
/**
 * Return true if the named item exists
 * @param file filename
 * @return true if named item (file, dir, etc) exists, false otherwise
 */
U_CAPI UBool U_EXPORT2
uprv_fileExists(const char *file);
#endif

/**
 * Performs a golden data test. Asserts that the contents of the buffer is equal
 * to the data in goldenFilePath.
 *
 * Pass the value of the -G flag to "overwrite"; if true, new goldens will be
 * written to the filesystem.
 * 
 * @return The first index at which the files differ, or -1 if they are the same.
 */
U_CAPI int32_t U_EXPORT2
uprv_compareGoldenFiles(
    const char* buffer, int32_t bufferLen,
    const char* goldenFilePath,
    bool overwrite);

/**
 * Return the modification date for the specified file or directory.
 * Return value is undefined if there was an error.
 */
/*U_CAPI UDate U_EXPORT2
uprv_getModificationDate(const char *pathname, UErrorCode *status);
*/
/*
 * Returns the modification
 *
 * @param status Set to an error code when mkdir failed.
 */

/*
 * UToolMemory is used for generic, custom memory management.
 * It is allocated with enough space for count*size bytes starting
 * at array.
 * The array is declared with a union of large data types so
 * that its base address is aligned for any types.
 * If size is a multiple of a data type size, then such items
 * can be safely allocated inside the array, at offsets that
 * are themselves multiples of size.
 */
struct UToolMemory;
typedef struct UToolMemory UToolMemory;

/**
 * Open a UToolMemory object for allocation of initialCapacity to maxCapacity
 * items with size bytes each.
 */
U_CAPI UToolMemory * U_EXPORT2
utm_open(const char *name, int32_t initialCapacity, int32_t maxCapacity, int32_t size);

/**
 * Close a UToolMemory object.
 */
U_CAPI void U_EXPORT2
utm_close(UToolMemory *mem);

/**
 * Get the pointer to the beginning of the array of items.
 * The pointer becomes invalid after allocation of new items.
 */
U_CAPI void * U_EXPORT2
utm_getStart(UToolMemory *mem);

/**
 * Get the current number of items.
 */
U_CAPI int32_t U_EXPORT2
utm_countItems(UToolMemory *mem);

/**
 * Allocate one more item and return the pointer to its start in the array.
 */
U_CAPI void * U_EXPORT2
utm_alloc(UToolMemory *mem);

/**
 * Allocate n items and return the pointer to the start of the first one in the array.
 */
U_CAPI void * U_EXPORT2
utm_allocN(UToolMemory *mem, int32_t n);

#endif
