// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 **********************************************************************
 *   Copyright (C) 2005-2016, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 **********************************************************************
 */

#include "unicode/utypes.h"

#if !UCONFIG_NO_CONVERSION

#include "inputext.h"

#include "cmemory.h"
#include "cstring.h"

#include <string.h>

U_NAMESPACE_BEGIN

#define BUFFER_SIZE 8192

#define NEW_ARRAY(type,count) (type *) uprv_malloc((count) * sizeof(type))
#define DELETE_ARRAY(array) uprv_free((void *) (array))

InputText::InputText(UErrorCode &status)
    : fInputBytes(NEW_ARRAY(uint8_t, BUFFER_SIZE)), // The text to be checked.  Markup will have been
                                                 //   removed if appropriate.
      fByteStats(NEW_ARRAY(int16_t, 256)),       // byte frequency statistics for the input text.
                                                 //   Value is percent, not absolute.
      fDeclaredEncoding(nullptr),
      fRawInput(nullptr),
      fRawLength(0)
{
    if (fInputBytes == nullptr || fByteStats == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
}

InputText::~InputText()
{
    DELETE_ARRAY(fDeclaredEncoding);
    DELETE_ARRAY(fByteStats);
    DELETE_ARRAY(fInputBytes);
}

void InputText::setText(const char *in, int32_t len)
{
    fInputLen  = 0;
    fC1Bytes   = false;
    fRawInput  = (const uint8_t *) in;
    fRawLength = len == -1? (int32_t)uprv_strlen(in) : len;
}

void InputText::setDeclaredEncoding(const char* encoding, int32_t len)
{
    if(encoding) {
        if (len == -1) {
            len = (int32_t)uprv_strlen(encoding);
        }

        len += 1;     // to make place for the \0 at the end.
        uprv_free(fDeclaredEncoding);
        fDeclaredEncoding = NEW_ARRAY(char, len);
        uprv_strncpy(fDeclaredEncoding, encoding, len);
    }
}

UBool InputText::isSet() const 
{
    return fRawInput != nullptr;
}

/**
*  MungeInput - after getting a set of raw input data to be analyzed, preprocess
*               it by removing what appears to be html markup.
* 
* @internal
*/
void InputText::MungeInput(UBool fStripTags) {
    int     srci = 0;
    int     dsti = 0;
    uint8_t b;
    bool    inMarkup = false;
    int32_t openTags = 0;
    int32_t badTags  = 0;

    //
    //  html / xml markup stripping.
    //     quick and dirty, not 100% accurate, but hopefully good enough, statistically.
    //     discard everything within < brackets >
    //     Count how many total '<' and illegal (nested) '<' occur, so we can make some
    //     guess as to whether the input was actually marked up at all.
    // TODO: Think about how this interacts with EBCDIC charsets that are detected.
    if (fStripTags) {
        for (srci = 0; srci < fRawLength && dsti < BUFFER_SIZE; srci += 1) {
            b = fRawInput[srci];

            if (b == (uint8_t)0x3C) { /* Check for the ASCII '<' */
                if (inMarkup) {
                    badTags += 1;
                }

                inMarkup = true;
                openTags += 1;
            }

            if (! inMarkup) {
                fInputBytes[dsti++] = b;
            }

            if (b == (uint8_t)0x3E) { /* Check for the ASCII '>' */
                inMarkup = false;
            }
        }

        fInputLen = dsti;
    }

    //
    //  If it looks like this input wasn't marked up, or if it looks like it's
    //    essentially nothing but markup abandon the markup stripping.
    //    Detection will have to work on the unstripped input.
    //
    if (openTags<5 || openTags/5 < badTags || 
        (fInputLen < 100 && fRawLength>600))
    {
        int32_t limit = fRawLength;

        if (limit > BUFFER_SIZE) {
            limit = BUFFER_SIZE;
        }

        for (srci=0; srci<limit; srci++) {
            fInputBytes[srci] = fRawInput[srci];
        }

        fInputLen = srci;
    }

    //
    // Tally up the byte occurrence statistics.
    // These are available for use by the various detectors.
    //

    uprv_memset(fByteStats, 0, (sizeof fByteStats[0]) * 256);

    for (srci = 0; srci < fInputLen; srci += 1) {
        fByteStats[fInputBytes[srci]] += 1;
    }

    for (int32_t i = 0x80; i <= 0x9F; i += 1) {
        if (fByteStats[i] != 0) {
            fC1Bytes = true;
            break;
        }
    }
}

U_NAMESPACE_END
#endif

