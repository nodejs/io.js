// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 1999-2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  unistr_cnv.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:2
*
*   created on: 2004aug19
*   created by: Markus W. Scherer
*
*   Character conversion functions moved here from unistr.cpp
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_CONVERSION

#include "unicode/putil.h"
#include "cstring.h"
#include "cmemory.h"
#include "unicode/ustring.h"
#include "unicode/unistr.h"
#include "unicode/ucnv.h"
#include "ucnv_imp.h"
#include "putilimp.h"
#include "ustr_cnv.h"
#include "ustr_imp.h"

U_NAMESPACE_BEGIN

//========================================
// Constructors
//========================================

#if !U_CHARSET_IS_UTF8

UnicodeString::UnicodeString(const char *codepageData) {
    fUnion.fFields.fLengthAndFlags = kShortString;
    if(codepageData != 0) {
        doCodepageCreate(codepageData, (int32_t)uprv_strlen(codepageData), 0);
    }
}

UnicodeString::UnicodeString(const char *codepageData,
                             int32_t dataLength) {
    fUnion.fFields.fLengthAndFlags = kShortString;
    if(codepageData != 0) {
        doCodepageCreate(codepageData, dataLength, 0);
    }
}

// else see unistr.cpp
#endif

UnicodeString::UnicodeString(const char *codepageData,
                             const char *codepage) {
    fUnion.fFields.fLengthAndFlags = kShortString;
    if (codepageData != nullptr) {
        doCodepageCreate(codepageData, (int32_t)uprv_strlen(codepageData), codepage);
    }
}

UnicodeString::UnicodeString(const char *codepageData,
                             int32_t dataLength,
                             const char *codepage) {
    fUnion.fFields.fLengthAndFlags = kShortString;
    if (codepageData != nullptr) {
        doCodepageCreate(codepageData, dataLength, codepage);
    }
}

UnicodeString::UnicodeString(const char *src, int32_t srcLength,
                             UConverter *cnv,
                             UErrorCode &errorCode) {
    fUnion.fFields.fLengthAndFlags = kShortString;
    if(U_SUCCESS(errorCode)) {
        // check arguments
        if(src==nullptr) {
            // treat as an empty string, do nothing more
        } else if(srcLength<-1) {
            errorCode=U_ILLEGAL_ARGUMENT_ERROR;
        } else {
            // get input length
            if(srcLength==-1) {
                srcLength=(int32_t)uprv_strlen(src);
            }
            if(srcLength>0) {
                if (cnv != nullptr) {
                    // use the provided converter
                    ucnv_resetToUnicode(cnv);
                    doCodepageCreate(src, srcLength, cnv, errorCode);
                } else {
                    // use the default converter
                    cnv=u_getDefaultConverter(&errorCode);
                    doCodepageCreate(src, srcLength, cnv, errorCode);
                    u_releaseDefaultConverter(cnv);
                }
            }
        }

        if(U_FAILURE(errorCode)) {
            setToBogus();
        }
    }
}

//========================================
// Codeset conversion
//========================================

#if !U_CHARSET_IS_UTF8

int32_t
UnicodeString::extract(int32_t start,
                       int32_t length,
                       char *target,
                       uint32_t dstSize) const {
    return extract(start, length, target, dstSize, 0);
}

// else see unistr.cpp
#endif

int32_t
UnicodeString::extract(int32_t start,
                       int32_t length,
                       char *target,
                       uint32_t dstSize,
                       const char *codepage) const
{
    // if the arguments are illegal, then do nothing
    if (/*dstSize < 0 || */(dstSize > 0 && target == nullptr)) {
        return 0;
    }

    // pin the indices to legal values
    pinIndices(start, length);

    // We need to cast dstSize to int32_t for all subsequent code.
    // I don't know why the API was defined with uint32_t but we are stuck with it.
    // Also, dstSize==0xffffffff means "unlimited" but if we use target+dstSize
    // as a limit in some functions, it may wrap around and yield a pointer
    // that compares less-than target.
    int32_t capacity;
    if(dstSize < 0x7fffffff) {
        // Assume that the capacity is real and a limit pointer won't wrap around.
        capacity = (int32_t)dstSize;
    } else {
        // Pin the capacity so that a limit pointer does not wrap around.
        char *targetLimit = (char *)U_MAX_PTR(target);
        // U_MAX_PTR(target) returns a targetLimit that is at most 0x7fffffff
        // greater than target and does not wrap around the top of the address space.
        capacity = (int32_t)(targetLimit - target);
    }

    // create the converter
    UConverter *converter;
    UErrorCode status = U_ZERO_ERROR;

    // just write the NUL if the string length is 0
    if(length == 0) {
        return u_terminateChars(target, capacity, 0, &status);
    }

    // if the codepage is the default, use our cache
    // if it is an empty string, then use the "invariant character" conversion
    if (codepage == nullptr) {
        const char *defaultName = ucnv_getDefaultName();
        if(UCNV_FAST_IS_UTF8(defaultName)) {
            return toUTF8(start, length, target, capacity);
        }
        converter = u_getDefaultConverter(&status);
    } else if (*codepage == 0) {
        // use the "invariant characters" conversion
        int32_t destLength;
        if(length <= capacity) {
            destLength = length;
        } else {
            destLength = capacity;
        }
        u_UCharsToChars(getArrayStart() + start, target, destLength);
        return u_terminateChars(target, capacity, length, &status);
    } else {
        converter = ucnv_open(codepage, &status);
    }

    length = doExtract(start, length, target, capacity, converter, status);

    // close the converter
    if (codepage == nullptr) {
        u_releaseDefaultConverter(converter);
    } else {
        ucnv_close(converter);
    }

    return length;
}

int32_t
UnicodeString::extract(char *dest, int32_t destCapacity,
                       UConverter *cnv,
                       UErrorCode &errorCode) const
{
    if(U_FAILURE(errorCode)) {
        return 0;
    }

    if (isBogus() || destCapacity < 0 || (destCapacity > 0 && dest == nullptr)) {
        errorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    // nothing to do?
    if(isEmpty()) {
        return u_terminateChars(dest, destCapacity, 0, &errorCode);
    }

    // get the converter
    UBool isDefaultConverter;
    if (cnv == nullptr) {
        isDefaultConverter=true;
        cnv=u_getDefaultConverter(&errorCode);
        if(U_FAILURE(errorCode)) {
            return 0;
        }
    } else {
        isDefaultConverter=false;
        ucnv_resetFromUnicode(cnv);
    }

    // convert
    int32_t len=doExtract(0, length(), dest, destCapacity, cnv, errorCode);

    // release the converter
    if(isDefaultConverter) {
        u_releaseDefaultConverter(cnv);
    }

    return len;
}

int32_t
UnicodeString::doExtract(int32_t start, int32_t length,
                         char *dest, int32_t destCapacity,
                         UConverter *cnv,
                         UErrorCode &errorCode) const
{
    if(U_FAILURE(errorCode)) {
        if(destCapacity!=0) {
            *dest=0;
        }
        return 0;
    }

    const char16_t *src=getArrayStart()+start, *srcLimit=src+length;
    char *originalDest=dest;
    const char *destLimit;

    if(destCapacity==0) {
        destLimit=dest=nullptr;
    } else if(destCapacity==-1) {
        // Pin the limit to U_MAX_PTR if the "magic" destCapacity is used.
        destLimit=(char*)U_MAX_PTR(dest);
        // for NUL-termination, translate into highest int32_t
        destCapacity=0x7fffffff;
    } else {
        destLimit=dest+destCapacity;
    }

    // perform the conversion
    ucnv_fromUnicode(cnv, &dest, destLimit, &src, srcLimit, nullptr, true, &errorCode);
    length=(int32_t)(dest-originalDest);

    // if an overflow occurs, then get the preflighting length
    if(errorCode==U_BUFFER_OVERFLOW_ERROR) {
        char buffer[1024];

        destLimit=buffer+sizeof(buffer);
        do {
            dest=buffer;
            errorCode=U_ZERO_ERROR;
            ucnv_fromUnicode(cnv, &dest, destLimit, &src, srcLimit, nullptr, true, &errorCode);
            length+=(int32_t)(dest-buffer);
        } while(errorCode==U_BUFFER_OVERFLOW_ERROR);
    }

    return u_terminateChars(originalDest, destCapacity, length, &errorCode);
}

void
UnicodeString::doCodepageCreate(const char *codepageData,
                                int32_t dataLength,
                                const char *codepage)
{
    // if there's nothing to convert, do nothing
    if (codepageData == nullptr || dataLength == 0 || dataLength < -1) {
        return;
    }
    if(dataLength == -1) {
        dataLength = (int32_t)uprv_strlen(codepageData);
    }

    UErrorCode status = U_ZERO_ERROR;

    // create the converter
    // if the codepage is the default, use our cache
    // if it is an empty string, then use the "invariant character" conversion
    UConverter *converter;
    if (codepage == nullptr) {
        const char *defaultName = ucnv_getDefaultName();
        if(UCNV_FAST_IS_UTF8(defaultName)) {
            setToUTF8(StringPiece(codepageData, dataLength));
            return;
        }
        converter = u_getDefaultConverter(&status);
    } else if (*codepage == 0) {
        // use the "invariant characters" conversion
        if(cloneArrayIfNeeded(dataLength, dataLength, false)) {
            u_charsToUChars(codepageData, getArrayStart(), dataLength);
            setLength(dataLength);
        } else {
            setToBogus();
        }
        return;
    } else {
        converter = ucnv_open(codepage, &status);
    }

    // if we failed, set the appropriate flags and return
    if(U_FAILURE(status)) {
        setToBogus();
        return;
    }

    // perform the conversion
    doCodepageCreate(codepageData, dataLength, converter, status);
    if(U_FAILURE(status)) {
        setToBogus();
    }

    // close the converter
    if (codepage == nullptr) {
        u_releaseDefaultConverter(converter);
    } else {
        ucnv_close(converter);
    }
}

void
UnicodeString::doCodepageCreate(const char *codepageData,
                                int32_t dataLength,
                                UConverter *converter,
                                UErrorCode &status)
{
    if(U_FAILURE(status)) {
        return;
    }

    // set up the conversion parameters
    const char *mySource     = codepageData;
    const char *mySourceEnd  = mySource + dataLength;
    char16_t *array, *myTarget;

    // estimate the size needed:
    int32_t arraySize;
    if(dataLength <= US_STACKBUF_SIZE) {
        // try to use the stack buffer
        arraySize = US_STACKBUF_SIZE;
    } else {
        // 1.25 char16_t's per source byte should cover most cases
        arraySize = dataLength + (dataLength >> 2);
    }

    // we do not care about the current contents
    UBool doCopyArray = false;
    for(;;) {
        if(!cloneArrayIfNeeded(arraySize, arraySize, doCopyArray)) {
            setToBogus();
            break;
        }

        // perform the conversion
        array = getArrayStart();
        myTarget = array + length();
        ucnv_toUnicode(converter, &myTarget,  array + getCapacity(),
            &mySource, mySourceEnd, nullptr, true, &status);

        // update the conversion parameters
        setLength((int32_t)(myTarget - array));

        // allocate more space and copy data, if needed
        if(status == U_BUFFER_OVERFLOW_ERROR) {
            // reset the error code
            status = U_ZERO_ERROR;

            // keep the previous conversion results
            doCopyArray = true;

            // estimate the new size needed, larger than before
            // try 2 char16_t's per remaining source byte
            arraySize = (int32_t)(length() + 2 * (mySourceEnd - mySource));
        } else {
            break;
        }
    }
}

U_NAMESPACE_END

#endif
