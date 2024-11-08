// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*
*   Copyright (C) 2000-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
******************************************************************************
*   file name:  ucnvscsu.c
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2000nov18
*   created by: Markus W. Scherer
*
*   This is an implementation of the Standard Compression Scheme for Unicode
*   as defined in https://www.unicode.org/reports/tr6/ .
*   Reserved commands and window settings are treated as illegal sequences and
*   will result in callback calls.
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_CONVERSION && !UCONFIG_ONLY_HTML_CONVERSION

#include "unicode/ucnv.h"
#include "unicode/ucnv_cb.h"
#include "unicode/utf16.h"
#include "ucnv_bld.h"
#include "ucnv_cnv.h"
#include "cmemory.h"

/* SCSU definitions --------------------------------------------------------- */

/* SCSU command byte values */
enum {
    SQ0=0x01, /* Quote from window pair 0 */
    SQ7=0x08, /* Quote from window pair 7 */
    SDX=0x0B, /* Define a window as extended */
    Srs=0x0C, /* reserved */
    SQU=0x0E, /* Quote a single Unicode character */
    SCU=0x0F, /* Change to Unicode mode */
    SC0=0x10, /* Select window 0 */
    SC7=0x17, /* Select window 7 */
    SD0=0x18, /* Define and select window 0 */
    SD7=0x1F, /* Define and select window 7 */

    UC0=0xE0, /* Select window 0 */
    UC7=0xE7, /* Select window 7 */
    UD0=0xE8, /* Define and select window 0 */
    UD7=0xEF, /* Define and select window 7 */
    UQU=0xF0, /* Quote a single Unicode character */
    UDX=0xF1, /* Define a Window as extended */
    Urs=0xF2  /* reserved */
};

enum {
    /*
     * Unicode code points from 3400 to E000 are not adressible by
     * dynamic window, since in these areas no short run alphabets are
     * found. Therefore add gapOffset to all values from gapThreshold.
     */
    gapThreshold=0x68,
    gapOffset=0xAC00,

    /* values between reservedStart and fixedThreshold are reserved */
    reservedStart=0xA8,

    /* use table of predefined fixed offsets for values from fixedThreshold */
    fixedThreshold=0xF9
};

/* constant offsets for the 8 static windows */
static const uint32_t staticOffsets[8]={
    0x0000, /* ASCII for quoted tags */
    0x0080, /* Latin - 1 Supplement (for access to punctuation) */
    0x0100, /* Latin Extended-A */
    0x0300, /* Combining Diacritical Marks */
    0x2000, /* General Punctuation */
    0x2080, /* Currency Symbols */
    0x2100, /* Letterlike Symbols and Number Forms */
    0x3000  /* CJK Symbols and punctuation */
};

/* initial offsets for the 8 dynamic (sliding) windows */
static const uint32_t initialDynamicOffsets[8]={
    0x0080, /* Latin-1 */
    0x00C0, /* Latin Extended A */
    0x0400, /* Cyrillic */
    0x0600, /* Arabic */
    0x0900, /* Devanagari */
    0x3040, /* Hiragana */
    0x30A0, /* Katakana */
    0xFF00  /* Fullwidth ASCII */
};

/* Table of fixed predefined Offsets */
static const uint32_t fixedOffsets[]={
    /* 0xF9 */ 0x00C0, /* Latin-1 Letters + half of Latin Extended A */
    /* 0xFA */ 0x0250, /* IPA extensions */
    /* 0xFB */ 0x0370, /* Greek */
    /* 0xFC */ 0x0530, /* Armenian */
    /* 0xFD */ 0x3040, /* Hiragana */
    /* 0xFE */ 0x30A0, /* Katakana */
    /* 0xFF */ 0xFF60  /* Halfwidth Katakana */
};

/* state values */
enum {
    readCommand,
    quotePairOne,
    quotePairTwo,
    quoteOne,
    definePairOne,
    definePairTwo,
    defineOne
};

typedef struct SCSUData {
    /* dynamic window offsets, initialize to default values from initialDynamicOffsets */
    uint32_t toUDynamicOffsets[8];
    uint32_t fromUDynamicOffsets[8];

    /* state machine state - toUnicode */
    UBool toUIsSingleByteMode;
    uint8_t toUState;
    int8_t toUQuoteWindow, toUDynamicWindow;
    uint8_t toUByteOne;
    uint8_t toUPadding[3];

    /* state machine state - fromUnicode */
    UBool fromUIsSingleByteMode;
    int8_t fromUDynamicWindow;

    /*
     * windowUse[] keeps track of the use of the dynamic windows:
     * At nextWindowUseIndex there is the least recently used window,
     * and the following windows (in a wrapping manner) are more and more
     * recently used.
     * At nextWindowUseIndex-1 there is the most recently used window.
     */
    uint8_t locale;
    int8_t nextWindowUseIndex;
    int8_t windowUse[8];
} SCSUData;

static const int8_t initialWindowUse[8]={ 7, 0, 3, 2, 4, 5, 6, 1 };
static const int8_t initialWindowUse_ja[8]={ 3, 2, 4, 1, 0, 7, 5, 6 };

enum {
    lGeneric, l_ja
};

/* SCSU setup functions ----------------------------------------------------- */
U_CDECL_BEGIN
static void U_CALLCONV
_SCSUReset(UConverter *cnv, UConverterResetChoice choice) {
    SCSUData *scsu=(SCSUData *)cnv->extraInfo;

    if(choice<=UCNV_RESET_TO_UNICODE) {
        /* reset toUnicode */
        uprv_memcpy(scsu->toUDynamicOffsets, initialDynamicOffsets, 32);

        scsu->toUIsSingleByteMode=true;
        scsu->toUState=readCommand;
        scsu->toUQuoteWindow=scsu->toUDynamicWindow=0;
        scsu->toUByteOne=0;

        cnv->toULength=0;
    }
    if(choice!=UCNV_RESET_TO_UNICODE) {
        /* reset fromUnicode */
        uprv_memcpy(scsu->fromUDynamicOffsets, initialDynamicOffsets, 32);

        scsu->fromUIsSingleByteMode=true;
        scsu->fromUDynamicWindow=0;

        scsu->nextWindowUseIndex=0;
        switch(scsu->locale) {
        case l_ja:
            uprv_memcpy(scsu->windowUse, initialWindowUse_ja, 8);
            break;
        default:
            uprv_memcpy(scsu->windowUse, initialWindowUse, 8);
            break;
        }

        cnv->fromUChar32=0;
    }
}

static void U_CALLCONV
_SCSUOpen(UConverter *cnv,
          UConverterLoadArgs *pArgs,
          UErrorCode *pErrorCode) {
    const char *locale=pArgs->locale;
    if(pArgs->onlyTestIsLoadable) {
        return;
    }
    cnv->extraInfo=uprv_malloc(sizeof(SCSUData));
    if(cnv->extraInfo!=nullptr) {
        if(locale!=nullptr && locale[0]=='j' && locale[1]=='a' && (locale[2]==0 || locale[2]=='_')) {
            ((SCSUData *)cnv->extraInfo)->locale=l_ja;
        } else {
            ((SCSUData *)cnv->extraInfo)->locale=lGeneric;
        }
        _SCSUReset(cnv, UCNV_RESET_BOTH);
    } else {
        *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
    }

    /* Set the substitution character U+fffd as a Unicode string. */
    cnv->subUChars[0]=0xfffd;
    cnv->subCharLen=-1;
}

static void U_CALLCONV
_SCSUClose(UConverter *cnv) {
    if(cnv->extraInfo!=nullptr) {
        if(!cnv->isExtraLocal) {
            uprv_free(cnv->extraInfo);
        }
        cnv->extraInfo=nullptr;
    }
}

/* SCSU-to-Unicode conversion functions ------------------------------------- */

static void U_CALLCONV
_SCSUToUnicodeWithOffsets(UConverterToUnicodeArgs *pArgs,
                          UErrorCode *pErrorCode) {
    UConverter *cnv;
    SCSUData *scsu;
    const uint8_t *source, *sourceLimit;
    char16_t *target;
    const char16_t *targetLimit;
    int32_t *offsets;
    UBool isSingleByteMode;
    uint8_t state, byteOne;
    int8_t quoteWindow, dynamicWindow;

    int32_t sourceIndex, nextSourceIndex;

    uint8_t b;

    /* set up the local pointers */
    cnv=pArgs->converter;
    scsu=(SCSUData *)cnv->extraInfo;

    source=(const uint8_t *)pArgs->source;
    sourceLimit=(const uint8_t *)pArgs->sourceLimit;
    target=pArgs->target;
    targetLimit=pArgs->targetLimit;
    offsets=pArgs->offsets;

    /* get the state machine state */
    isSingleByteMode=scsu->toUIsSingleByteMode;
    state=scsu->toUState;
    quoteWindow=scsu->toUQuoteWindow;
    dynamicWindow=scsu->toUDynamicWindow;
    byteOne=scsu->toUByteOne;

    /* sourceIndex=-1 if the current character began in the previous buffer */
    sourceIndex=state==readCommand ? 0 : -1;
    nextSourceIndex=0;

    /*
     * conversion "loop"
     *
     * For performance, this is not a normal C loop.
     * Instead, there are two code blocks for the two SCSU modes.
     * The function branches to either one, and a change of the mode is done with a goto to
     * the other branch.
     *
     * Each branch has two conventional loops:
     * - a fast-path loop for the most common codes in the mode
     * - a loop for all other codes in the mode
     * When the fast-path runs into a code that it cannot handle, its loop ends and it
     * runs into the following loop to handle the other codes.
     * The end of the input or output buffer is also handled by the slower loop.
     * The slow loop jumps (goto) to the fast-path loop again as soon as possible.
     *
     * The callback handling is done by returning with an error code.
     * The conversion framework actually calls the callback function.
     */
    if(isSingleByteMode) {
        /* fast path for single-byte mode */
        if(state==readCommand) {
fastSingle:
            while(source<sourceLimit && target<targetLimit && (b=*source)>=0x20) {
                ++source;
                ++nextSourceIndex;
                if(b<=0x7f) {
                    /* write US-ASCII graphic character or DEL */
                    *target++=(char16_t)b;
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                } else {
                    /* write from dynamic window */
                    uint32_t c=scsu->toUDynamicOffsets[dynamicWindow]+(b&0x7f);
                    if(c<=0xffff) {
                        *target++=(char16_t)c;
                        if(offsets!=nullptr) {
                            *offsets++=sourceIndex;
                        }
                    } else {
                        /* output surrogate pair */
                        *target++=(char16_t)(0xd7c0+(c>>10));
                        if(target<targetLimit) {
                            *target++=(char16_t)(0xdc00|(c&0x3ff));
                            if(offsets!=nullptr) {
                                *offsets++=sourceIndex;
                                *offsets++=sourceIndex;
                            }
                        } else {
                            /* target overflow */
                            if(offsets!=nullptr) {
                                *offsets++=sourceIndex;
                            }
                            cnv->UCharErrorBuffer[0]=(char16_t)(0xdc00|(c&0x3ff));
                            cnv->UCharErrorBufferLength=1;
                            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                            goto endloop;
                        }
                    }
                }
                sourceIndex=nextSourceIndex;
            }
        }

        /* normal state machine for single-byte mode, minus handling for what fastSingle covers */
singleByteMode:
        while(source<sourceLimit) {
            if(target>=targetLimit) {
                /* target is full */
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                break;
            }
            b=*source++;
            ++nextSourceIndex;
            switch(state) {
            case readCommand:
                /* redundant conditions are commented out */
                /* here: b<0x20 because otherwise we would be in fastSingle */
                if((1UL<<b)&0x2601 /* binary 0010 0110 0000 0001, check for b==0xd || b==0xa || b==9 || b==0 */) {
                    /* CR/LF/TAB/NUL */
                    *target++=(char16_t)b;
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                    sourceIndex=nextSourceIndex;
                    goto fastSingle;
                } else if(SC0<=b) {
                    if(b<=SC7) {
                        dynamicWindow=(int8_t)(b-SC0);
                        sourceIndex=nextSourceIndex;
                        goto fastSingle;
                    } else /* if(SD0<=b && b<=SD7) */ {
                        dynamicWindow=(int8_t)(b-SD0);
                        state=defineOne;
                    }
                } else if(/* SQ0<=b && */ b<=SQ7) {
                    quoteWindow=(int8_t)(b-SQ0);
                    state=quoteOne;
                } else if(b==SDX) {
                    state=definePairOne;
                } else if(b==SQU) {
                    state=quotePairOne;
                } else if(b==SCU) {
                    sourceIndex=nextSourceIndex;
                    isSingleByteMode=false;
                    goto fastUnicode;
                } else /* Srs */ {
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    goto endloop;
                }

                /* store the first byte of a multibyte sequence in toUBytes[] */
                cnv->toUBytes[0]=b;
                cnv->toULength=1;
                break;
            case quotePairOne:
                byteOne=b;
                cnv->toUBytes[1]=b;
                cnv->toULength=2;
                state=quotePairTwo;
                break;
            case quotePairTwo:
                *target++=(char16_t)((byteOne<<8)|b);
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                sourceIndex=nextSourceIndex;
                state=readCommand;
                goto fastSingle;
            case quoteOne:
                if(b<0x80) {
                    /* all static offsets are in the BMP */
                    *target++=(char16_t)(staticOffsets[quoteWindow]+b);
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                } else {
                    /* write from dynamic window */
                    uint32_t c=scsu->toUDynamicOffsets[quoteWindow]+(b&0x7f);
                    if(c<=0xffff) {
                        *target++=(char16_t)c;
                        if(offsets!=nullptr) {
                            *offsets++=sourceIndex;
                        }
                    } else {
                        /* output surrogate pair */
                        *target++=(char16_t)(0xd7c0+(c>>10));
                        if(target<targetLimit) {
                            *target++=(char16_t)(0xdc00|(c&0x3ff));
                            if(offsets!=nullptr) {
                                *offsets++=sourceIndex;
                                *offsets++=sourceIndex;
                            }
                        } else {
                            /* target overflow */
                            if(offsets!=nullptr) {
                                *offsets++=sourceIndex;
                            }
                            cnv->UCharErrorBuffer[0]=(char16_t)(0xdc00|(c&0x3ff));
                            cnv->UCharErrorBufferLength=1;
                            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                            goto endloop;
                        }
                    }
                }
                sourceIndex=nextSourceIndex;
                state=readCommand;
                goto fastSingle;
            case definePairOne:
                dynamicWindow=(int8_t)((b>>5)&7);
                byteOne=(uint8_t)(b&0x1f);
                cnv->toUBytes[1]=b;
                cnv->toULength=2;
                state=definePairTwo;
                break;
            case definePairTwo:
                scsu->toUDynamicOffsets[dynamicWindow]=0x10000+(byteOne<<15UL | b<<7UL);
                sourceIndex=nextSourceIndex;
                state=readCommand;
                goto fastSingle;
            case defineOne:
                if(b==0) {
                    /* callback(illegal): Reserved window offset value 0 */
                    cnv->toUBytes[1]=b;
                    cnv->toULength=2;
                    goto endloop;
                } else if(b<gapThreshold) {
                    scsu->toUDynamicOffsets[dynamicWindow]=b<<7UL;
                } else if((uint8_t)(b-gapThreshold)<(reservedStart-gapThreshold)) {
                    scsu->toUDynamicOffsets[dynamicWindow]=(b<<7UL)+gapOffset;
                } else if(b>=fixedThreshold) {
                    scsu->toUDynamicOffsets[dynamicWindow]=fixedOffsets[b-fixedThreshold];
                } else {
                    /* callback(illegal): Reserved window offset value 0xa8..0xf8 */
                    cnv->toUBytes[1]=b;
                    cnv->toULength=2;
                    goto endloop;
                }
                sourceIndex=nextSourceIndex;
                state=readCommand;
                goto fastSingle;
            }
        }
    } else {
        /* fast path for Unicode mode */
        if(state==readCommand) {
fastUnicode:
            while(source+1<sourceLimit && target<targetLimit && (uint8_t)((b=*source)-UC0)>(Urs-UC0)) {
                *target++=(char16_t)((b<<8)|source[1]);
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                sourceIndex=nextSourceIndex;
                nextSourceIndex+=2;
                source+=2;
            }
        }

        /* normal state machine for Unicode mode */
/* unicodeByteMode: */
        while(source<sourceLimit) {
            if(target>=targetLimit) {
                /* target is full */
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                break;
            }
            b=*source++;
            ++nextSourceIndex;
            switch(state) {
            case readCommand:
                if((uint8_t)(b-UC0)>(Urs-UC0)) {
                    byteOne=b;
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    state=quotePairTwo;
                } else if(/* UC0<=b && */ b<=UC7) {
                    dynamicWindow=(int8_t)(b-UC0);
                    sourceIndex=nextSourceIndex;
                    isSingleByteMode=true;
                    goto fastSingle;
                } else if(/* UD0<=b && */ b<=UD7) {
                    dynamicWindow=(int8_t)(b-UD0);
                    isSingleByteMode=true;
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    state=defineOne;
                    goto singleByteMode;
                } else if(b==UDX) {
                    isSingleByteMode=true;
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    state=definePairOne;
                    goto singleByteMode;
                } else if(b==UQU) {
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    state=quotePairOne;
                } else /* Urs */ {
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    goto endloop;
                }
                break;
            case quotePairOne:
                byteOne=b;
                cnv->toUBytes[1]=b;
                cnv->toULength=2;
                state=quotePairTwo;
                break;
            case quotePairTwo:
                *target++=(char16_t)((byteOne<<8)|b);
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                sourceIndex=nextSourceIndex;
                state=readCommand;
                goto fastUnicode;
            }
        }
    }
endloop:

    /* set the converter state back into UConverter */
    if(U_FAILURE(*pErrorCode) && *pErrorCode!=U_BUFFER_OVERFLOW_ERROR) {
        /* reset to deal with the next character */
        state=readCommand;
    } else if(state==readCommand) {
        /* not in a multi-byte sequence, reset toULength */
        cnv->toULength=0;
    }
    scsu->toUIsSingleByteMode=isSingleByteMode;
    scsu->toUState=state;
    scsu->toUQuoteWindow=quoteWindow;
    scsu->toUDynamicWindow=dynamicWindow;
    scsu->toUByteOne=byteOne;

    /* write back the updated pointers */
    pArgs->source=(const char *)source;
    pArgs->target=target;
    pArgs->offsets=offsets;
}

/*
 * Identical to _SCSUToUnicodeWithOffsets but without offset handling.
 * If a change is made in the original function, then either
 * change this function the same way or
 * re-copy the original function and remove the variables
 * offsets, sourceIndex, and nextSourceIndex.
 */
static void U_CALLCONV
_SCSUToUnicode(UConverterToUnicodeArgs *pArgs,
               UErrorCode *pErrorCode) {
    UConverter *cnv;
    SCSUData *scsu;
    const uint8_t *source, *sourceLimit;
    char16_t *target;
    const char16_t *targetLimit;
    UBool isSingleByteMode;
    uint8_t state, byteOne;
    int8_t quoteWindow, dynamicWindow;

    uint8_t b;

    /* set up the local pointers */
    cnv=pArgs->converter;
    scsu=(SCSUData *)cnv->extraInfo;

    source=(const uint8_t *)pArgs->source;
    sourceLimit=(const uint8_t *)pArgs->sourceLimit;
    target=pArgs->target;
    targetLimit=pArgs->targetLimit;

    /* get the state machine state */
    isSingleByteMode=scsu->toUIsSingleByteMode;
    state=scsu->toUState;
    quoteWindow=scsu->toUQuoteWindow;
    dynamicWindow=scsu->toUDynamicWindow;
    byteOne=scsu->toUByteOne;

    /*
     * conversion "loop"
     *
     * For performance, this is not a normal C loop.
     * Instead, there are two code blocks for the two SCSU modes.
     * The function branches to either one, and a change of the mode is done with a goto to
     * the other branch.
     *
     * Each branch has two conventional loops:
     * - a fast-path loop for the most common codes in the mode
     * - a loop for all other codes in the mode
     * When the fast-path runs into a code that it cannot handle, its loop ends and it
     * runs into the following loop to handle the other codes.
     * The end of the input or output buffer is also handled by the slower loop.
     * The slow loop jumps (goto) to the fast-path loop again as soon as possible.
     *
     * The callback handling is done by returning with an error code.
     * The conversion framework actually calls the callback function.
     */
    if(isSingleByteMode) {
        /* fast path for single-byte mode */
        if(state==readCommand) {
fastSingle:
            while(source<sourceLimit && target<targetLimit && (b=*source)>=0x20) {
                ++source;
                if(b<=0x7f) {
                    /* write US-ASCII graphic character or DEL */
                    *target++=(char16_t)b;
                } else {
                    /* write from dynamic window */
                    uint32_t c=scsu->toUDynamicOffsets[dynamicWindow]+(b&0x7f);
                    if(c<=0xffff) {
                        *target++=(char16_t)c;
                    } else {
                        /* output surrogate pair */
                        *target++=(char16_t)(0xd7c0+(c>>10));
                        if(target<targetLimit) {
                            *target++=(char16_t)(0xdc00|(c&0x3ff));
                        } else {
                            /* target overflow */
                            cnv->UCharErrorBuffer[0]=(char16_t)(0xdc00|(c&0x3ff));
                            cnv->UCharErrorBufferLength=1;
                            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                            goto endloop;
                        }
                    }
                }
            }
        }

        /* normal state machine for single-byte mode, minus handling for what fastSingle covers */
singleByteMode:
        while(source<sourceLimit) {
            if(target>=targetLimit) {
                /* target is full */
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                break;
            }
            b=*source++;
            switch(state) {
            case readCommand:
                /* redundant conditions are commented out */
                /* here: b<0x20 because otherwise we would be in fastSingle */
                if((1UL<<b)&0x2601 /* binary 0010 0110 0000 0001, check for b==0xd || b==0xa || b==9 || b==0 */) {
                    /* CR/LF/TAB/NUL */
                    *target++=(char16_t)b;
                    goto fastSingle;
                } else if(SC0<=b) {
                    if(b<=SC7) {
                        dynamicWindow=(int8_t)(b-SC0);
                        goto fastSingle;
                    } else /* if(SD0<=b && b<=SD7) */ {
                        dynamicWindow=(int8_t)(b-SD0);
                        state=defineOne;
                    }
                } else if(/* SQ0<=b && */ b<=SQ7) {
                    quoteWindow=(int8_t)(b-SQ0);
                    state=quoteOne;
                } else if(b==SDX) {
                    state=definePairOne;
                } else if(b==SQU) {
                    state=quotePairOne;
                } else if(b==SCU) {
                    isSingleByteMode=false;
                    goto fastUnicode;
                } else /* Srs */ {
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    goto endloop;
                }

                /* store the first byte of a multibyte sequence in toUBytes[] */
                cnv->toUBytes[0]=b;
                cnv->toULength=1;
                break;
            case quotePairOne:
                byteOne=b;
                cnv->toUBytes[1]=b;
                cnv->toULength=2;
                state=quotePairTwo;
                break;
            case quotePairTwo:
                *target++=(char16_t)((byteOne<<8)|b);
                state=readCommand;
                goto fastSingle;
            case quoteOne:
                if(b<0x80) {
                    /* all static offsets are in the BMP */
                    *target++=(char16_t)(staticOffsets[quoteWindow]+b);
                } else {
                    /* write from dynamic window */
                    uint32_t c=scsu->toUDynamicOffsets[quoteWindow]+(b&0x7f);
                    if(c<=0xffff) {
                        *target++=(char16_t)c;
                    } else {
                        /* output surrogate pair */
                        *target++=(char16_t)(0xd7c0+(c>>10));
                        if(target<targetLimit) {
                            *target++=(char16_t)(0xdc00|(c&0x3ff));
                        } else {
                            /* target overflow */
                            cnv->UCharErrorBuffer[0]=(char16_t)(0xdc00|(c&0x3ff));
                            cnv->UCharErrorBufferLength=1;
                            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                            goto endloop;
                        }
                    }
                }
                state=readCommand;
                goto fastSingle;
            case definePairOne:
                dynamicWindow=(int8_t)((b>>5)&7);
                byteOne=(uint8_t)(b&0x1f);
                cnv->toUBytes[1]=b;
                cnv->toULength=2;
                state=definePairTwo;
                break;
            case definePairTwo:
                scsu->toUDynamicOffsets[dynamicWindow]=0x10000+(byteOne<<15UL | b<<7UL);
                state=readCommand;
                goto fastSingle;
            case defineOne:
                if(b==0) {
                    /* callback(illegal): Reserved window offset value 0 */
                    cnv->toUBytes[1]=b;
                    cnv->toULength=2;
                    goto endloop;
                } else if(b<gapThreshold) {
                    scsu->toUDynamicOffsets[dynamicWindow]=b<<7UL;
                } else if((uint8_t)(b-gapThreshold)<(reservedStart-gapThreshold)) {
                    scsu->toUDynamicOffsets[dynamicWindow]=(b<<7UL)+gapOffset;
                } else if(b>=fixedThreshold) {
                    scsu->toUDynamicOffsets[dynamicWindow]=fixedOffsets[b-fixedThreshold];
                } else {
                    /* callback(illegal): Reserved window offset value 0xa8..0xf8 */
                    cnv->toUBytes[1]=b;
                    cnv->toULength=2;
                    goto endloop;
                }
                state=readCommand;
                goto fastSingle;
            }
        }
    } else {
        /* fast path for Unicode mode */
        if(state==readCommand) {
fastUnicode:
            while(source+1<sourceLimit && target<targetLimit && (uint8_t)((b=*source)-UC0)>(Urs-UC0)) {
                *target++=(char16_t)((b<<8)|source[1]);
                source+=2;
            }
        }

        /* normal state machine for Unicode mode */
/* unicodeByteMode: */
        while(source<sourceLimit) {
            if(target>=targetLimit) {
                /* target is full */
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                break;
            }
            b=*source++;
            switch(state) {
            case readCommand:
                if((uint8_t)(b-UC0)>(Urs-UC0)) {
                    byteOne=b;
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    state=quotePairTwo;
                } else if(/* UC0<=b && */ b<=UC7) {
                    dynamicWindow=(int8_t)(b-UC0);
                    isSingleByteMode=true;
                    goto fastSingle;
                } else if(/* UD0<=b && */ b<=UD7) {
                    dynamicWindow=(int8_t)(b-UD0);
                    isSingleByteMode=true;
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    state=defineOne;
                    goto singleByteMode;
                } else if(b==UDX) {
                    isSingleByteMode=true;
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    state=definePairOne;
                    goto singleByteMode;
                } else if(b==UQU) {
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    state=quotePairOne;
                } else /* Urs */ {
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                    cnv->toUBytes[0]=b;
                    cnv->toULength=1;
                    goto endloop;
                }
                break;
            case quotePairOne:
                byteOne=b;
                cnv->toUBytes[1]=b;
                cnv->toULength=2;
                state=quotePairTwo;
                break;
            case quotePairTwo:
                *target++=(char16_t)((byteOne<<8)|b);
                state=readCommand;
                goto fastUnicode;
            }
        }
    }
endloop:

    /* set the converter state back into UConverter */
    if(U_FAILURE(*pErrorCode) && *pErrorCode!=U_BUFFER_OVERFLOW_ERROR) {
        /* reset to deal with the next character */
        state=readCommand;
    } else if(state==readCommand) {
        /* not in a multi-byte sequence, reset toULength */
        cnv->toULength=0;
    }
    scsu->toUIsSingleByteMode=isSingleByteMode;
    scsu->toUState=state;
    scsu->toUQuoteWindow=quoteWindow;
    scsu->toUDynamicWindow=dynamicWindow;
    scsu->toUByteOne=byteOne;

    /* write back the updated pointers */
    pArgs->source=(const char *)source;
    pArgs->target=target;
}
U_CDECL_END
/* SCSU-from-Unicode conversion functions ----------------------------------- */

/*
 * This SCSU Encoder is fairly simple but uses all SCSU commands to achieve
 * reasonable results. The lookahead is minimal.
 * Many cases are simple:
 * A character fits directly into the current mode, a dynamic or static window,
 * or is not compressible. These cases are tested first.
 * Real compression heuristics are applied to the rest, in code branches for
 * single/Unicode mode and BMP/supplementary code points.
 * The heuristics used here are extremely simple.
 */

/* get the number of the window that this character is in, or -1 */
static int8_t
getWindow(const uint32_t offsets[8], uint32_t c) {
    int i;
    for(i=0; i<8; ++i) {
        if (c - offsets[i] <= 0x7f) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

/* is the character in the dynamic window starting at the offset, or in the direct-encoded range? */
static UBool
isInOffsetWindowOrDirect(uint32_t offset, uint32_t c) {
    return c<=offset+0x7f &&
          (c>=offset || (c<=0x7f &&
                        (c>=0x20 || (1UL<<c)&0x2601)));
                                /* binary 0010 0110 0000 0001,
                                   check for b==0xd || b==0xa || b==9 || b==0 */
}

/*
 * getNextDynamicWindow returns the next dynamic window to be redefined
 */
static int8_t
getNextDynamicWindow(SCSUData *scsu) {
    int8_t window=scsu->windowUse[scsu->nextWindowUseIndex];
    if(++scsu->nextWindowUseIndex==8) {
        scsu->nextWindowUseIndex=0;
    }
    return window;
}

/*
 * useDynamicWindow() adjusts
 * windowUse[] and nextWindowUseIndex for the algorithm to choose
 * the next dynamic window to be defined;
 * a subclass may override it and provide its own algorithm.
 */
static void
useDynamicWindow(SCSUData *scsu, int8_t window) {
    /*
     * move the existing window, which just became the most recently used one,
     * up in windowUse[] to nextWindowUseIndex-1
     */

    /* first, find the index of the window - backwards to favor the more recently used windows */
    int i, j;

    i=scsu->nextWindowUseIndex;
    do {
        if(--i<0) {
            i=7;
        }
    } while(scsu->windowUse[i]!=window);

    /* now copy each windowUse[i+1] to [i] */
    j=i+1;
    if(j==8) {
        j=0;
    }
    while(j!=scsu->nextWindowUseIndex) {
        scsu->windowUse[i]=scsu->windowUse[j];
        i=j;
        if(++j==8) { j=0; }
    }

    /* finally, set the window into the most recently used index */
    scsu->windowUse[i]=window;
}

/*
 * calculate the offset and the code for a dynamic window that contains the character
 * takes fixed offsets into account
 * the offset of the window is stored in the offset variable,
 * the code is returned
 *
 * return offset code: -1 none  <=0xff code for SDn/UDn  else code for SDX/UDX, subtract 0x200 to get the true code
 */
static int
getDynamicOffset(uint32_t c, uint32_t *pOffset) {
    int i;

    for(i=0; i<7; ++i) {
        if (c - fixedOffsets[i] <= 0x7f) {
            *pOffset=fixedOffsets[i];
            return 0xf9+i;
        }
    }

    if(c<0x80) {
        /* No dynamic window for US-ASCII. */
        return -1;
    } else if(c<0x3400 ||
              c - 0x10000 < 0x14000 - 0x10000 ||
              c - 0x1d000 <= 0x1ffff - 0x1d000
    ) {
        /* This character is in a code range for a "small", i.e., reasonably windowable, script. */
        *pOffset=c&0x7fffff80;
        return static_cast<int>(c >> 7);
    } else if(0xe000<=c && c!=0xfeff && c<0xfff0) {
        /* For these characters we need to take the gapOffset into account. */
        *pOffset=c&0x7fffff80;
        return static_cast<int>((c - gapOffset) >> 7);
    } else {
        return -1;
    }
}
U_CDECL_BEGIN
/*
 * Idea for compression:
 *  - save SCSUData and other state before really starting work
 *  - at endloop, see if compression could be better with just unicode mode
 *  - don't do this if a callback has been called
 *  - if unicode mode would be smaller, then override the results with it - may need SCU at the beginning
 *  - different buffer handling!
 *
 * Drawback or need for corrective handling:
 * it is desirable to encode U+feff as SQU fe ff for the SCSU signature, and
 * it is desirable to start a document in US-ASCII/Latin-1 for as long as possible
 * not only for compression but also for HTML/XML documents with following charset/encoding announcers.
 *
 * How to achieve both?
 *  - Only replace the result after an SDX or SCU?
 */

static void U_CALLCONV
_SCSUFromUnicodeWithOffsets(UConverterFromUnicodeArgs *pArgs,
                            UErrorCode *pErrorCode) {
    UConverter *cnv;
    SCSUData *scsu;
    const char16_t *source, *sourceLimit;
    uint8_t *target;
    int32_t targetCapacity;
    int32_t *offsets;

    UBool isSingleByteMode;
    uint8_t dynamicWindow;
    uint32_t currentOffset;

    uint32_t c, delta;

    int32_t sourceIndex, nextSourceIndex;

    int32_t length;

    /* variables for compression heuristics */
    uint32_t offset;
    char16_t lead, trail;
    int code;
    int8_t window;

    /* set up the local pointers */
    cnv=pArgs->converter;
    scsu=(SCSUData *)cnv->extraInfo;

    /* set up the local pointers */
    source=pArgs->source;
    sourceLimit=pArgs->sourceLimit;
    target=(uint8_t *)pArgs->target;
    targetCapacity=(int32_t)(pArgs->targetLimit-pArgs->target);
    offsets=pArgs->offsets;

    /* get the state machine state */
    isSingleByteMode=scsu->fromUIsSingleByteMode;
    dynamicWindow=scsu->fromUDynamicWindow;
    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow];

    c=cnv->fromUChar32;

    /* sourceIndex=-1 if the current character began in the previous buffer */
    sourceIndex= c==0 ? 0 : -1;
    nextSourceIndex=0;

    /* similar conversion "loop" as in toUnicode */
loop:
    if(isSingleByteMode) {
        if(c!=0 && targetCapacity>0) {
            goto getTrailSingle;
        }

        /* state machine for single-byte mode */
/* singleByteMode: */
        while(source<sourceLimit) {
            if(targetCapacity<=0) {
                /* target is full */
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                break;
            }
            c=*source++;
            ++nextSourceIndex;

            if((c-0x20)<=0x5f) {
                /* pass US-ASCII graphic character through */
                *target++=(uint8_t)c;
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                --targetCapacity;
            } else if(c<0x20) {
                if((1UL<<c)&0x2601 /* binary 0010 0110 0000 0001, check for b==0xd || b==0xa || b==9 || b==0 */) {
                    /* CR/LF/TAB/NUL */
                    *target++=(uint8_t)c;
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                    --targetCapacity;
                } else {
                    /* quote C0 control character */
                    c|=SQ0<<8;
                    length=2;
                    goto outputBytes;
                }
            } else if((delta=c-currentOffset)<=0x7f) {
                /* use the current dynamic window */
                *target++=(uint8_t)(delta|0x80);
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                --targetCapacity;
            } else if(U16_IS_SURROGATE(c)) {
                if(U16_IS_SURROGATE_LEAD(c)) {
getTrailSingle:
                    lead=(char16_t)c;
                    if(source<sourceLimit) {
                        /* test the following code unit */
                        trail=*source;
                        if(U16_IS_TRAIL(trail)) {
                            ++source;
                            ++nextSourceIndex;
                            c=U16_GET_SUPPLEMENTARY(c, trail);
                            /* convert this surrogate code point */
                            /* exit this condition tree */
                        } else {
                            /* this is an unmatched lead code unit (1st surrogate) */
                            /* callback(illegal) */
                            *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                            goto endloop;
                        }
                    } else {
                        /* no more input */
                        break;
                    }
                } else {
                    /* this is an unmatched trail code unit (2nd surrogate) */
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                    goto endloop;
                }

                /* compress supplementary character U+10000..U+10ffff */
                if((delta=c-currentOffset)<=0x7f) {
                    /* use the current dynamic window */
                    *target++=(uint8_t)(delta|0x80);
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                    --targetCapacity;
                } else if((window=getWindow(scsu->fromUDynamicOffsets, c))>=0) {
                    /* there is a dynamic window that contains this character, change to it */
                    dynamicWindow=window;
                    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow];
                    useDynamicWindow(scsu, dynamicWindow);
                    c=((uint32_t)(SC0+dynamicWindow)<<8)|(c-currentOffset)|0x80;
                    length=2;
                    goto outputBytes;
                } else if((code=getDynamicOffset(c, &offset))>=0) {
                    /* might check if there are more characters in this window to come */
                    /* define an extended window with this character */
                    code-=0x200;
                    dynamicWindow=getNextDynamicWindow(scsu);
                    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow]=offset;
                    useDynamicWindow(scsu, dynamicWindow);
                    c=((uint32_t)SDX<<24)|((uint32_t)dynamicWindow<<21)|((uint32_t)code<<8)|(c-currentOffset)|0x80;
                    length=4;
                    goto outputBytes;
                } else {
                    /* change to Unicode mode and output this (lead, trail) pair */
                    isSingleByteMode=false;
                    *target++=(uint8_t)SCU;
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                    --targetCapacity;
                    c=((uint32_t)lead<<16)|trail;
                    length=4;
                    goto outputBytes;
                }
            } else if(c<0xa0) {
                /* quote C1 control character */
                c=(c&0x7f)|(SQ0+1)<<8; /* SQ0+1==SQ1 */
                length=2;
                goto outputBytes;
            } else if(c==0xfeff || c>=0xfff0) {
                /* quote signature character=byte order mark and specials */
                c|=SQU<<16;
                length=3;
                goto outputBytes;
            } else {
                /* compress all other BMP characters */
                if((window=getWindow(scsu->fromUDynamicOffsets, c))>=0) {
                    /* there is a window defined that contains this character - switch to it or quote from it? */
                    if(source>=sourceLimit || isInOffsetWindowOrDirect(scsu->fromUDynamicOffsets[window], *source)) {
                        /* change to dynamic window */
                        dynamicWindow=window;
                        currentOffset=scsu->fromUDynamicOffsets[dynamicWindow];
                        useDynamicWindow(scsu, dynamicWindow);
                        c=((uint32_t)(SC0+dynamicWindow)<<8)|(c-currentOffset)|0x80;
                        length=2;
                        goto outputBytes;
                    } else {
                        /* quote from dynamic window */
                        c=((uint32_t)(SQ0+window)<<8)|(c-scsu->fromUDynamicOffsets[window])|0x80;
                        length=2;
                        goto outputBytes;
                    }
                } else if((window=getWindow(staticOffsets, c))>=0) {
                    /* quote from static window */
                    c=((uint32_t)(SQ0+window)<<8)|(c-staticOffsets[window]);
                    length=2;
                    goto outputBytes;
                } else if((code=getDynamicOffset(c, &offset))>=0) {
                    /* define a dynamic window with this character */
                    dynamicWindow=getNextDynamicWindow(scsu);
                    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow]=offset;
                    useDynamicWindow(scsu, dynamicWindow);
                    c=((uint32_t)(SD0+dynamicWindow)<<16)|((uint32_t)code<<8)|(c-currentOffset)|0x80;
                    length=3;
                    goto outputBytes;
                } else if ((c - 0x3400) < (0xd800 - 0x3400) &&
                           (source >= sourceLimit || (uint32_t)(*source - 0x3400) < (0xd800 - 0x3400))
                ) {
                    /*
                     * this character is not compressible (a BMP ideograph or similar);
                     * switch to Unicode mode if this is the last character in the block
                     * or there is at least one more ideograph following immediately
                     */
                    isSingleByteMode=false;
                    c|=SCU<<16;
                    length=3;
                    goto outputBytes;
                } else {
                    /* quote Unicode */
                    c|=SQU<<16;
                    length=3;
                    goto outputBytes;
                }
            }

            /* normal end of conversion: prepare for a new character */
            c=0;
            sourceIndex=nextSourceIndex;
        }
    } else {
        if(c!=0 && targetCapacity>0) {
            goto getTrailUnicode;
        }

        /* state machine for Unicode mode */
/* unicodeByteMode: */
        while(source<sourceLimit) {
            if(targetCapacity<=0) {
                /* target is full */
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                break;
            }
            c=*source++;
            ++nextSourceIndex;

            if ((c - 0x3400) < (0xd800 - 0x3400)) {
                /* not compressible, write character directly */
                if(targetCapacity>=2) {
                    *target++=(uint8_t)(c>>8);
                    *target++=(uint8_t)c;
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                        *offsets++=sourceIndex;
                    }
                    targetCapacity-=2;
                } else {
                    length=2;
                    goto outputBytes;
                }
            } else if (c - 0x3400 >= 0xf300 - 0x3400 /* c<0x3400 || c>=0xf300 */) {
                /* compress BMP character if the following one is not an uncompressible ideograph */
                if(!(source<sourceLimit && (uint32_t)(*source-0x3400)<(0xd800-0x3400))) {
                    if (c - 0x30 < 10 || c - 0x61 < 26 || c - 0x41 < 26) {
                        /* ASCII digit or letter */
                        isSingleByteMode=true;
                        c|=((uint32_t)(UC0+dynamicWindow)<<8)|c;
                        length=2;
                        goto outputBytes;
                    } else if((window=getWindow(scsu->fromUDynamicOffsets, c))>=0) {
                        /* there is a dynamic window that contains this character, change to it */
                        isSingleByteMode=true;
                        dynamicWindow=window;
                        currentOffset=scsu->fromUDynamicOffsets[dynamicWindow];
                        useDynamicWindow(scsu, dynamicWindow);
                        c=((uint32_t)(UC0+dynamicWindow)<<8)|(c-currentOffset)|0x80;
                        length=2;
                        goto outputBytes;
                    } else if((code=getDynamicOffset(c, &offset))>=0) {
                        /* define a dynamic window with this character */
                        isSingleByteMode=true;
                        dynamicWindow=getNextDynamicWindow(scsu);
                        currentOffset=scsu->fromUDynamicOffsets[dynamicWindow]=offset;
                        useDynamicWindow(scsu, dynamicWindow);
                        c=((uint32_t)(UD0+dynamicWindow)<<16)|((uint32_t)code<<8)|(c-currentOffset)|0x80;
                        length=3;
                        goto outputBytes;
                    }
                }

                /* don't know how to compress this character, just write it directly */
                length=2;
                goto outputBytes;
            } else if(c<0xe000) {
                /* c is a surrogate */
                if(U16_IS_SURROGATE_LEAD(c)) {
getTrailUnicode:
                    lead=(char16_t)c;
                    if(source<sourceLimit) {
                        /* test the following code unit */
                        trail=*source;
                        if(U16_IS_TRAIL(trail)) {
                            ++source;
                            ++nextSourceIndex;
                            c=U16_GET_SUPPLEMENTARY(c, trail);
                            /* convert this surrogate code point */
                            /* exit this condition tree */
                        } else {
                            /* this is an unmatched lead code unit (1st surrogate) */
                            /* callback(illegal) */
                            *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                            goto endloop;
                        }
                    } else {
                        /* no more input */
                        break;
                    }
                } else {
                    /* this is an unmatched trail code unit (2nd surrogate) */
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                    goto endloop;
                }

                /* compress supplementary character */
                if( (window=getWindow(scsu->fromUDynamicOffsets, c))>=0 &&
                    !(source<sourceLimit && (uint32_t)(*source-0x3400)<(0xd800-0x3400))
                ) {
                    /*
                     * there is a dynamic window that contains this character and
                     * the following character is not uncompressible,
                     * change to the window
                     */
                    isSingleByteMode=true;
                    dynamicWindow=window;
                    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow];
                    useDynamicWindow(scsu, dynamicWindow);
                    c=((uint32_t)(UC0+dynamicWindow)<<8)|(c-currentOffset)|0x80;
                    length=2;
                    goto outputBytes;
                } else if(source<sourceLimit && lead==*source && /* too lazy to check trail in same window as source[1] */
                          (code=getDynamicOffset(c, &offset))>=0
                ) {
                    /* two supplementary characters in (probably) the same window - define an extended one */
                    isSingleByteMode=true;
                    code-=0x200;
                    dynamicWindow=getNextDynamicWindow(scsu);
                    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow]=offset;
                    useDynamicWindow(scsu, dynamicWindow);
                    c=((uint32_t)UDX<<24)|((uint32_t)dynamicWindow<<21)|((uint32_t)code<<8)|(c-currentOffset)|0x80;
                    length=4;
                    goto outputBytes;
                } else {
                    /* don't know how to compress this character, just write it directly */
                    c=((uint32_t)lead<<16)|trail;
                    length=4;
                    goto outputBytes;
                }
            } else /* 0xe000<=c<0xf300 */ {
                /* quote to avoid SCSU tags */
                c|=UQU<<16;
                length=3;
                goto outputBytes;
            }

            /* normal end of conversion: prepare for a new character */
            c=0;
            sourceIndex=nextSourceIndex;
        }
    }
endloop:

    /* set the converter state back into UConverter */
    scsu->fromUIsSingleByteMode=isSingleByteMode;
    scsu->fromUDynamicWindow=dynamicWindow;

    cnv->fromUChar32=c;

    /* write back the updated pointers */
    pArgs->source=source;
    pArgs->target=(char *)target;
    pArgs->offsets=offsets;
    return;

outputBytes:
    /* write the output character bytes from c and length [code copied from ucnvmbcs.c] */
    /* from the first if in the loop we know that targetCapacity>0 */
    if(length<=targetCapacity) {
        if(offsets==nullptr) {
            switch(length) {
                /* each branch falls through to the next one */
            case 4:
                *target++=(uint8_t)(c>>24);
                U_FALLTHROUGH;
            case 3:
                *target++=(uint8_t)(c>>16);
                U_FALLTHROUGH;
            case 2:
                *target++=(uint8_t)(c>>8);
                U_FALLTHROUGH;
            case 1:
                *target++=(uint8_t)c;
                U_FALLTHROUGH;
            default:
                /* will never occur */
                break;
            }
        } else {
            switch(length) {
                /* each branch falls through to the next one */
            case 4:
                *target++=(uint8_t)(c>>24);
                *offsets++=sourceIndex;
                U_FALLTHROUGH;
            case 3:
                *target++=(uint8_t)(c>>16);
                *offsets++=sourceIndex;
                U_FALLTHROUGH;
            case 2:
                *target++=(uint8_t)(c>>8);
                *offsets++=sourceIndex;
                U_FALLTHROUGH;
            case 1:
                *target++=(uint8_t)c;
                *offsets++=sourceIndex;
                U_FALLTHROUGH;
            default:
                /* will never occur */
                break;
            }
        }
        targetCapacity-=length;

        /* normal end of conversion: prepare for a new character */
        c=0;
        sourceIndex=nextSourceIndex;
        goto loop;
    } else {
        uint8_t *p;

        /*
         * We actually do this backwards here:
         * In order to save an intermediate variable, we output
         * first to the overflow buffer what does not fit into the
         * regular target.
         */
        /* we know that 0<=targetCapacity<length<=4 */
        /* targetCapacity==0 when SCU+supplementary where SCU used up targetCapacity==1 */
        length-=targetCapacity;
        p=(uint8_t *)cnv->charErrorBuffer;
        switch(length) {
            /* each branch falls through to the next one */
        case 4:
            *p++=(uint8_t)(c>>24);
            U_FALLTHROUGH;
        case 3:
            *p++=(uint8_t)(c>>16);
            U_FALLTHROUGH;
        case 2:
            *p++=(uint8_t)(c>>8);
            U_FALLTHROUGH;
        case 1:
            *p=(uint8_t)c;
            U_FALLTHROUGH;
        default:
            /* will never occur */
            break;
        }
        cnv->charErrorBufferLength=(int8_t)length;

        /* now output what fits into the regular target */
        c>>=8*length; /* length was reduced by targetCapacity */
        switch(targetCapacity) {
            /* each branch falls through to the next one */
        case 3:
            *target++=(uint8_t)(c>>16);
            if(offsets!=nullptr) {
                *offsets++=sourceIndex;
            }
            U_FALLTHROUGH;
        case 2:
            *target++=(uint8_t)(c>>8);
            if(offsets!=nullptr) {
                *offsets++=sourceIndex;
            }
            U_FALLTHROUGH;
        case 1:
            *target++=(uint8_t)c;
            if(offsets!=nullptr) {
                *offsets++=sourceIndex;
            }
            U_FALLTHROUGH;
        default:
            break;
        }

        /* target overflow */
        targetCapacity=0;
        *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
        c=0;
        goto endloop;
    }
}

/*
 * Identical to _SCSUFromUnicodeWithOffsets but without offset handling.
 * If a change is made in the original function, then either
 * change this function the same way or
 * re-copy the original function and remove the variables
 * offsets, sourceIndex, and nextSourceIndex.
 */
static void U_CALLCONV
_SCSUFromUnicode(UConverterFromUnicodeArgs *pArgs,
                 UErrorCode *pErrorCode) {
    UConverter *cnv;
    SCSUData *scsu;
    const char16_t *source, *sourceLimit;
    uint8_t *target;
    int32_t targetCapacity;

    UBool isSingleByteMode;
    uint8_t dynamicWindow;
    uint32_t currentOffset;

    uint32_t c, delta;

    int32_t length;

    /* variables for compression heuristics */
    uint32_t offset;
    char16_t lead, trail;
    int code;
    int8_t window;

    /* set up the local pointers */
    cnv=pArgs->converter;
    scsu=(SCSUData *)cnv->extraInfo;

    /* set up the local pointers */
    source=pArgs->source;
    sourceLimit=pArgs->sourceLimit;
    target=(uint8_t *)pArgs->target;
    targetCapacity=(int32_t)(pArgs->targetLimit-pArgs->target);

    /* get the state machine state */
    isSingleByteMode=scsu->fromUIsSingleByteMode;
    dynamicWindow=scsu->fromUDynamicWindow;
    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow];

    c=cnv->fromUChar32;

    /* similar conversion "loop" as in toUnicode */
loop:
    if(isSingleByteMode) {
        if(c!=0 && targetCapacity>0) {
            goto getTrailSingle;
        }

        /* state machine for single-byte mode */
/* singleByteMode: */
        while(source<sourceLimit) {
            if(targetCapacity<=0) {
                /* target is full */
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                break;
            }
            c=*source++;

            if((c-0x20)<=0x5f) {
                /* pass US-ASCII graphic character through */
                *target++=(uint8_t)c;
                --targetCapacity;
            } else if(c<0x20) {
                if((1UL<<c)&0x2601 /* binary 0010 0110 0000 0001, check for b==0xd || b==0xa || b==9 || b==0 */) {
                    /* CR/LF/TAB/NUL */
                    *target++=(uint8_t)c;
                    --targetCapacity;
                } else {
                    /* quote C0 control character */
                    c|=SQ0<<8;
                    length=2;
                    goto outputBytes;
                }
            } else if((delta=c-currentOffset)<=0x7f) {
                /* use the current dynamic window */
                *target++=(uint8_t)(delta|0x80);
                --targetCapacity;
            } else if(U16_IS_SURROGATE(c)) {
                if(U16_IS_SURROGATE_LEAD(c)) {
getTrailSingle:
                    lead=(char16_t)c;
                    if(source<sourceLimit) {
                        /* test the following code unit */
                        trail=*source;
                        if(U16_IS_TRAIL(trail)) {
                            ++source;
                            c=U16_GET_SUPPLEMENTARY(c, trail);
                            /* convert this surrogate code point */
                            /* exit this condition tree */
                        } else {
                            /* this is an unmatched lead code unit (1st surrogate) */
                            /* callback(illegal) */
                            *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                            goto endloop;
                        }
                    } else {
                        /* no more input */
                        break;
                    }
                } else {
                    /* this is an unmatched trail code unit (2nd surrogate) */
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                    goto endloop;
                }

                /* compress supplementary character U+10000..U+10ffff */
                if((delta=c-currentOffset)<=0x7f) {
                    /* use the current dynamic window */
                    *target++=(uint8_t)(delta|0x80);
                    --targetCapacity;
                } else if((window=getWindow(scsu->fromUDynamicOffsets, c))>=0) {
                    /* there is a dynamic window that contains this character, change to it */
                    dynamicWindow=window;
                    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow];
                    useDynamicWindow(scsu, dynamicWindow);
                    c=((uint32_t)(SC0+dynamicWindow)<<8)|(c-currentOffset)|0x80;
                    length=2;
                    goto outputBytes;
                } else if((code=getDynamicOffset(c, &offset))>=0) {
                    /* might check if there are more characters in this window to come */
                    /* define an extended window with this character */
                    code-=0x200;
                    dynamicWindow=getNextDynamicWindow(scsu);
                    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow]=offset;
                    useDynamicWindow(scsu, dynamicWindow);
                    c=((uint32_t)SDX<<24)|((uint32_t)dynamicWindow<<21)|((uint32_t)code<<8)|(c-currentOffset)|0x80;
                    length=4;
                    goto outputBytes;
                } else {
                    /* change to Unicode mode and output this (lead, trail) pair */
                    isSingleByteMode=false;
                    *target++=(uint8_t)SCU;
                    --targetCapacity;
                    c=((uint32_t)lead<<16)|trail;
                    length=4;
                    goto outputBytes;
                }
            } else if(c<0xa0) {
                /* quote C1 control character */
                c=(c&0x7f)|(SQ0+1)<<8; /* SQ0+1==SQ1 */
                length=2;
                goto outputBytes;
            } else if(c==0xfeff || c>=0xfff0) {
                /* quote signature character=byte order mark and specials */
                c|=SQU<<16;
                length=3;
                goto outputBytes;
            } else {
                /* compress all other BMP characters */
                if((window=getWindow(scsu->fromUDynamicOffsets, c))>=0) {
                    /* there is a window defined that contains this character - switch to it or quote from it? */
                    if(source>=sourceLimit || isInOffsetWindowOrDirect(scsu->fromUDynamicOffsets[window], *source)) {
                        /* change to dynamic window */
                        dynamicWindow=window;
                        currentOffset=scsu->fromUDynamicOffsets[dynamicWindow];
                        useDynamicWindow(scsu, dynamicWindow);
                        c=((uint32_t)(SC0+dynamicWindow)<<8)|(c-currentOffset)|0x80;
                        length=2;
                        goto outputBytes;
                    } else {
                        /* quote from dynamic window */
                        c=((uint32_t)(SQ0+window)<<8)|(c-scsu->fromUDynamicOffsets[window])|0x80;
                        length=2;
                        goto outputBytes;
                    }
                } else if((window=getWindow(staticOffsets, c))>=0) {
                    /* quote from static window */
                    c=((uint32_t)(SQ0+window)<<8)|(c-staticOffsets[window]);
                    length=2;
                    goto outputBytes;
                } else if((code=getDynamicOffset(c, &offset))>=0) {
                    /* define a dynamic window with this character */
                    dynamicWindow=getNextDynamicWindow(scsu);
                    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow]=offset;
                    useDynamicWindow(scsu, dynamicWindow);
                    c=((uint32_t)(SD0+dynamicWindow)<<16)|((uint32_t)code<<8)|(c-currentOffset)|0x80;
                    length=3;
                    goto outputBytes;
                } else if (c - 0x3400 < 0xd800 - 0x3400 &&
                           (source >= sourceLimit || static_cast<uint32_t>(*source - 0x3400) < 0xd800 - 0x3400)
                ) {
                    /*
                     * this character is not compressible (a BMP ideograph or similar);
                     * switch to Unicode mode if this is the last character in the block
                     * or there is at least one more ideograph following immediately
                     */
                    isSingleByteMode=false;
                    c|=SCU<<16;
                    length=3;
                    goto outputBytes;
                } else {
                    /* quote Unicode */
                    c|=SQU<<16;
                    length=3;
                    goto outputBytes;
                }
            }

            /* normal end of conversion: prepare for a new character */
            c=0;
        }
    } else {
        if(c!=0 && targetCapacity>0) {
            goto getTrailUnicode;
        }

        /* state machine for Unicode mode */
/* unicodeByteMode: */
        while(source<sourceLimit) {
            if(targetCapacity<=0) {
                /* target is full */
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                break;
            }
            c=*source++;

            if (c - 0x3400 < 0xd800 - 0x3400) {
                /* not compressible, write character directly */
                if(targetCapacity>=2) {
                    *target++=(uint8_t)(c>>8);
                    *target++=(uint8_t)c;
                    targetCapacity-=2;
                } else {
                    length=2;
                    goto outputBytes;
                }
            } else if (c - 0x3400 >= 0xf300 - 0x3400 /* c<0x3400 || c>=0xf300 */) {
                /* compress BMP character if the following one is not an uncompressible ideograph */
                if(!(source<sourceLimit && (uint32_t)(*source-0x3400)<(0xd800-0x3400))) {
                    if (c - 0x30 < 10 || c - 0x61 < 26 || c - 0x41 < 26) {
                        /* ASCII digit or letter */
                        isSingleByteMode=true;
                        c|=((uint32_t)(UC0+dynamicWindow)<<8)|c;
                        length=2;
                        goto outputBytes;
                    } else if((window=getWindow(scsu->fromUDynamicOffsets, c))>=0) {
                        /* there is a dynamic window that contains this character, change to it */
                        isSingleByteMode=true;
                        dynamicWindow=window;
                        currentOffset=scsu->fromUDynamicOffsets[dynamicWindow];
                        useDynamicWindow(scsu, dynamicWindow);
                        c=((uint32_t)(UC0+dynamicWindow)<<8)|(c-currentOffset)|0x80;
                        length=2;
                        goto outputBytes;
                    } else if((code=getDynamicOffset(c, &offset))>=0) {
                        /* define a dynamic window with this character */
                        isSingleByteMode=true;
                        dynamicWindow=getNextDynamicWindow(scsu);
                        currentOffset=scsu->fromUDynamicOffsets[dynamicWindow]=offset;
                        useDynamicWindow(scsu, dynamicWindow);
                        c=((uint32_t)(UD0+dynamicWindow)<<16)|((uint32_t)code<<8)|(c-currentOffset)|0x80;
                        length=3;
                        goto outputBytes;
                    }
                }

                /* don't know how to compress this character, just write it directly */
                length=2;
                goto outputBytes;
            } else if(c<0xe000) {
                /* c is a surrogate */
                if(U16_IS_SURROGATE_LEAD(c)) {
getTrailUnicode:
                    lead=(char16_t)c;
                    if(source<sourceLimit) {
                        /* test the following code unit */
                        trail=*source;
                        if(U16_IS_TRAIL(trail)) {
                            ++source;
                            c=U16_GET_SUPPLEMENTARY(c, trail);
                            /* convert this surrogate code point */
                            /* exit this condition tree */
                        } else {
                            /* this is an unmatched lead code unit (1st surrogate) */
                            /* callback(illegal) */
                            *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                            goto endloop;
                        }
                    } else {
                        /* no more input */
                        break;
                    }
                } else {
                    /* this is an unmatched trail code unit (2nd surrogate) */
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                    goto endloop;
                }

                /* compress supplementary character */
                if( (window=getWindow(scsu->fromUDynamicOffsets, c))>=0 &&
                    !(source<sourceLimit && (uint32_t)(*source-0x3400)<(0xd800-0x3400))
                ) {
                    /*
                     * there is a dynamic window that contains this character and
                     * the following character is not uncompressible,
                     * change to the window
                     */
                    isSingleByteMode=true;
                    dynamicWindow=window;
                    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow];
                    useDynamicWindow(scsu, dynamicWindow);
                    c=((uint32_t)(UC0+dynamicWindow)<<8)|(c-currentOffset)|0x80;
                    length=2;
                    goto outputBytes;
                } else if(source<sourceLimit && lead==*source && /* too lazy to check trail in same window as source[1] */
                          (code=getDynamicOffset(c, &offset))>=0
                ) {
                    /* two supplementary characters in (probably) the same window - define an extended one */
                    isSingleByteMode=true;
                    code-=0x200;
                    dynamicWindow=getNextDynamicWindow(scsu);
                    currentOffset=scsu->fromUDynamicOffsets[dynamicWindow]=offset;
                    useDynamicWindow(scsu, dynamicWindow);
                    c=((uint32_t)UDX<<24)|((uint32_t)dynamicWindow<<21)|((uint32_t)code<<8)|(c-currentOffset)|0x80;
                    length=4;
                    goto outputBytes;
                } else {
                    /* don't know how to compress this character, just write it directly */
                    c=((uint32_t)lead<<16)|trail;
                    length=4;
                    goto outputBytes;
                }
            } else /* 0xe000<=c<0xf300 */ {
                /* quote to avoid SCSU tags */
                c|=UQU<<16;
                length=3;
                goto outputBytes;
            }

            /* normal end of conversion: prepare for a new character */
            c=0;
        }
    }
endloop:

    /* set the converter state back into UConverter */
    scsu->fromUIsSingleByteMode=isSingleByteMode;
    scsu->fromUDynamicWindow=dynamicWindow;

    cnv->fromUChar32=c;

    /* write back the updated pointers */
    pArgs->source=source;
    pArgs->target=(char *)target;
    return;

outputBytes:
    /* write the output character bytes from c and length [code copied from ucnvmbcs.c] */
    /* from the first if in the loop we know that targetCapacity>0 */
    if(length<=targetCapacity) {
        switch(length) {
            /* each branch falls through to the next one */
        case 4:
            *target++=(uint8_t)(c>>24);
            U_FALLTHROUGH;
        case 3:
            *target++=(uint8_t)(c>>16);
            U_FALLTHROUGH;
        case 2:
            *target++=(uint8_t)(c>>8);
            U_FALLTHROUGH;
        case 1:
            *target++=(uint8_t)c;
            U_FALLTHROUGH;
        default:
            /* will never occur */
            break;
        }
        targetCapacity-=length;

        /* normal end of conversion: prepare for a new character */
        c=0;
        goto loop;
    } else {
        uint8_t *p;

        /*
         * We actually do this backwards here:
         * In order to save an intermediate variable, we output
         * first to the overflow buffer what does not fit into the
         * regular target.
         */
        /* we know that 0<=targetCapacity<length<=4 */
        /* targetCapacity==0 when SCU+supplementary where SCU used up targetCapacity==1 */
        length-=targetCapacity;
        p=(uint8_t *)cnv->charErrorBuffer;
        switch(length) {
            /* each branch falls through to the next one */
        case 4:
            *p++=(uint8_t)(c>>24);
            U_FALLTHROUGH;
        case 3:
            *p++=(uint8_t)(c>>16);
            U_FALLTHROUGH;
        case 2:
            *p++=(uint8_t)(c>>8);
            U_FALLTHROUGH;
        case 1:
            *p=(uint8_t)c;
            U_FALLTHROUGH;
        default:
            /* will never occur */
            break;
        }
        cnv->charErrorBufferLength=(int8_t)length;

        /* now output what fits into the regular target */
        c = (length == 4) ? 0 : c >> 8*length; /* length was reduced by targetCapacity */
        switch(targetCapacity) {
            /* each branch falls through to the next one */
        case 3:
            *target++=(uint8_t)(c>>16);
            U_FALLTHROUGH;
        case 2:
            *target++=(uint8_t)(c>>8);
            U_FALLTHROUGH;
        case 1:
            *target++=(uint8_t)c;
            U_FALLTHROUGH;
        default:
            break;
        }

        /* target overflow */
        targetCapacity=0;
        *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
        c=0;
        goto endloop;
    }
}

/* miscellaneous ------------------------------------------------------------ */

static const char *  U_CALLCONV
_SCSUGetName(const UConverter *cnv) {
    SCSUData *scsu=(SCSUData *)cnv->extraInfo;

    switch(scsu->locale) {
    case l_ja:
        return "SCSU,locale=ja";
    default:
        return "SCSU";
    }
}

/* structure for SafeClone calculations */
struct cloneSCSUStruct
{
    UConverter cnv;
    SCSUData mydata;
};

static UConverter *  U_CALLCONV
_SCSUSafeClone(const UConverter *cnv, 
               void *stackBuffer, 
               int32_t *pBufferSize, 
               UErrorCode *status)
{
    struct cloneSCSUStruct * localClone;
    int32_t bufferSizeNeeded = sizeof(struct cloneSCSUStruct);

    if (U_FAILURE(*status)){
        return nullptr;
    }

    if (*pBufferSize == 0){ /* 'preflighting' request - set needed size into *pBufferSize */
        *pBufferSize = bufferSizeNeeded;
        return nullptr;
    }

    localClone = (struct cloneSCSUStruct *)stackBuffer;
    /* ucnv.c/ucnv_safeClone() copied the main UConverter already */

    uprv_memcpy(&localClone->mydata, cnv->extraInfo, sizeof(SCSUData));
    localClone->cnv.extraInfo = &localClone->mydata;
    localClone->cnv.isExtraLocal = true;

    return &localClone->cnv;
}
U_CDECL_END

static const UConverterImpl _SCSUImpl={
    UCNV_SCSU,

    nullptr,
    nullptr,

    _SCSUOpen,
    _SCSUClose,
    _SCSUReset,

    _SCSUToUnicode,
    _SCSUToUnicodeWithOffsets,
    _SCSUFromUnicode,
    _SCSUFromUnicodeWithOffsets,
    nullptr,

    nullptr,
    _SCSUGetName,
    nullptr,
    _SCSUSafeClone,
    ucnv_getCompleteUnicodeSet,
    nullptr,
    nullptr
};

static const UConverterStaticData _SCSUStaticData={
    sizeof(UConverterStaticData),
    "SCSU",
    1212, /* CCSID for SCSU */
    UCNV_IBM, UCNV_SCSU,
    1, 3, /* one char16_t generates at least 1 byte and at most 3 bytes */
    /*
     * The subchar here is ignored because _SCSUOpen() sets U+fffd as a Unicode
     * substitution string.
     */
    { 0x0e, 0xff, 0xfd, 0 }, 3,
    false, false,
    0,
    0,
    { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } /* reserved */
};

const UConverterSharedData _SCSUData=
        UCNV_IMMUTABLE_SHARED_DATA_INITIALIZER(&_SCSUStaticData, &_SCSUImpl);

#endif
