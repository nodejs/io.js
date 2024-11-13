// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/******************************************************************************
 *   Copyright (C) 2008-2011, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 *******************************************************************************
 */

#ifndef __PKG_GENC_H__
#define __PKG_GENC_H__

#include "unicode/utypes.h"
#include "toolutil.h"

#include "unicode/putil.h"
#include "putilimp.h"

/*** Platform #defines move here ***/
#if U_PLATFORM_HAS_WIN32_API
#ifdef __GNUC__
#define WINDOWS_WITH_GNUC
#else
#define WINDOWS_WITH_MSVC
#endif
#endif


#if !defined(WINDOWS_WITH_MSVC)
#define BUILD_DATA_WITHOUT_ASSEMBLY
#endif

#ifndef U_DISABLE_OBJ_CODE /* testing */
#if defined(WINDOWS_WITH_MSVC) || U_PLATFORM_IS_LINUX_BASED
#define CAN_WRITE_OBJ_CODE
#endif
#if U_PLATFORM_HAS_WIN32_API || defined(U_ELF)
#define CAN_GENERATE_OBJECTS
#endif
#endif

#if U_PLATFORM == U_PF_CYGWIN || defined(CYGWINMSVC)
#define USING_CYGWIN
#endif

/*
 * When building the data library without assembly,
 * some platforms use a single c code file for all of
 * the data to generate the final data library. This can
 * increase the performance of the pkdata tool.
 */
#if U_PLATFORM == U_PF_OS400
#define USE_SINGLE_CCODE_FILE
#endif

/* Need to fix the file seperator character when using MinGW. */
#if defined(WINDOWS_WITH_GNUC) || defined(USING_CYGWIN)
#define PKGDATA_FILE_SEP_STRING "/"
#else
#define PKGDATA_FILE_SEP_STRING U_FILE_SEP_STRING
#endif

#define LARGE_BUFFER_MAX_SIZE 2048
#define SMALL_BUFFER_MAX_SIZE 512
#define SMALL_BUFFER_FLAG_NAMES 32
#define BUFFER_PADDING_SIZE 20

/** End platform defines **/



U_CAPI void U_EXPORT2
printAssemblyHeadersToStdErr(void);

U_CAPI UBool U_EXPORT2
checkAssemblyHeaderName(const char* optAssembly);

U_CAPI UBool U_EXPORT2
checkCpuArchitecture(const char* optCpuArch);

U_CAPI void U_EXPORT2
writeCCode(
    const char *filename,
    const char *destdir,
    const char *optEntryPoint,
    const char *optName,
    const char *optFilename,
    char *outFilePath,
    size_t outFilePathCapacity);

U_CAPI void U_EXPORT2
writeAssemblyCode(
    const char *filename,
    const char *destdir,
    const char *optEntryPoint,
    const char *optFilename,
    char *outFilePath,
    size_t outFilePathCapacity);

U_CAPI void U_EXPORT2
writeObjectCode(
    const char *filename,
    const char *destdir,
    const char *optEntryPoint,
    const char *optMatchArch,
    const char *optCpuArch,
    const char *optFilename,
    char *outFilePath,
    size_t outFilePathCapacity,
    UBool optWinDllExport);

#endif
