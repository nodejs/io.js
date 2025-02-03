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

#include "cmemory.h"
#include "cstring.h"

#include "csr2022.h"
#include "csmatch.h"

U_NAMESPACE_BEGIN

/**
 * Matching function shared among the 2022 detectors JP, CN and KR
 * Counts up the number of legal and unrecognized escape sequences in
 * the sample of text, and computes a score based on the total number &
 * the proportion that fit the encoding.
 * 
 * 
 * @param text the byte buffer containing text to analyse
 * @param textLen  the size of the text in the byte.
 * @param escapeSequences the byte escape sequences to test for.
 * @return match quality, in the range of 0-100.
 */
int32_t CharsetRecog_2022::match_2022(const uint8_t *text, int32_t textLen, const uint8_t escapeSequences[][5], int32_t escapeSequences_length) const
{
    int32_t i, j;
    int32_t escN;
    int32_t hits   = 0;
    int32_t misses = 0;
    int32_t shifts = 0;
    int32_t quality;

    i = 0;
    while(i < textLen) {
        if(text[i] == 0x1B) {
            escN = 0;
            while(escN < escapeSequences_length) {
                const uint8_t *seq = escapeSequences[escN];
                int32_t seq_length = static_cast<int32_t>(uprv_strlen((const char*)seq));

                if (textLen-i >= seq_length) {
                    j = 1;
                    while(j < seq_length) {
                        if(seq[j] != text[i+j]) {
                            goto checkEscapes;
                        }

                        j += 1;
                    }

                    hits += 1;
                    i += seq_length-1;
                    goto scanInput;
                }
                // else we ran out of string to compare this time.
checkEscapes:
                escN += 1;
            }

            misses += 1;
        }

        if( text[i]== 0x0e || text[i] == 0x0f){
            shifts += 1;
        }

scanInput:
        i += 1;
    }

    if (hits == 0) {
        return 0;
    }

    //
    // Initial quality is based on relative proportion of recognized vs.
    //   unrecognized escape sequences. 
    //   All good:  quality = 100;
    //   half or less good: quality = 0;
    //   linear inbetween.
    quality = (100*hits - 100*misses) / (hits + misses);

    // Back off quality if there were too few escape sequences seen.
    //   Include shifts in this computation, so that KR does not get penalized
    //   for having only a single Escape sequence, but many shifts.
    if (hits+shifts < 5) {
        quality -= (5-(hits+shifts))*10;
    }

    if (quality < 0) {
        quality = 0;
    }

    return quality;
}


static const uint8_t escapeSequences_2022JP[][5] = {
    {0x1b, 0x24, 0x28, 0x43, 0x00},   // KS X 1001:1992
    {0x1b, 0x24, 0x28, 0x44, 0x00},   // JIS X 212-1990
    {0x1b, 0x24, 0x40, 0x00, 0x00},   // JIS C 6226-1978
    {0x1b, 0x24, 0x41, 0x00, 0x00},   // GB 2312-80
    {0x1b, 0x24, 0x42, 0x00, 0x00},   // JIS X 208-1983
    {0x1b, 0x26, 0x40, 0x00, 0x00},   // JIS X 208 1990, 1997
    {0x1b, 0x28, 0x42, 0x00, 0x00},   // ASCII
    {0x1b, 0x28, 0x48, 0x00, 0x00},   // JIS-Roman
    {0x1b, 0x28, 0x49, 0x00, 0x00},   // Half-width katakana
    {0x1b, 0x28, 0x4a, 0x00, 0x00},   // JIS-Roman
    {0x1b, 0x2e, 0x41, 0x00, 0x00},   // ISO 8859-1
    {0x1b, 0x2e, 0x46, 0x00, 0x00}    // ISO 8859-7
};

#if !UCONFIG_ONLY_HTML_CONVERSION
static const uint8_t escapeSequences_2022KR[][5] = {
    {0x1b, 0x24, 0x29, 0x43, 0x00}   
};

static const uint8_t escapeSequences_2022CN[][5] = {
    {0x1b, 0x24, 0x29, 0x41, 0x00},   // GB 2312-80
    {0x1b, 0x24, 0x29, 0x47, 0x00},   // CNS 11643-1992 Plane 1
    {0x1b, 0x24, 0x2A, 0x48, 0x00},   // CNS 11643-1992 Plane 2
    {0x1b, 0x24, 0x29, 0x45, 0x00},   // ISO-IR-165
    {0x1b, 0x24, 0x2B, 0x49, 0x00},   // CNS 11643-1992 Plane 3
    {0x1b, 0x24, 0x2B, 0x4A, 0x00},   // CNS 11643-1992 Plane 4
    {0x1b, 0x24, 0x2B, 0x4B, 0x00},   // CNS 11643-1992 Plane 5
    {0x1b, 0x24, 0x2B, 0x4C, 0x00},   // CNS 11643-1992 Plane 6
    {0x1b, 0x24, 0x2B, 0x4D, 0x00},   // CNS 11643-1992 Plane 7
    {0x1b, 0x4e, 0x00, 0x00, 0x00},   // SS2
    {0x1b, 0x4f, 0x00, 0x00, 0x00},   // SS3
};
#endif

CharsetRecog_2022JP::~CharsetRecog_2022JP() {}

const char *CharsetRecog_2022JP::getName() const {
    return "ISO-2022-JP";
}

UBool CharsetRecog_2022JP::match(InputText *textIn, CharsetMatch *results) const {
    int32_t confidence = match_2022(textIn->fInputBytes, 
                                    textIn->fInputLen, 
                                    escapeSequences_2022JP, 
                                    UPRV_LENGTHOF(escapeSequences_2022JP));
    results->set(textIn, this, confidence);
    return (confidence > 0);
}

#if !UCONFIG_ONLY_HTML_CONVERSION
CharsetRecog_2022KR::~CharsetRecog_2022KR() {}

const char *CharsetRecog_2022KR::getName() const {
    return "ISO-2022-KR";
}

UBool CharsetRecog_2022KR::match(InputText *textIn, CharsetMatch *results) const {
    int32_t confidence = match_2022(textIn->fInputBytes, 
                                    textIn->fInputLen, 
                                    escapeSequences_2022KR, 
                                    UPRV_LENGTHOF(escapeSequences_2022KR));
    results->set(textIn, this, confidence);
    return (confidence > 0);
}

CharsetRecog_2022CN::~CharsetRecog_2022CN() {}

const char *CharsetRecog_2022CN::getName() const {
    return "ISO-2022-CN";
}

UBool CharsetRecog_2022CN::match(InputText *textIn, CharsetMatch *results) const {
    int32_t confidence = match_2022(textIn->fInputBytes,
                                    textIn->fInputLen,
                                    escapeSequences_2022CN,
                                    UPRV_LENGTHOF(escapeSequences_2022CN));
    results->set(textIn, this, confidence);
    return (confidence > 0);
}
#endif

CharsetRecog_2022::~CharsetRecog_2022() {
    // nothing to do
}

U_NAMESPACE_END
#endif
