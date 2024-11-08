// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**************************************************************************
*   Copyright (C) 2002-2016 International Business Machines Corporation
*   and others. All rights reserved.
**************************************************************************
*/
//
//  file:  rematch.cpp
//
//         Contains the implementation of class RegexMatcher,
//         which is one of the main API classes for the ICU regular expression package.
//

#include "unicode/utypes.h"
#if !UCONFIG_NO_REGULAR_EXPRESSIONS

#include "unicode/regex.h"
#include "unicode/uniset.h"
#include "unicode/uchar.h"
#include "unicode/ustring.h"
#include "unicode/rbbi.h"
#include "unicode/utf.h"
#include "unicode/utf16.h"
#include "uassert.h"
#include "cmemory.h"
#include "cstr.h"
#include "uvector.h"
#include "uvectr32.h"
#include "uvectr64.h"
#include "regeximp.h"
#include "regexst.h"
#include "regextxt.h"
#include "ucase.h"

// #include <malloc.h>        // Needed for heapcheck testing


U_NAMESPACE_BEGIN

// Default limit for the size of the back track stack, to avoid system
//    failures causedby heap exhaustion.  Units are in 32 bit words, not bytes.
// This value puts ICU's limits higher than most other regexp implementations,
//    which use recursion rather than the heap, and take more storage per
//    backtrack point.
//
static const int32_t DEFAULT_BACKTRACK_STACK_CAPACITY = 8000000;

// Time limit counter constant.
//   Time limits for expression evaluation are in terms of quanta of work by
//   the engine, each of which is 10,000 state saves.
//   This constant determines that state saves per tick number.
static const int32_t TIMER_INITIAL_VALUE = 10000;


// Test for any of the Unicode line terminating characters.
static inline UBool isLineTerminator(UChar32 c) {
    if (c & ~(0x0a | 0x0b | 0x0c | 0x0d | 0x85 | 0x2028 | 0x2029)) {
        return false;
    }
    return (c<=0x0d && c>=0x0a) || c==0x85 || c==0x2028 || c==0x2029;
}

//-----------------------------------------------------------------------------
//
//   Constructor and Destructor
//
//-----------------------------------------------------------------------------
RegexMatcher::RegexMatcher(const RegexPattern *pat)  {
    fDeferredStatus = U_ZERO_ERROR;
    init(fDeferredStatus);
    if (U_FAILURE(fDeferredStatus)) {
        return;
    }
    if (pat==nullptr) {
        fDeferredStatus = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    fPattern = pat;
    init2(RegexStaticSets::gStaticSets->fEmptyText, fDeferredStatus);
}



RegexMatcher::RegexMatcher(const UnicodeString &regexp, const UnicodeString &input,
                           uint32_t flags, UErrorCode &status) {
    init(status);
    if (U_FAILURE(status)) {
        return;
    }
    UParseError    pe;
    fPatternOwned      = RegexPattern::compile(regexp, flags, pe, status);
    fPattern           = fPatternOwned;

    UText inputText = UTEXT_INITIALIZER;
    utext_openConstUnicodeString(&inputText, &input, &status);
    init2(&inputText, status);
    utext_close(&inputText);

    fInputUniStrMaybeMutable = true;
}


RegexMatcher::RegexMatcher(UText *regexp, UText *input,
                           uint32_t flags, UErrorCode &status) {
    init(status);
    if (U_FAILURE(status)) {
        return;
    }
    UParseError    pe;
    fPatternOwned      = RegexPattern::compile(regexp, flags, pe, status);
    if (U_FAILURE(status)) {
        return;
    }

    fPattern           = fPatternOwned;
    init2(input, status);
}


RegexMatcher::RegexMatcher(const UnicodeString &regexp,
                           uint32_t flags, UErrorCode &status) {
    init(status);
    if (U_FAILURE(status)) {
        return;
    }
    UParseError    pe;
    fPatternOwned      = RegexPattern::compile(regexp, flags, pe, status);
    if (U_FAILURE(status)) {
        return;
    }
    fPattern           = fPatternOwned;
    init2(RegexStaticSets::gStaticSets->fEmptyText, status);
}

RegexMatcher::RegexMatcher(UText *regexp,
                           uint32_t flags, UErrorCode &status) {
    init(status);
    if (U_FAILURE(status)) {
        return;
    }
    UParseError    pe;
    fPatternOwned      = RegexPattern::compile(regexp, flags, pe, status);
        if (U_FAILURE(status)) {
        return;
    }

    fPattern           = fPatternOwned;
    init2(RegexStaticSets::gStaticSets->fEmptyText, status);
}




RegexMatcher::~RegexMatcher() {
    delete fStack;
    if (fData != fSmallData) {
        uprv_free(fData);
        fData = nullptr;
    }
    if (fPatternOwned) {
        delete fPatternOwned;
        fPatternOwned = nullptr;
        fPattern = nullptr;
    }

    delete fInput;
    if (fInputText) {
        utext_close(fInputText);
    }
    if (fAltInputText) {
        utext_close(fAltInputText);
    }

    #if UCONFIG_NO_BREAK_ITERATION==0
    delete fWordBreakItr;
    delete fGCBreakItr;
    #endif
}

//
//   init()   common initialization for use by all constructors.
//            Initialize all fields, get the object into a consistent state.
//            This must be done even when the initial status shows an error,
//            so that the object is initialized sufficiently well for the destructor
//            to run safely.
//
void RegexMatcher::init(UErrorCode &status) {
    fPattern           = nullptr;
    fPatternOwned      = nullptr;
    fFrameSize         = 0;
    fRegionStart       = 0;
    fRegionLimit       = 0;
    fAnchorStart       = 0;
    fAnchorLimit       = 0;
    fLookStart         = 0;
    fLookLimit         = 0;
    fActiveStart       = 0;
    fActiveLimit       = 0;
    fTransparentBounds = false;
    fAnchoringBounds   = true;
    fMatch             = false;
    fMatchStart        = 0;
    fMatchEnd          = 0;
    fLastMatchEnd      = -1;
    fAppendPosition    = 0;
    fHitEnd            = false;
    fRequireEnd        = false;
    fStack             = nullptr;
    fFrame             = nullptr;
    fTimeLimit         = 0;
    fTime              = 0;
    fTickCounter       = 0;
    fStackLimit        = DEFAULT_BACKTRACK_STACK_CAPACITY;
    fCallbackFn        = nullptr;
    fCallbackContext   = nullptr;
    fFindProgressCallbackFn      = nullptr;
    fFindProgressCallbackContext = nullptr;
    fTraceDebug        = false;
    fDeferredStatus    = status;
    fData              = fSmallData;
    fWordBreakItr      = nullptr;
    fGCBreakItr        = nullptr;

    fStack             = nullptr;
    fInputText         = nullptr;
    fAltInputText      = nullptr;
    fInput             = nullptr;
    fInputLength       = 0;
    fInputUniStrMaybeMutable = false;
}

//
//  init2()   Common initialization for use by RegexMatcher constructors, part 2.
//            This handles the common setup to be done after the Pattern is available.
//
void RegexMatcher::init2(UText *input, UErrorCode &status) {
    if (U_FAILURE(status)) {
        fDeferredStatus = status;
        return;
    }

    if (fPattern->fDataSize > UPRV_LENGTHOF(fSmallData)) {
        fData = static_cast<int64_t*>(uprv_malloc(fPattern->fDataSize * sizeof(int64_t)));
        if (fData == nullptr) {
            status = fDeferredStatus = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }

    fStack = new UVector64(status);
    if (fStack == nullptr) {
        status = fDeferredStatus = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    reset(input);
    setStackLimit(DEFAULT_BACKTRACK_STACK_CAPACITY, status);
    if (U_FAILURE(status)) {
        fDeferredStatus = status;
        return;
    }
}


static const char16_t BACKSLASH  = 0x5c;
static const char16_t DOLLARSIGN = 0x24;
static const char16_t LEFTBRACKET = 0x7b;
static const char16_t RIGHTBRACKET = 0x7d;

//--------------------------------------------------------------------------------
//
//    appendReplacement
//
//--------------------------------------------------------------------------------
RegexMatcher &RegexMatcher::appendReplacement(UnicodeString &dest,
                                              const UnicodeString &replacement,
                                              UErrorCode &status) {
    UText replacementText = UTEXT_INITIALIZER;

    utext_openConstUnicodeString(&replacementText, &replacement, &status);
    if (U_SUCCESS(status)) {
        UText resultText = UTEXT_INITIALIZER;
        utext_openUnicodeString(&resultText, &dest, &status);

        if (U_SUCCESS(status)) {
            appendReplacement(&resultText, &replacementText, status);
            utext_close(&resultText);
        }
        utext_close(&replacementText);
    }

    return *this;
}

//
//    appendReplacement, UText mode
//
RegexMatcher &RegexMatcher::appendReplacement(UText *dest,
                                              UText *replacement,
                                              UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return *this;
    }
    if (fMatch == false) {
        status = U_REGEX_INVALID_STATE;
        return *this;
    }

    // Copy input string from the end of previous match to start of current match
    int64_t  destLen = utext_nativeLength(dest);
    if (fMatchStart > fAppendPosition) {
        if (UTEXT_FULL_TEXT_IN_CHUNK(fInputText, fInputLength)) {
            destLen += utext_replace(dest, destLen, destLen, fInputText->chunkContents+fAppendPosition,
                                     static_cast<int32_t>(fMatchStart - fAppendPosition), &status);
        } else {
            int32_t len16;
            if (UTEXT_USES_U16(fInputText)) {
                len16 = static_cast<int32_t>(fMatchStart - fAppendPosition);
            } else {
                UErrorCode lengthStatus = U_ZERO_ERROR;
                len16 = utext_extract(fInputText, fAppendPosition, fMatchStart, nullptr, 0, &lengthStatus);
            }
            char16_t* inputChars = static_cast<char16_t*>(uprv_malloc(sizeof(char16_t) * (len16 + 1)));
            if (inputChars == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return *this;
            }
            utext_extract(fInputText, fAppendPosition, fMatchStart, inputChars, len16+1, &status);
            destLen += utext_replace(dest, destLen, destLen, inputChars, len16, &status);
            uprv_free(inputChars);
        }
    }
    fAppendPosition = fMatchEnd;


    // scan the replacement text, looking for substitutions ($n) and \escapes.
    //  TODO:  optimize this loop by efficiently scanning for '$' or '\',
    //         move entire ranges not containing substitutions.
    UTEXT_SETNATIVEINDEX(replacement, 0);
    for (UChar32 c = UTEXT_NEXT32(replacement); U_SUCCESS(status) && c != U_SENTINEL;  c = UTEXT_NEXT32(replacement)) {
        if (c == BACKSLASH) {
            // Backslash Escape.  Copy the following char out without further checks.
            //                    Note:  Surrogate pairs don't need any special handling
            //                           The second half wont be a '$' or a '\', and
            //                           will move to the dest normally on the next
            //                           loop iteration.
            c = UTEXT_CURRENT32(replacement);
            if (c == U_SENTINEL) {
                break;
            }

            if (c==0x55/*U*/ || c==0x75/*u*/) {
                // We have a \udddd or \Udddddddd escape sequence.
                int32_t offset = 0;
                struct URegexUTextUnescapeCharContext context = U_REGEX_UTEXT_UNESCAPE_CONTEXT(replacement);
                UChar32 escapedChar = u_unescapeAt(uregex_utext_unescape_charAt, &offset, INT32_MAX, &context);
                if (escapedChar != static_cast<UChar32>(0xFFFFFFFF)) {
                    if (U_IS_BMP(escapedChar)) {
                        char16_t c16 = static_cast<char16_t>(escapedChar);
                        destLen += utext_replace(dest, destLen, destLen, &c16, 1, &status);
                    } else {
                        char16_t surrogate[2];
                        surrogate[0] = U16_LEAD(escapedChar);
                        surrogate[1] = U16_TRAIL(escapedChar);
                        if (U_SUCCESS(status)) {
                            destLen += utext_replace(dest, destLen, destLen, surrogate, 2, &status);
                        }
                    }
                    // TODO:  Report errors for mal-formed \u escapes?
                    //        As this is, the original sequence is output, which may be OK.
                    if (context.lastOffset == offset) {
                        (void)UTEXT_PREVIOUS32(replacement);
                    } else if (context.lastOffset != offset-1) {
                        utext_moveIndex32(replacement, offset - context.lastOffset - 1);
                    }
                }
            } else {
                (void)UTEXT_NEXT32(replacement);
                // Plain backslash escape.  Just put out the escaped character.
                if (U_IS_BMP(c)) {
                    char16_t c16 = static_cast<char16_t>(c);
                    destLen += utext_replace(dest, destLen, destLen, &c16, 1, &status);
                } else {
                    char16_t surrogate[2];
                    surrogate[0] = U16_LEAD(c);
                    surrogate[1] = U16_TRAIL(c);
                    if (U_SUCCESS(status)) {
                        destLen += utext_replace(dest, destLen, destLen, surrogate, 2, &status);
                    }
                }
            }
        } else if (c != DOLLARSIGN) {
            // Normal char, not a $.  Copy it out without further checks.
            if (U_IS_BMP(c)) {
                char16_t c16 = static_cast<char16_t>(c);
                destLen += utext_replace(dest, destLen, destLen, &c16, 1, &status);
            } else {
                char16_t surrogate[2];
                surrogate[0] = U16_LEAD(c);
                surrogate[1] = U16_TRAIL(c);
                if (U_SUCCESS(status)) {
                    destLen += utext_replace(dest, destLen, destLen, surrogate, 2, &status);
                }
            }
        } else {
            // We've got a $.  Pick up a capture group name or number if one follows.
            // Consume digits so long as the resulting group number <= the number of
            // number of capture groups in the pattern.

            int32_t groupNum  = 0;
            int32_t numDigits = 0;
            UChar32 nextChar = utext_current32(replacement);
            if (nextChar == LEFTBRACKET) {
                // Scan for a Named Capture Group, ${name}.
                UnicodeString groupName;
                utext_next32(replacement);
                while(U_SUCCESS(status) && nextChar != RIGHTBRACKET) {
                    nextChar = utext_next32(replacement);
                    if (nextChar == U_SENTINEL) {
                        status = U_REGEX_INVALID_CAPTURE_GROUP_NAME;
                    } else if ((nextChar >= 0x41 && nextChar <= 0x5a) ||       // A..Z
                               (nextChar >= 0x61 && nextChar <= 0x7a) ||       // a..z
                               (nextChar >= 0x31 && nextChar <= 0x39)) {       // 0..9
                        groupName.append(nextChar);
                    } else if (nextChar == RIGHTBRACKET) {
                        groupNum = fPattern->fNamedCaptureMap ? uhash_geti(fPattern->fNamedCaptureMap, &groupName) : 0;
                        if (groupNum == 0) {
                            status = U_REGEX_INVALID_CAPTURE_GROUP_NAME;
                        }
                    } else {
                        // Character was something other than a name char or a closing '}'
                        status = U_REGEX_INVALID_CAPTURE_GROUP_NAME;
                    }
                }

            } else if (u_isdigit(nextChar)) {
                // $n    Scan for a capture group number
                int32_t numCaptureGroups = fPattern->fGroupMap->size();
                for (;;) {
                    nextChar = UTEXT_CURRENT32(replacement);
                    if (nextChar == U_SENTINEL) {
                        break;
                    }
                    if (u_isdigit(nextChar) == false) {
                        break;
                    }
                    int32_t nextDigitVal = u_charDigitValue(nextChar);
                    if (groupNum*10 + nextDigitVal > numCaptureGroups) {
                        // Don't consume the next digit if it makes the capture group number too big.
                        if (numDigits == 0) {
                            status = U_INDEX_OUTOFBOUNDS_ERROR;
                        }
                        break;
                    }
                    (void)UTEXT_NEXT32(replacement);
                    groupNum=groupNum*10 + nextDigitVal;
                    ++numDigits;
                }
            } else {
                // $ not followed by capture group name or number.
                status = U_REGEX_INVALID_CAPTURE_GROUP_NAME;
            }

            if (U_SUCCESS(status)) {
                destLen += appendGroup(groupNum, dest, status);
            }
        }  // End of $ capture group handling
    }  // End of per-character loop through the replacement string.

    return *this;
}



//--------------------------------------------------------------------------------
//
//    appendTail     Intended to be used in conjunction with appendReplacement()
//                   To the destination string, append everything following
//                   the last match position from the input string.
//
//                   Note:  Match ranges do not affect appendTail or appendReplacement
//
//--------------------------------------------------------------------------------
UnicodeString &RegexMatcher::appendTail(UnicodeString &dest) {
    UErrorCode status = U_ZERO_ERROR;
    UText resultText = UTEXT_INITIALIZER;
    utext_openUnicodeString(&resultText, &dest, &status);

    if (U_SUCCESS(status)) {
        appendTail(&resultText, status);
        utext_close(&resultText);
    }

    return dest;
}

//
//   appendTail, UText mode
//
UText *RegexMatcher::appendTail(UText *dest, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return dest;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return dest;
    }

    if (fInputLength > fAppendPosition) {
        if (UTEXT_FULL_TEXT_IN_CHUNK(fInputText, fInputLength)) {
            int64_t destLen = utext_nativeLength(dest);
            utext_replace(dest, destLen, destLen, fInputText->chunkContents+fAppendPosition,
                          static_cast<int32_t>(fInputLength - fAppendPosition), &status);
        } else {
            int32_t len16;
            if (UTEXT_USES_U16(fInputText)) {
                len16 = static_cast<int32_t>(fInputLength - fAppendPosition);
            } else {
                len16 = utext_extract(fInputText, fAppendPosition, fInputLength, nullptr, 0, &status);
                status = U_ZERO_ERROR; // buffer overflow
            }

            char16_t* inputChars = static_cast<char16_t*>(uprv_malloc(sizeof(char16_t) * (len16)));
            if (inputChars == nullptr) {
                fDeferredStatus = U_MEMORY_ALLOCATION_ERROR;
            } else {
                utext_extract(fInputText, fAppendPosition, fInputLength, inputChars, len16, &status); // unterminated
                int64_t destLen = utext_nativeLength(dest);
                utext_replace(dest, destLen, destLen, inputChars, len16, &status);
                uprv_free(inputChars);
            }
        }
    }
    return dest;
}



//--------------------------------------------------------------------------------
//
//   end
//
//--------------------------------------------------------------------------------
int32_t RegexMatcher::end(UErrorCode &err) const {
    return end(0, err);
}

int64_t RegexMatcher::end64(UErrorCode &err) const {
    return end64(0, err);
}

int64_t RegexMatcher::end64(int32_t group, UErrorCode &err) const {
    if (U_FAILURE(err)) {
        return -1;
    }
    if (fMatch == false) {
        err = U_REGEX_INVALID_STATE;
        return -1;
    }
    if (group < 0 || group > fPattern->fGroupMap->size()) {
        err = U_INDEX_OUTOFBOUNDS_ERROR;
        return -1;
    }
    int64_t e = -1;
    if (group == 0) {
        e = fMatchEnd;
    } else {
        // Get the position within the stack frame of the variables for
        //    this capture group.
        int32_t groupOffset = fPattern->fGroupMap->elementAti(group-1);
        U_ASSERT(groupOffset < fPattern->fFrameSize);
        U_ASSERT(groupOffset >= 0);
        e = fFrame->fExtra[groupOffset + 1];
    }

        return e;
}

int32_t RegexMatcher::end(int32_t group, UErrorCode &err) const {
    return static_cast<int32_t>(end64(group, err));
}

//--------------------------------------------------------------------------------
//
//   findProgressInterrupt  This function is called once for each advance in the target
//                          string from the find() function, and calls the user progress callback
//                          function if there is one installed.
//
//         Return:  true if the find operation is to be terminated.
//                  false if the find operation is to continue running.
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::findProgressInterrupt(int64_t pos, UErrorCode &status) {
    if (fFindProgressCallbackFn && !(*fFindProgressCallbackFn)(fFindProgressCallbackContext, pos)) {
        status = U_REGEX_STOPPED_BY_CALLER;
        return true;
    }
    return false;
}

//--------------------------------------------------------------------------------
//
//   find()
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::find() {
    if (U_FAILURE(fDeferredStatus)) {
        return false;
    }
    UErrorCode status = U_ZERO_ERROR;
    UBool result = find(status);
    return result;
}

//--------------------------------------------------------------------------------
//
//   find()
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::find(UErrorCode &status) {
    // Start at the position of the last match end.  (Will be zero if the
    //   matcher has been reset.)
    //
    if (U_FAILURE(status)) {
        return false;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return false;
    }

    if (UTEXT_FULL_TEXT_IN_CHUNK(fInputText, fInputLength)) {
        return findUsingChunk(status);
    }

    int64_t startPos = fMatchEnd;
    if (startPos==0) {
        startPos = fActiveStart;
    }

    if (fMatch) {
        // Save the position of any previous successful match.
        fLastMatchEnd = fMatchEnd;

        if (fMatchStart == fMatchEnd) {
            // Previous match had zero length.  Move start position up one position
            //  to avoid sending find() into a loop on zero-length matches.
            if (startPos >= fActiveLimit) {
                fMatch = false;
                fHitEnd = true;
                return false;
            }
            UTEXT_SETNATIVEINDEX(fInputText, startPos);
            (void)UTEXT_NEXT32(fInputText);
            startPos = UTEXT_GETNATIVEINDEX(fInputText);
        }
    } else {
        if (fLastMatchEnd >= 0) {
            // A previous find() failed to match.  Don't try again.
            //   (without this test, a pattern with a zero-length match
            //    could match again at the end of an input string.)
            fHitEnd = true;
            return false;
        }
    }


    // Compute the position in the input string beyond which a match can not begin, because
    //   the minimum length match would extend past the end of the input.
    //   Note:  some patterns that cannot match anything will have fMinMatchLength==Max Int.
    //          Be aware of possible overflows if making changes here.
    int64_t testStartLimit;
    if (UTEXT_USES_U16(fInputText)) {
        testStartLimit = fActiveLimit - fPattern->fMinMatchLen;
        if (startPos > testStartLimit) {
            fMatch = false;
            fHitEnd = true;
            return false;
        }
    } else {
        // We don't know exactly how long the minimum match length is in native characters.
        // Treat anything > 0 as 1.
        testStartLimit = fActiveLimit - (fPattern->fMinMatchLen > 0 ? 1 : 0);
    }

    UChar32  c;
    U_ASSERT(startPos >= 0);

    switch (fPattern->fStartType) {
    case START_NO_INFO:
        // No optimization was found.
        //  Try a match at each input position.
        for (;;) {
            MatchAt(startPos, false, status);
            if (U_FAILURE(status)) {
                return false;
            }
            if (fMatch) {
                return true;
            }
            if (startPos >= testStartLimit) {
                fHitEnd = true;
                return false;
            }
            UTEXT_SETNATIVEINDEX(fInputText, startPos);
            (void)UTEXT_NEXT32(fInputText);
            startPos = UTEXT_GETNATIVEINDEX(fInputText);
            // Note that it's perfectly OK for a pattern to have a zero-length
            //   match at the end of a string, so we must make sure that the loop
            //   runs with startPos == testStartLimit the last time through.
            if  (findProgressInterrupt(startPos, status))
                return false;
        }
        UPRV_UNREACHABLE_EXIT;

    case START_START:
        // Matches are only possible at the start of the input string
        //   (pattern begins with ^ or \A)
        if (startPos > fActiveStart) {
            fMatch = false;
            return false;
        }
        MatchAt(startPos, false, status);
        if (U_FAILURE(status)) {
            return false;
        }
        return fMatch;


    case START_SET:
        {
            // Match may start on any char from a pre-computed set.
            U_ASSERT(fPattern->fMinMatchLen > 0);
            UTEXT_SETNATIVEINDEX(fInputText, startPos);
            for (;;) {
                int64_t pos = startPos;
                c = UTEXT_NEXT32(fInputText);
                startPos = UTEXT_GETNATIVEINDEX(fInputText);
                // c will be -1 (U_SENTINEL) at end of text, in which case we
                // skip this next block (so we don't have a negative array index)
                // and handle end of text in the following block.
                if (c >= 0 && ((c<256 && fPattern->fInitialChars8->contains(c)) ||
                              (c>=256 && fPattern->fInitialChars->contains(c)))) {
                    MatchAt(pos, false, status);
                    if (U_FAILURE(status)) {
                        return false;
                    }
                    if (fMatch) {
                        return true;
                    }
                    UTEXT_SETNATIVEINDEX(fInputText, pos);
                }
                if (startPos > testStartLimit) {
                    fMatch = false;
                    fHitEnd = true;
                    return false;
                }
                if  (findProgressInterrupt(startPos, status))
                    return false;
            }
        }
        UPRV_UNREACHABLE_EXIT;

    case START_STRING:
    case START_CHAR:
        {
            // Match starts on exactly one char.
            U_ASSERT(fPattern->fMinMatchLen > 0);
            UChar32 theChar = fPattern->fInitialChar;
            UTEXT_SETNATIVEINDEX(fInputText, startPos);
            for (;;) {
                int64_t pos = startPos;
                c = UTEXT_NEXT32(fInputText);
                startPos = UTEXT_GETNATIVEINDEX(fInputText);
                if (c == theChar) {
                    MatchAt(pos, false, status);
                    if (U_FAILURE(status)) {
                        return false;
                    }
                    if (fMatch) {
                        return true;
                    }
                    UTEXT_SETNATIVEINDEX(fInputText, startPos);
                }
                if (startPos > testStartLimit) {
                    fMatch = false;
                    fHitEnd = true;
                    return false;
                }
                if  (findProgressInterrupt(startPos, status))
                    return false;
           }
        }
        UPRV_UNREACHABLE_EXIT;

    case START_LINE:
        {
            UChar32 ch;
            if (startPos == fAnchorStart) {
                MatchAt(startPos, false, status);
                if (U_FAILURE(status)) {
                    return false;
                }
                if (fMatch) {
                    return true;
                }
                UTEXT_SETNATIVEINDEX(fInputText, startPos);
                ch = UTEXT_NEXT32(fInputText);
                startPos = UTEXT_GETNATIVEINDEX(fInputText);
            } else {
                UTEXT_SETNATIVEINDEX(fInputText, startPos);
                ch = UTEXT_PREVIOUS32(fInputText);
                UTEXT_SETNATIVEINDEX(fInputText, startPos);
            }

            if (fPattern->fFlags & UREGEX_UNIX_LINES) {
                for (;;) {
                    if (ch == 0x0a) {
                            MatchAt(startPos, false, status);
                            if (U_FAILURE(status)) {
                                return false;
                            }
                            if (fMatch) {
                                return true;
                            }
                            UTEXT_SETNATIVEINDEX(fInputText, startPos);
                    }
                    if (startPos >= testStartLimit) {
                        fMatch = false;
                        fHitEnd = true;
                        return false;
                    }
                    ch = UTEXT_NEXT32(fInputText);
                    startPos = UTEXT_GETNATIVEINDEX(fInputText);
                    // Note that it's perfectly OK for a pattern to have a zero-length
                    //   match at the end of a string, so we must make sure that the loop
                    //   runs with startPos == testStartLimit the last time through.
                    if  (findProgressInterrupt(startPos, status))
                        return false;
                }
            } else {
                for (;;) {
                    if (isLineTerminator(ch)) {
                        if (ch == 0x0d && startPos < fActiveLimit && UTEXT_CURRENT32(fInputText) == 0x0a) {
                            (void)UTEXT_NEXT32(fInputText);
                            startPos = UTEXT_GETNATIVEINDEX(fInputText);
                        }
                        MatchAt(startPos, false, status);
                        if (U_FAILURE(status)) {
                            return false;
                        }
                        if (fMatch) {
                            return true;
                        }
                        UTEXT_SETNATIVEINDEX(fInputText, startPos);
                    }
                    if (startPos >= testStartLimit) {
                        fMatch = false;
                        fHitEnd = true;
                        return false;
                    }
                    ch = UTEXT_NEXT32(fInputText);
                    startPos = UTEXT_GETNATIVEINDEX(fInputText);
                    // Note that it's perfectly OK for a pattern to have a zero-length
                    //   match at the end of a string, so we must make sure that the loop
                    //   runs with startPos == testStartLimit the last time through.
                    if  (findProgressInterrupt(startPos, status))
                        return false;
                }
            }
        }

    default:
        UPRV_UNREACHABLE_ASSERT;
        // Unknown value in fPattern->fStartType, should be from StartOfMatch enum. But
        // we have reports of this in production code, don't use UPRV_UNREACHABLE_EXIT.
        // See ICU-21669.
        status = U_INTERNAL_PROGRAM_ERROR;
        return false;
    }

    UPRV_UNREACHABLE_EXIT;
}



UBool RegexMatcher::find(int64_t start, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return false;
    }
    this->reset();                        // Note:  Reset() is specified by Java Matcher documentation.
                                          //        This will reset the region to be the full input length.
    if (start < 0) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return false;
    }

    int64_t nativeStart = start;
    if (nativeStart < fActiveStart || nativeStart > fActiveLimit) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return false;
    }
    fMatchEnd = nativeStart;
    return find(status);
}


//--------------------------------------------------------------------------------
//
//   findUsingChunk() -- like find(), but with the advance knowledge that the
//                       entire string is available in the UText's chunk buffer.
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::findUsingChunk(UErrorCode &status) {
    // Start at the position of the last match end.  (Will be zero if the
    //   matcher has been reset.
    //

    int32_t startPos = static_cast<int32_t>(fMatchEnd);
    if (startPos==0) {
        startPos = static_cast<int32_t>(fActiveStart);
    }

    const char16_t *inputBuf = fInputText->chunkContents;

    if (fMatch) {
        // Save the position of any previous successful match.
        fLastMatchEnd = fMatchEnd;

        if (fMatchStart == fMatchEnd) {
            // Previous match had zero length.  Move start position up one position
            //  to avoid sending find() into a loop on zero-length matches.
            if (startPos >= fActiveLimit) {
                fMatch = false;
                fHitEnd = true;
                return false;
            }
            U16_FWD_1(inputBuf, startPos, fInputLength);
        }
    } else {
        if (fLastMatchEnd >= 0) {
            // A previous find() failed to match.  Don't try again.
            //   (without this test, a pattern with a zero-length match
            //    could match again at the end of an input string.)
            fHitEnd = true;
            return false;
        }
    }


    // Compute the position in the input string beyond which a match can not begin, because
    //   the minimum length match would extend past the end of the input.
    //   Note:  some patterns that cannot match anything will have fMinMatchLength==Max Int.
    //          Be aware of possible overflows if making changes here.
    //   Note:  a match can begin at inputBuf + testLen; it is an inclusive limit.
    int32_t testLen = static_cast<int32_t>(fActiveLimit - fPattern->fMinMatchLen);
    if (startPos > testLen) {
        fMatch = false;
        fHitEnd = true;
        return false;
    }

    UChar32  c;
    U_ASSERT(startPos >= 0);

    switch (fPattern->fStartType) {
    case START_NO_INFO:
        // No optimization was found.
        //  Try a match at each input position.
        for (;;) {
            MatchChunkAt(startPos, false, status);
            if (U_FAILURE(status)) {
                return false;
            }
            if (fMatch) {
                return true;
            }
            if (startPos >= testLen) {
                fHitEnd = true;
                return false;
            }
            U16_FWD_1(inputBuf, startPos, fActiveLimit);
            // Note that it's perfectly OK for a pattern to have a zero-length
            //   match at the end of a string, so we must make sure that the loop
            //   runs with startPos == testLen the last time through.
            if  (findProgressInterrupt(startPos, status))
                return false;
        }
        UPRV_UNREACHABLE_EXIT;

    case START_START:
        // Matches are only possible at the start of the input string
        //   (pattern begins with ^ or \A)
        if (startPos > fActiveStart) {
            fMatch = false;
            return false;
        }
        MatchChunkAt(startPos, false, status);
        if (U_FAILURE(status)) {
            return false;
        }
        return fMatch;


    case START_SET:
    {
        // Match may start on any char from a pre-computed set.
        U_ASSERT(fPattern->fMinMatchLen > 0);
        for (;;) {
            int32_t pos = startPos;
            U16_NEXT(inputBuf, startPos, fActiveLimit, c);  // like c = inputBuf[startPos++];
            if ((c<256 && fPattern->fInitialChars8->contains(c)) ||
                (c>=256 && fPattern->fInitialChars->contains(c))) {
                MatchChunkAt(pos, false, status);
                if (U_FAILURE(status)) {
                    return false;
                }
                if (fMatch) {
                    return true;
                }
            }
            if (startPos > testLen) {
                fMatch = false;
                fHitEnd = true;
                return false;
            }
            if  (findProgressInterrupt(startPos, status))
                return false;
        }
    }
    UPRV_UNREACHABLE_EXIT;

    case START_STRING:
    case START_CHAR:
    {
        // Match starts on exactly one char.
        U_ASSERT(fPattern->fMinMatchLen > 0);
        UChar32 theChar = fPattern->fInitialChar;
        for (;;) {
            int32_t pos = startPos;
            U16_NEXT(inputBuf, startPos, fActiveLimit, c);  // like c = inputBuf[startPos++];
            if (c == theChar) {
                MatchChunkAt(pos, false, status);
                if (U_FAILURE(status)) {
                    return false;
                }
                if (fMatch) {
                    return true;
                }
            }
            if (startPos > testLen) {
                fMatch = false;
                fHitEnd = true;
                return false;
            }
            if  (findProgressInterrupt(startPos, status))
                return false;
        }
    }
    UPRV_UNREACHABLE_EXIT;

    case START_LINE:
    {
        UChar32 ch;
        if (startPos == fAnchorStart) {
            MatchChunkAt(startPos, false, status);
            if (U_FAILURE(status)) {
                return false;
            }
            if (fMatch) {
                return true;
            }
            U16_FWD_1(inputBuf, startPos, fActiveLimit);
        }

        if (fPattern->fFlags & UREGEX_UNIX_LINES) {
            for (;;) {
                ch = inputBuf[startPos-1];
                if (ch == 0x0a) {
                    MatchChunkAt(startPos, false, status);
                    if (U_FAILURE(status)) {
                        return false;
                    }
                    if (fMatch) {
                        return true;
                    }
                }
                if (startPos >= testLen) {
                    fMatch = false;
                    fHitEnd = true;
                    return false;
                }
                U16_FWD_1(inputBuf, startPos, fActiveLimit);
                // Note that it's perfectly OK for a pattern to have a zero-length
                //   match at the end of a string, so we must make sure that the loop
                //   runs with startPos == testLen the last time through.
                if  (findProgressInterrupt(startPos, status))
                    return false;
            }
        } else {
            for (;;) {
                ch = inputBuf[startPos-1];
                if (isLineTerminator(ch)) {
                    if (ch == 0x0d && startPos < fActiveLimit && inputBuf[startPos] == 0x0a) {
                        startPos++;
                    }
                    MatchChunkAt(startPos, false, status);
                    if (U_FAILURE(status)) {
                        return false;
                    }
                    if (fMatch) {
                        return true;
                    }
                }
                if (startPos >= testLen) {
                    fMatch = false;
                    fHitEnd = true;
                    return false;
                }
                U16_FWD_1(inputBuf, startPos, fActiveLimit);
                // Note that it's perfectly OK for a pattern to have a zero-length
                //   match at the end of a string, so we must make sure that the loop
                //   runs with startPos == testLen the last time through.
                if  (findProgressInterrupt(startPos, status))
                    return false;
            }
        }
    }

    default:
        UPRV_UNREACHABLE_ASSERT;
        // Unknown value in fPattern->fStartType, should be from StartOfMatch enum. But
        // we have reports of this in production code, don't use UPRV_UNREACHABLE_EXIT.
        // See ICU-21669.
        status = U_INTERNAL_PROGRAM_ERROR;
        return false;
    }

    UPRV_UNREACHABLE_EXIT;
}



//--------------------------------------------------------------------------------
//
//  group()
//
//--------------------------------------------------------------------------------
UnicodeString RegexMatcher::group(UErrorCode &status) const {
    return group(0, status);
}

//  Return immutable shallow clone
UText *RegexMatcher::group(UText *dest, int64_t &group_len, UErrorCode &status) const {
    return group(0, dest, group_len, status);
}

//  Return immutable shallow clone
UText *RegexMatcher::group(int32_t groupNum, UText *dest, int64_t &group_len, UErrorCode &status) const {
    group_len = 0;
    if (U_FAILURE(status)) {
        return dest;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
    } else if (fMatch == false) {
        status = U_REGEX_INVALID_STATE;
    } else if (groupNum < 0 || groupNum > fPattern->fGroupMap->size()) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
    }

    if (U_FAILURE(status)) {
        return dest;
    }

    int64_t s, e;
    if (groupNum == 0) {
        s = fMatchStart;
        e = fMatchEnd;
    } else {
        int32_t groupOffset = fPattern->fGroupMap->elementAti(groupNum-1);
        U_ASSERT(groupOffset < fPattern->fFrameSize);
        U_ASSERT(groupOffset >= 0);
        s = fFrame->fExtra[groupOffset];
        e = fFrame->fExtra[groupOffset+1];
    }

    if (s < 0) {
        // A capture group wasn't part of the match
        return utext_clone(dest, fInputText, false, true, &status);
    }
    U_ASSERT(s <= e);
    group_len = e - s;

    dest = utext_clone(dest, fInputText, false, true, &status);
    if (dest)
        UTEXT_SETNATIVEINDEX(dest, s);
    return dest;
}

UnicodeString RegexMatcher::group(int32_t groupNum, UErrorCode &status) const {
    UnicodeString result;
    int64_t groupStart = start64(groupNum, status);
    int64_t groupEnd = end64(groupNum, status);
    if (U_FAILURE(status) || groupStart == -1 || groupStart == groupEnd) {
        return result;
    }

    // Get the group length using a utext_extract preflight.
    //    UText is actually pretty efficient at this when underlying encoding is UTF-16.
    int32_t length = utext_extract(fInputText, groupStart, groupEnd, nullptr, 0, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR) {
        return result;
    }

    status = U_ZERO_ERROR;
    char16_t *buf = result.getBuffer(length);
    if (buf == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    } else {
        int32_t extractLength = utext_extract(fInputText, groupStart, groupEnd, buf, length, &status);
        result.releaseBuffer(extractLength);
        U_ASSERT(length == extractLength);
    }
    return result;
}


//--------------------------------------------------------------------------------
//
//  appendGroup() -- currently internal only, appends a group to a UText rather
//                   than replacing its contents
//
//--------------------------------------------------------------------------------

int64_t RegexMatcher::appendGroup(int32_t groupNum, UText *dest, UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return 0;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return 0;
    }
    int64_t destLen = utext_nativeLength(dest);

    if (fMatch == false) {
        status = U_REGEX_INVALID_STATE;
        return utext_replace(dest, destLen, destLen, nullptr, 0, &status);
    }
    if (groupNum < 0 || groupNum > fPattern->fGroupMap->size()) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return utext_replace(dest, destLen, destLen, nullptr, 0, &status);
    }

    int64_t s, e;
    if (groupNum == 0) {
        s = fMatchStart;
        e = fMatchEnd;
    } else {
        int32_t groupOffset = fPattern->fGroupMap->elementAti(groupNum-1);
        U_ASSERT(groupOffset < fPattern->fFrameSize);
        U_ASSERT(groupOffset >= 0);
        s = fFrame->fExtra[groupOffset];
        e = fFrame->fExtra[groupOffset+1];
    }

    if (s < 0) {
        // A capture group wasn't part of the match
        return utext_replace(dest, destLen, destLen, nullptr, 0, &status);
    }
    U_ASSERT(s <= e);

    int64_t deltaLen;
    if (UTEXT_FULL_TEXT_IN_CHUNK(fInputText, fInputLength)) {
        U_ASSERT(e <= fInputLength);
        deltaLen = utext_replace(dest, destLen, destLen, fInputText->chunkContents + s, static_cast<int32_t>(e - s), &status);
    } else {
        int32_t len16;
        if (UTEXT_USES_U16(fInputText)) {
            len16 = static_cast<int32_t>(e - s);
        } else {
            UErrorCode lengthStatus = U_ZERO_ERROR;
            len16 = utext_extract(fInputText, s, e, nullptr, 0, &lengthStatus);
        }
        char16_t* groupChars = static_cast<char16_t*>(uprv_malloc(sizeof(char16_t) * (len16 + 1)));
        if (groupChars == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return 0;
        }
        utext_extract(fInputText, s, e, groupChars, len16+1, &status);

        deltaLen = utext_replace(dest, destLen, destLen, groupChars, len16, &status);
        uprv_free(groupChars);
    }
    return deltaLen;
}



//--------------------------------------------------------------------------------
//
//  groupCount()
//
//--------------------------------------------------------------------------------
int32_t RegexMatcher::groupCount() const {
    return fPattern->fGroupMap->size();
}

//--------------------------------------------------------------------------------
//
//  hasAnchoringBounds()
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::hasAnchoringBounds() const {
    return fAnchoringBounds;
}


//--------------------------------------------------------------------------------
//
//  hasTransparentBounds()
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::hasTransparentBounds() const {
    return fTransparentBounds;
}



//--------------------------------------------------------------------------------
//
//  hitEnd()
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::hitEnd() const {
    return fHitEnd;
}


//--------------------------------------------------------------------------------
//
//  input()
//
//--------------------------------------------------------------------------------
const UnicodeString &RegexMatcher::input() const {
    if (!fInput) {
        UErrorCode status = U_ZERO_ERROR;
        int32_t len16;
        if (UTEXT_USES_U16(fInputText)) {
            len16 = static_cast<int32_t>(fInputLength);
        } else {
            len16 = utext_extract(fInputText, 0, fInputLength, nullptr, 0, &status);
            status = U_ZERO_ERROR; // overflow, length status
        }
        UnicodeString *result = new UnicodeString(len16, 0, 0);

        char16_t *inputChars = result->getBuffer(len16);
        utext_extract(fInputText, 0, fInputLength, inputChars, len16, &status); // unterminated warning
        result->releaseBuffer(len16);

        *const_cast<const UnicodeString**>(&fInput) = result; // pointer assignment, rather than operator=
    }

    return *fInput;
}

//--------------------------------------------------------------------------------
//
//  inputText()
//
//--------------------------------------------------------------------------------
UText *RegexMatcher::inputText() const {
    return fInputText;
}


//--------------------------------------------------------------------------------
//
//  getInput() -- like inputText(), but makes a clone or copies into another UText
//
//--------------------------------------------------------------------------------
UText *RegexMatcher::getInput (UText *dest, UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return dest;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return dest;
    }

    if (dest) {
        if (UTEXT_FULL_TEXT_IN_CHUNK(fInputText, fInputLength)) {
            utext_replace(dest, 0, utext_nativeLength(dest), fInputText->chunkContents, static_cast<int32_t>(fInputLength), &status);
        } else {
            int32_t input16Len;
            if (UTEXT_USES_U16(fInputText)) {
                input16Len = static_cast<int32_t>(fInputLength);
            } else {
                UErrorCode lengthStatus = U_ZERO_ERROR;
                input16Len = utext_extract(fInputText, 0, fInputLength, nullptr, 0, &lengthStatus); // buffer overflow error
            }
            char16_t* inputChars = static_cast<char16_t*>(uprv_malloc(sizeof(char16_t) * (input16Len)));
            if (inputChars == nullptr) {
                return dest;
            }

            status = U_ZERO_ERROR;
            utext_extract(fInputText, 0, fInputLength, inputChars, input16Len, &status); // not terminated warning
            status = U_ZERO_ERROR;
            utext_replace(dest, 0, utext_nativeLength(dest), inputChars, input16Len, &status);

            uprv_free(inputChars);
        }
        return dest;
    } else {
        return utext_clone(nullptr, fInputText, false, true, &status);
    }
}


static UBool compat_SyncMutableUTextContents(UText *ut);
static UBool compat_SyncMutableUTextContents(UText *ut) {
    UBool retVal = false;

    //  In the following test, we're really only interested in whether the UText should switch
    //  between heap and stack allocation.  If length hasn't changed, we won't, so the chunkContents
    //  will still point to the correct data.
    if (utext_nativeLength(ut) != ut->nativeIndexingLimit) {
        UnicodeString *us=(UnicodeString *)ut->context;

        // Update to the latest length.
        // For example, (utext_nativeLength(ut) != ut->nativeIndexingLimit).
        int32_t newLength = us->length();

        // Update the chunk description.
        // The buffer may have switched between stack- and heap-based.
        ut->chunkContents    = us->getBuffer();
        ut->chunkLength      = newLength;
        ut->chunkNativeLimit = newLength;
        ut->nativeIndexingLimit = newLength;
        retVal = true;
    }

    return retVal;
}

//--------------------------------------------------------------------------------
//
//  lookingAt()
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::lookingAt(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return false;
    }

    if (fInputUniStrMaybeMutable) {
        if (compat_SyncMutableUTextContents(fInputText)) {
        fInputLength = utext_nativeLength(fInputText);
        reset();
        }
    }
    else {
        resetPreserveRegion();
    }
    if (UTEXT_FULL_TEXT_IN_CHUNK(fInputText, fInputLength)) {
        MatchChunkAt(static_cast<int32_t>(fActiveStart), false, status);
    } else {
        MatchAt(fActiveStart, false, status);
    }
    return fMatch;
}


UBool RegexMatcher::lookingAt(int64_t start, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return false;
    }
    reset();

    if (start < 0) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return false;
    }

    if (fInputUniStrMaybeMutable) {
        if (compat_SyncMutableUTextContents(fInputText)) {
        fInputLength = utext_nativeLength(fInputText);
        reset();
        }
    }

    int64_t nativeStart;
    nativeStart = start;
    if (nativeStart < fActiveStart || nativeStart > fActiveLimit) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return false;
    }

    if (UTEXT_FULL_TEXT_IN_CHUNK(fInputText, fInputLength)) {
        MatchChunkAt(static_cast<int32_t>(nativeStart), false, status);
    } else {
        MatchAt(nativeStart, false, status);
    }
    return fMatch;
}



//--------------------------------------------------------------------------------
//
//  matches()
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::matches(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return false;
    }

    if (fInputUniStrMaybeMutable) {
        if (compat_SyncMutableUTextContents(fInputText)) {
        fInputLength = utext_nativeLength(fInputText);
        reset();
        }
    }
    else {
        resetPreserveRegion();
    }

    if (UTEXT_FULL_TEXT_IN_CHUNK(fInputText, fInputLength)) {
        MatchChunkAt(static_cast<int32_t>(fActiveStart), true, status);
    } else {
        MatchAt(fActiveStart, true, status);
    }
    return fMatch;
}


UBool RegexMatcher::matches(int64_t start, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return false;
    }
    reset();

    if (start < 0) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return false;
    }

    if (fInputUniStrMaybeMutable) {
        if (compat_SyncMutableUTextContents(fInputText)) {
        fInputLength = utext_nativeLength(fInputText);
        reset();
        }
    }

    int64_t nativeStart;
    nativeStart = start;
    if (nativeStart < fActiveStart || nativeStart > fActiveLimit) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return false;
    }

    if (UTEXT_FULL_TEXT_IN_CHUNK(fInputText, fInputLength)) {
        MatchChunkAt(static_cast<int32_t>(nativeStart), true, status);
    } else {
        MatchAt(nativeStart, true, status);
    }
    return fMatch;
}



//--------------------------------------------------------------------------------
//
//    pattern
//
//--------------------------------------------------------------------------------
const RegexPattern &RegexMatcher::pattern() const {
    return *fPattern;
}



//--------------------------------------------------------------------------------
//
//    region
//
//--------------------------------------------------------------------------------
RegexMatcher &RegexMatcher::region(int64_t regionStart, int64_t regionLimit, int64_t startIndex, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }

    if (regionStart>regionLimit || regionStart<0 || regionLimit<0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }

    int64_t nativeStart = regionStart;
    int64_t nativeLimit = regionLimit;
    if (nativeStart > fInputLength || nativeLimit > fInputLength) {
      status = U_ILLEGAL_ARGUMENT_ERROR;
    }

    if (startIndex == -1)
      this->reset();
    else
      resetPreserveRegion();

    fRegionStart = nativeStart;
    fRegionLimit = nativeLimit;
    fActiveStart = nativeStart;
    fActiveLimit = nativeLimit;

    if (startIndex != -1) {
      if (startIndex < fActiveStart || startIndex > fActiveLimit) {
          status = U_INDEX_OUTOFBOUNDS_ERROR;
      }
      fMatchEnd = startIndex;
    }

    if (!fTransparentBounds) {
        fLookStart = nativeStart;
        fLookLimit = nativeLimit;
    }
    if (fAnchoringBounds) {
        fAnchorStart = nativeStart;
        fAnchorLimit = nativeLimit;
    }
    return *this;
}

RegexMatcher &RegexMatcher::region(int64_t start, int64_t limit, UErrorCode &status) {
  return region(start, limit, -1, status);
}

//--------------------------------------------------------------------------------
//
//    regionEnd
//
//--------------------------------------------------------------------------------
int32_t RegexMatcher::regionEnd() const {
    return static_cast<int32_t>(fRegionLimit);
}

int64_t RegexMatcher::regionEnd64() const {
    return fRegionLimit;
}

//--------------------------------------------------------------------------------
//
//    regionStart
//
//--------------------------------------------------------------------------------
int32_t RegexMatcher::regionStart() const {
    return static_cast<int32_t>(fRegionStart);
}

int64_t RegexMatcher::regionStart64() const {
    return fRegionStart;
}


//--------------------------------------------------------------------------------
//
//    replaceAll
//
//--------------------------------------------------------------------------------
UnicodeString RegexMatcher::replaceAll(const UnicodeString &replacement, UErrorCode &status) {
    UText replacementText = UTEXT_INITIALIZER;
    UText resultText = UTEXT_INITIALIZER;
    UnicodeString resultString;
    if (U_FAILURE(status)) {
        return resultString;
    }

    utext_openConstUnicodeString(&replacementText, &replacement, &status);
    utext_openUnicodeString(&resultText, &resultString, &status);

    replaceAll(&replacementText, &resultText, status);

    utext_close(&resultText);
    utext_close(&replacementText);

    return resultString;
}


//
//    replaceAll, UText mode
//
UText *RegexMatcher::replaceAll(UText *replacement, UText *dest, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return dest;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return dest;
    }

    if (dest == nullptr) {
        UnicodeString emptyString;
        UText empty = UTEXT_INITIALIZER;

        utext_openUnicodeString(&empty, &emptyString, &status);
        dest = utext_clone(nullptr, &empty, true, false, &status);
        utext_close(&empty);
    }

    if (U_SUCCESS(status)) {
        reset();
        while (find()) {
            appendReplacement(dest, replacement, status);
            if (U_FAILURE(status)) {
                break;
            }
        }
        appendTail(dest, status);
    }

    return dest;
}


//--------------------------------------------------------------------------------
//
//    replaceFirst
//
//--------------------------------------------------------------------------------
UnicodeString RegexMatcher::replaceFirst(const UnicodeString &replacement, UErrorCode &status) {
    UText replacementText = UTEXT_INITIALIZER;
    UText resultText = UTEXT_INITIALIZER;
    UnicodeString resultString;

    utext_openConstUnicodeString(&replacementText, &replacement, &status);
    utext_openUnicodeString(&resultText, &resultString, &status);

    replaceFirst(&replacementText, &resultText, status);

    utext_close(&resultText);
    utext_close(&replacementText);

    return resultString;
}

//
//    replaceFirst, UText mode
//
UText *RegexMatcher::replaceFirst(UText *replacement, UText *dest, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return dest;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return dest;
    }

    reset();
    if (!find()) {
        return getInput(dest, status);
    }

    if (dest == nullptr) {
        UnicodeString emptyString;
        UText empty = UTEXT_INITIALIZER;

        utext_openUnicodeString(&empty, &emptyString, &status);
        dest = utext_clone(nullptr, &empty, true, false, &status);
        utext_close(&empty);
    }

    appendReplacement(dest, replacement, status);
    appendTail(dest, status);

    return dest;
}


//--------------------------------------------------------------------------------
//
//     requireEnd
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::requireEnd() const {
    return fRequireEnd;
}


//--------------------------------------------------------------------------------
//
//     reset
//
//--------------------------------------------------------------------------------
RegexMatcher &RegexMatcher::reset() {
    fRegionStart    = 0;
    fRegionLimit    = fInputLength;
    fActiveStart    = 0;
    fActiveLimit    = fInputLength;
    fAnchorStart    = 0;
    fAnchorLimit    = fInputLength;
    fLookStart      = 0;
    fLookLimit      = fInputLength;
    resetPreserveRegion();
    return *this;
}



void RegexMatcher::resetPreserveRegion() {
    fMatchStart     = 0;
    fMatchEnd       = 0;
    fLastMatchEnd   = -1;
    fAppendPosition = 0;
    fMatch          = false;
    fHitEnd         = false;
    fRequireEnd     = false;
    fTime           = 0;
    fTickCounter    = TIMER_INITIAL_VALUE;
    //resetStack(); // more expensive than it looks...
}


RegexMatcher &RegexMatcher::reset(const UnicodeString &input) {
    fInputText = utext_openConstUnicodeString(fInputText, &input, &fDeferredStatus);
    if (fPattern->fNeedsAltInput) {
        fAltInputText = utext_clone(fAltInputText, fInputText, false, true, &fDeferredStatus);
    }
    if (U_FAILURE(fDeferredStatus)) {
        return *this;
    }
    fInputLength = utext_nativeLength(fInputText);

    reset();
    delete fInput;
    fInput = nullptr;

    //  Do the following for any UnicodeString.
    //  This is for compatibility for those clients who modify the input string "live" during regex operations.
    fInputUniStrMaybeMutable = true;

#if UCONFIG_NO_BREAK_ITERATION==0
    if (fWordBreakItr) {
        fWordBreakItr->setText(fInputText, fDeferredStatus);
    }
    if (fGCBreakItr) {
        fGCBreakItr->setText(fInputText, fDeferredStatus);
    }
#endif

    return *this;
}


RegexMatcher &RegexMatcher::reset(UText *input) {
    if (fInputText != input) {
        fInputText = utext_clone(fInputText, input, false, true, &fDeferredStatus);
        if (fPattern->fNeedsAltInput) fAltInputText = utext_clone(fAltInputText, fInputText, false, true, &fDeferredStatus);
        if (U_FAILURE(fDeferredStatus)) {
            return *this;
        }
        fInputLength = utext_nativeLength(fInputText);

        delete fInput;
        fInput = nullptr;

#if UCONFIG_NO_BREAK_ITERATION==0
        if (fWordBreakItr) {
            fWordBreakItr->setText(input, fDeferredStatus);
        }
        if (fGCBreakItr) {
            fGCBreakItr->setText(fInputText, fDeferredStatus);
        }
#endif
    }
    reset();
    fInputUniStrMaybeMutable = false;

    return *this;
}

/*RegexMatcher &RegexMatcher::reset(const char16_t *) {
    fDeferredStatus = U_INTERNAL_PROGRAM_ERROR;
    return *this;
}*/

RegexMatcher &RegexMatcher::reset(int64_t position, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    reset();       // Reset also resets the region to be the entire string.

    if (position < 0 || position > fActiveLimit) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return *this;
    }
    fMatchEnd = position;
    return *this;
}


//--------------------------------------------------------------------------------
//
//    refresh
//
//--------------------------------------------------------------------------------
RegexMatcher &RegexMatcher::refreshInputText(UText *input, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    if (input == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return *this;
    }
    if (utext_nativeLength(fInputText) != utext_nativeLength(input)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return *this;
    }
    int64_t  pos = utext_getNativeIndex(fInputText);
    //  Shallow read-only clone of the new UText into the existing input UText
    fInputText = utext_clone(fInputText, input, false, true, &status);
    if (U_FAILURE(status)) {
        return *this;
    }
    utext_setNativeIndex(fInputText, pos);

    if (fAltInputText != nullptr) {
        pos = utext_getNativeIndex(fAltInputText);
        fAltInputText = utext_clone(fAltInputText, input, false, true, &status);
        if (U_FAILURE(status)) {
            return *this;
        }
        utext_setNativeIndex(fAltInputText, pos);
    }
    return *this;
}



//--------------------------------------------------------------------------------
//
//    setTrace
//
//--------------------------------------------------------------------------------
void RegexMatcher::setTrace(UBool state) {
    fTraceDebug = state;
}



/**
  *  UText, replace entire contents of the destination UText with a substring of the source UText.
  *
  *     @param src    The source UText
  *     @param dest   The destination UText. Must be writable.
  *                   May be nullptr, in which case a new UText will be allocated.
  *     @param start  Start index of source substring.
  *     @param limit  Limit index of source substring.
  *     @param status An error code.
  */
static UText *utext_extract_replace(UText *src, UText *dest, int64_t start, int64_t limit, UErrorCode *status) {
    if (U_FAILURE(*status)) {
        return dest;
    }
    if (start == limit) {
        if (dest) {
            utext_replace(dest, 0, utext_nativeLength(dest), nullptr, 0, status);
            return dest;
        } else {
            return utext_openUChars(nullptr, nullptr, 0, status);
        }
    }
    int32_t length = utext_extract(src, start, limit, nullptr, 0, status);
    if (*status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(*status)) {
        return dest;
    }
    *status = U_ZERO_ERROR;
    MaybeStackArray<char16_t, 40> buffer;
    if (length >= buffer.getCapacity()) {
        char16_t *newBuf = buffer.resize(length+1);   // Leave space for terminating Nul.
        if (newBuf == nullptr) {
            *status = U_MEMORY_ALLOCATION_ERROR;
        }
    }
    utext_extract(src, start, limit, buffer.getAlias(), length+1, status);
    if (dest) {
        utext_replace(dest, 0, utext_nativeLength(dest), buffer.getAlias(), length, status);
        return dest;
    }

    // Caller did not provide a preexisting UText.
    // Open a new one, and have it adopt the text buffer storage.
    if (U_FAILURE(*status)) {
        return nullptr;
    }
    int32_t ownedLength = 0;
    char16_t *ownedBuf = buffer.orphanOrClone(length+1, ownedLength);
    if (ownedBuf == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    UText *result = utext_openUChars(nullptr, ownedBuf, length, status);
    if (U_FAILURE(*status)) {
        uprv_free(ownedBuf);
        return nullptr;
    }
    result->providerProperties |= (1 << UTEXT_PROVIDER_OWNS_TEXT);
    return result;
}


//---------------------------------------------------------------------
//
//   split
//
//---------------------------------------------------------------------
int32_t  RegexMatcher::split(const UnicodeString &input,
        UnicodeString    dest[],
        int32_t          destCapacity,
        UErrorCode      &status)
{
    UText inputText = UTEXT_INITIALIZER;
    utext_openConstUnicodeString(&inputText, &input, &status);
    if (U_FAILURE(status)) {
        return 0;
    }

    UText** destText = static_cast<UText**>(uprv_malloc(sizeof(UText*) * destCapacity));
    if (destText == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return 0;
    }
    int32_t i;
    for (i = 0; i < destCapacity; i++) {
        destText[i] = utext_openUnicodeString(nullptr, &dest[i], &status);
    }

    int32_t fieldCount = split(&inputText, destText, destCapacity, status);

    for (i = 0; i < destCapacity; i++) {
        utext_close(destText[i]);
    }

    uprv_free(destText);
    utext_close(&inputText);
    return fieldCount;
}

//
//   split, UText mode
//
int32_t  RegexMatcher::split(UText *input,
        UText           *dest[],
        int32_t          destCapacity,
        UErrorCode      &status)
{
    //
    // Check arguments for validity
    //
    if (U_FAILURE(status)) {
        return 0;
    }

    if (destCapacity < 1) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    //
    // Reset for the input text
    //
    reset(input);
    int64_t   nextOutputStringStart = 0;
    if (fActiveLimit == 0) {
        return 0;
    }

    //
    // Loop through the input text, searching for the delimiter pattern
    //
    int32_t i;
    int32_t numCaptureGroups = fPattern->fGroupMap->size();
    for (i=0; ; i++) {
        if (i>=destCapacity-1) {
            // There is one or zero output string left.
            // Fill the last output string with whatever is left from the input, then exit the loop.
            //  ( i will be == destCapacity if we filled the output array while processing
            //    capture groups of the delimiter expression, in which case we will discard the
            //    last capture group saved in favor of the unprocessed remainder of the
            //    input string.)
            i = destCapacity-1;
            if (fActiveLimit > nextOutputStringStart) {
                if (UTEXT_FULL_TEXT_IN_CHUNK(input, fInputLength)) {
                    if (dest[i]) {
                        utext_replace(dest[i], 0, utext_nativeLength(dest[i]),
                                      input->chunkContents+nextOutputStringStart,
                                      static_cast<int32_t>(fActiveLimit - nextOutputStringStart), &status);
                    } else {
                        UText remainingText = UTEXT_INITIALIZER;
                        utext_openUChars(&remainingText, input->chunkContents+nextOutputStringStart,
                                         fActiveLimit-nextOutputStringStart, &status);
                        dest[i] = utext_clone(nullptr, &remainingText, true, false, &status);
                        utext_close(&remainingText);
                    }
                } else {
                    UErrorCode lengthStatus = U_ZERO_ERROR;
                    int32_t remaining16Length =
                        utext_extract(input, nextOutputStringStart, fActiveLimit, nullptr, 0, &lengthStatus);
                    char16_t* remainingChars = static_cast<char16_t*>(uprv_malloc(sizeof(char16_t) * (remaining16Length + 1)));
                    if (remainingChars == nullptr) {
                        status = U_MEMORY_ALLOCATION_ERROR;
                        break;
                    }

                    utext_extract(input, nextOutputStringStart, fActiveLimit, remainingChars, remaining16Length+1, &status);
                    if (dest[i]) {
                        utext_replace(dest[i], 0, utext_nativeLength(dest[i]), remainingChars, remaining16Length, &status);
                    } else {
                        UText remainingText = UTEXT_INITIALIZER;
                        utext_openUChars(&remainingText, remainingChars, remaining16Length, &status);
                        dest[i] = utext_clone(nullptr, &remainingText, true, false, &status);
                        utext_close(&remainingText);
                    }

                    uprv_free(remainingChars);
                }
            }
            break;
        }
        if (find()) {
            // We found another delimiter.  Move everything from where we started looking
            //  up until the start of the delimiter into the next output string.
            if (UTEXT_FULL_TEXT_IN_CHUNK(input, fInputLength)) {
                if (dest[i]) {
                    utext_replace(dest[i], 0, utext_nativeLength(dest[i]),
                                  input->chunkContents+nextOutputStringStart,
                                  static_cast<int32_t>(fMatchStart - nextOutputStringStart), &status);
                } else {
                    UText remainingText = UTEXT_INITIALIZER;
                    utext_openUChars(&remainingText, input->chunkContents+nextOutputStringStart,
                                      fMatchStart-nextOutputStringStart, &status);
                    dest[i] = utext_clone(nullptr, &remainingText, true, false, &status);
                    utext_close(&remainingText);
                }
            } else {
                UErrorCode lengthStatus = U_ZERO_ERROR;
                int32_t remaining16Length = utext_extract(input, nextOutputStringStart, fMatchStart, nullptr, 0, &lengthStatus);
                char16_t* remainingChars = static_cast<char16_t*>(uprv_malloc(sizeof(char16_t) * (remaining16Length + 1)));
                if (remainingChars == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    break;
                }
                utext_extract(input, nextOutputStringStart, fMatchStart, remainingChars, remaining16Length+1, &status);
                if (dest[i]) {
                    utext_replace(dest[i], 0, utext_nativeLength(dest[i]), remainingChars, remaining16Length, &status);
                } else {
                    UText remainingText = UTEXT_INITIALIZER;
                    utext_openUChars(&remainingText, remainingChars, remaining16Length, &status);
                    dest[i] = utext_clone(nullptr, &remainingText, true, false, &status);
                    utext_close(&remainingText);
                }

                uprv_free(remainingChars);
            }
            nextOutputStringStart = fMatchEnd;

            // If the delimiter pattern has capturing parentheses, the captured
            //  text goes out into the next n destination strings.
            int32_t groupNum;
            for (groupNum=1; groupNum<=numCaptureGroups; groupNum++) {
                if (i >= destCapacity-2) {
                    // Never fill the last available output string with capture group text.
                    // It will filled with the last field, the remainder of the
                    //  unsplit input text.
                    break;
                }
                i++;
                dest[i] = utext_extract_replace(fInputText, dest[i],
                                               start64(groupNum, status), end64(groupNum, status), &status);
            }

            if (nextOutputStringStart == fActiveLimit) {
                // The delimiter was at the end of the string.  We're done, but first
                // we output one last empty string, for the empty field following
                //   the delimiter at the end of input.
                if (i+1 < destCapacity) {
                    ++i;
                    if (dest[i] == nullptr) {
                        dest[i] = utext_openUChars(nullptr, nullptr, 0, &status);
                    } else {
                        static const char16_t emptyString[] = {static_cast<char16_t>(0)};
                        utext_replace(dest[i], 0, utext_nativeLength(dest[i]), emptyString, 0, &status);
                    }
                }
                break;

            }
        }
        else
        {
            // We ran off the end of the input while looking for the next delimiter.
            // All the remaining text goes into the current output string.
            if (UTEXT_FULL_TEXT_IN_CHUNK(input, fInputLength)) {
                if (dest[i]) {
                    utext_replace(dest[i], 0, utext_nativeLength(dest[i]),
                                  input->chunkContents+nextOutputStringStart,
                                  static_cast<int32_t>(fActiveLimit - nextOutputStringStart), &status);
                } else {
                    UText remainingText = UTEXT_INITIALIZER;
                    utext_openUChars(&remainingText, input->chunkContents+nextOutputStringStart,
                                     fActiveLimit-nextOutputStringStart, &status);
                    dest[i] = utext_clone(nullptr, &remainingText, true, false, &status);
                    utext_close(&remainingText);
                }
            } else {
                UErrorCode lengthStatus = U_ZERO_ERROR;
                int32_t remaining16Length = utext_extract(input, nextOutputStringStart, fActiveLimit, nullptr, 0, &lengthStatus);
                char16_t* remainingChars = static_cast<char16_t*>(uprv_malloc(sizeof(char16_t) * (remaining16Length + 1)));
                if (remainingChars == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    break;
                }

                utext_extract(input, nextOutputStringStart, fActiveLimit, remainingChars, remaining16Length+1, &status);
                if (dest[i]) {
                    utext_replace(dest[i], 0, utext_nativeLength(dest[i]), remainingChars, remaining16Length, &status);
                } else {
                    UText remainingText = UTEXT_INITIALIZER;
                    utext_openUChars(&remainingText, remainingChars, remaining16Length, &status);
                    dest[i] = utext_clone(nullptr, &remainingText, true, false, &status);
                    utext_close(&remainingText);
                }

                uprv_free(remainingChars);
            }
            break;
        }
        if (U_FAILURE(status)) {
            break;
        }
    }   // end of for loop
    return i+1;
}


//--------------------------------------------------------------------------------
//
//     start
//
//--------------------------------------------------------------------------------
int32_t RegexMatcher::start(UErrorCode &status) const {
    return start(0, status);
}

int64_t RegexMatcher::start64(UErrorCode &status) const {
    return start64(0, status);
}

//--------------------------------------------------------------------------------
//
//     start(int32_t group, UErrorCode &status)
//
//--------------------------------------------------------------------------------

int64_t RegexMatcher::start64(int32_t group, UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return -1;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return -1;
    }
    if (fMatch == false) {
        status = U_REGEX_INVALID_STATE;
        return -1;
    }
    if (group < 0 || group > fPattern->fGroupMap->size()) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return -1;
    }
    int64_t s;
    if (group == 0) {
        s = fMatchStart;
    } else {
        int32_t groupOffset = fPattern->fGroupMap->elementAti(group-1);
        U_ASSERT(groupOffset < fPattern->fFrameSize);
        U_ASSERT(groupOffset >= 0);
        s = fFrame->fExtra[groupOffset];
    }

    return s;
}


int32_t RegexMatcher::start(int32_t group, UErrorCode &status) const {
    return static_cast<int32_t>(start64(group, status));
}

//--------------------------------------------------------------------------------
//
//     useAnchoringBounds
//
//--------------------------------------------------------------------------------
RegexMatcher &RegexMatcher::useAnchoringBounds(UBool b) {
    fAnchoringBounds = b;
    fAnchorStart = (fAnchoringBounds ? fRegionStart : 0);
    fAnchorLimit = (fAnchoringBounds ? fRegionLimit : fInputLength);
    return *this;
}


//--------------------------------------------------------------------------------
//
//     useTransparentBounds
//
//--------------------------------------------------------------------------------
RegexMatcher &RegexMatcher::useTransparentBounds(UBool b) {
    fTransparentBounds = b;
    fLookStart = (fTransparentBounds ? 0 : fRegionStart);
    fLookLimit = (fTransparentBounds ? fInputLength : fRegionLimit);
    return *this;
}

//--------------------------------------------------------------------------------
//
//     setTimeLimit
//
//--------------------------------------------------------------------------------
void RegexMatcher::setTimeLimit(int32_t limit, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return;
    }
    if (limit < 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    fTimeLimit = limit;
}


//--------------------------------------------------------------------------------
//
//     getTimeLimit
//
//--------------------------------------------------------------------------------
int32_t RegexMatcher::getTimeLimit() const {
    return fTimeLimit;
}


//--------------------------------------------------------------------------------
//
//     setStackLimit
//
//--------------------------------------------------------------------------------
void RegexMatcher::setStackLimit(int32_t limit, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return;
    }
    if (limit < 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    // Reset the matcher.  This is needed here in case there is a current match
    //    whose final stack frame (containing the match results, pointed to by fFrame)
    //    would be lost by resizing to a smaller stack size.
    reset();

    if (limit == 0) {
        // Unlimited stack expansion
        fStack->setMaxCapacity(0);
    } else {
        // Change the units of the limit  from bytes to ints, and bump the size up
        //   to be big enough to hold at least one stack frame for the pattern,
        //   if it isn't there already.
        int32_t adjustedLimit = limit / sizeof(int32_t);
        if (adjustedLimit < fPattern->fFrameSize) {
            adjustedLimit = fPattern->fFrameSize;
        }
        fStack->setMaxCapacity(adjustedLimit);
    }
    fStackLimit = limit;
}


//--------------------------------------------------------------------------------
//
//     getStackLimit
//
//--------------------------------------------------------------------------------
int32_t RegexMatcher::getStackLimit() const {
    return fStackLimit;
}


//--------------------------------------------------------------------------------
//
//     setMatchCallback
//
//--------------------------------------------------------------------------------
void RegexMatcher::setMatchCallback(URegexMatchCallback     *callback,
                                    const void              *context,
                                    UErrorCode              &status) {
    if (U_FAILURE(status)) {
        return;
    }
    fCallbackFn = callback;
    fCallbackContext = context;
}


//--------------------------------------------------------------------------------
//
//     getMatchCallback
//
//--------------------------------------------------------------------------------
void RegexMatcher::getMatchCallback(URegexMatchCallback   *&callback,
                                  const void              *&context,
                                  UErrorCode              &status) {
    if (U_FAILURE(status)) {
       return;
    }
    callback = fCallbackFn;
    context  = fCallbackContext;
}


//--------------------------------------------------------------------------------
//
//     setMatchCallback
//
//--------------------------------------------------------------------------------
void RegexMatcher::setFindProgressCallback(URegexFindProgressCallback      *callback,
                                                const void                      *context,
                                                UErrorCode                      &status) {
    if (U_FAILURE(status)) {
        return;
    }
    fFindProgressCallbackFn = callback;
    fFindProgressCallbackContext = context;
}


//--------------------------------------------------------------------------------
//
//     getMatchCallback
//
//--------------------------------------------------------------------------------
void RegexMatcher::getFindProgressCallback(URegexFindProgressCallback    *&callback,
                                                const void                    *&context,
                                                UErrorCode                    &status) {
    if (U_FAILURE(status)) {
       return;
    }
    callback = fFindProgressCallbackFn;
    context  = fFindProgressCallbackContext;
}


//================================================================================
//
//    Code following this point in this file is the internal
//    Match Engine Implementation.
//
//================================================================================


//--------------------------------------------------------------------------------
//
//   resetStack
//           Discard any previous contents of the state save stack, and initialize a
//           new stack frame to all -1.  The -1s are needed for capture group limits,
//           where they indicate that a group has not yet matched anything.
//--------------------------------------------------------------------------------
REStackFrame *RegexMatcher::resetStack() {
    // Discard any previous contents of the state save stack, and initialize a
    //  new stack frame with all -1 data.  The -1s are needed for capture group limits,
    //  where they indicate that a group has not yet matched anything.
    fStack->removeAllElements();

    REStackFrame* iFrame = reinterpret_cast<REStackFrame*>(fStack->reserveBlock(fPattern->fFrameSize, fDeferredStatus));
    if(U_FAILURE(fDeferredStatus)) {
        return nullptr;
    }

    int32_t i;
    for (i=0; i<fPattern->fFrameSize-RESTACKFRAME_HDRCOUNT; i++) {
        iFrame->fExtra[i] = -1;
    }
    return iFrame;
}



//--------------------------------------------------------------------------------
//
//   isWordBoundary
//                     in perl, "xab..cd..", \b is true at positions 0,3,5,7
//                     For us,
//                       If the current char is a combining mark,
//                          \b is false.
//                       Else Scan backwards to the first non-combining char.
//                            We are at a boundary if the this char and the original chars are
//                               opposite in membership in \w set
//
//          parameters:   pos   - the current position in the input buffer
//
//              TODO:  double-check edge cases at region boundaries.
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::isWordBoundary(int64_t pos) {
    UBool isBoundary = false;
    UBool cIsWord    = false;

    if (pos >= fLookLimit) {
        fHitEnd = true;
    } else {
        // Determine whether char c at current position is a member of the word set of chars.
        // If we're off the end of the string, behave as though we're not at a word char.
        UTEXT_SETNATIVEINDEX(fInputText, pos);
        UChar32  c = UTEXT_CURRENT32(fInputText);
        if (u_hasBinaryProperty(c, UCHAR_GRAPHEME_EXTEND) || u_charType(c) == U_FORMAT_CHAR) {
            // Current char is a combining one.  Not a boundary.
            return false;
        }
        cIsWord = RegexStaticSets::gStaticSets->fPropSets[URX_ISWORD_SET].contains(c);
    }

    // Back up until we come to a non-combining char, determine whether
    //  that char is a word char.
    UBool prevCIsWord = false;
    for (;;) {
        if (UTEXT_GETNATIVEINDEX(fInputText) <= fLookStart) {
            break;
        }
        UChar32 prevChar = UTEXT_PREVIOUS32(fInputText);
        if (!(u_hasBinaryProperty(prevChar, UCHAR_GRAPHEME_EXTEND)
              || u_charType(prevChar) == U_FORMAT_CHAR)) {
            prevCIsWord = RegexStaticSets::gStaticSets->fPropSets[URX_ISWORD_SET].contains(prevChar);
            break;
        }
    }
    isBoundary = cIsWord ^ prevCIsWord;
    return isBoundary;
}

UBool RegexMatcher::isChunkWordBoundary(int32_t pos) {
    UBool isBoundary = false;
    UBool cIsWord    = false;

    const char16_t *inputBuf = fInputText->chunkContents;

    if (pos >= fLookLimit) {
        fHitEnd = true;
    } else {
        // Determine whether char c at current position is a member of the word set of chars.
        // If we're off the end of the string, behave as though we're not at a word char.
        UChar32 c;
        U16_GET(inputBuf, fLookStart, pos, fLookLimit, c);
        if (u_hasBinaryProperty(c, UCHAR_GRAPHEME_EXTEND) || u_charType(c) == U_FORMAT_CHAR) {
            // Current char is a combining one.  Not a boundary.
            return false;
        }
        cIsWord = RegexStaticSets::gStaticSets->fPropSets[URX_ISWORD_SET].contains(c);
    }

    // Back up until we come to a non-combining char, determine whether
    //  that char is a word char.
    UBool prevCIsWord = false;
    for (;;) {
        if (pos <= fLookStart) {
            break;
        }
        UChar32 prevChar;
        U16_PREV(inputBuf, fLookStart, pos, prevChar);
        if (!(u_hasBinaryProperty(prevChar, UCHAR_GRAPHEME_EXTEND)
              || u_charType(prevChar) == U_FORMAT_CHAR)) {
            prevCIsWord = RegexStaticSets::gStaticSets->fPropSets[URX_ISWORD_SET].contains(prevChar);
            break;
        }
    }
    isBoundary = cIsWord ^ prevCIsWord;
    return isBoundary;
}

//--------------------------------------------------------------------------------
//
//   isUWordBoundary
//
//         Test for a word boundary using RBBI word break.
//
//          parameters:   pos   - the current position in the input buffer
//
//--------------------------------------------------------------------------------
UBool RegexMatcher::isUWordBoundary(int64_t pos, UErrorCode &status) {
    UBool       returnVal = false;

#if UCONFIG_NO_BREAK_ITERATION==0
    // Note: this point will never be reached if break iteration is configured out.
    //       Regex patterns that would require this function will fail to compile.

    // If we haven't yet created a break iterator for this matcher, do it now.
    if (fWordBreakItr == nullptr) {
        fWordBreakItr = BreakIterator::createWordInstance(Locale::getEnglish(), status);
        if (U_FAILURE(status)) {
            return false;
        }
        fWordBreakItr->setText(fInputText, status);
    }

    // Note: zero width boundary tests like \b see through transparent region bounds,
    //       which is why fLookLimit is used here, rather than fActiveLimit.
    if (pos >= fLookLimit) {
        fHitEnd = true;
        returnVal = true;   // With Unicode word rules, only positions within the interior of "real"
                            //    words are not boundaries.  All non-word chars stand by themselves,
                            //    with word boundaries on both sides.
    } else {
        returnVal = fWordBreakItr->isBoundary(static_cast<int32_t>(pos));
    }
#endif
    return   returnVal;
}


int64_t RegexMatcher::followingGCBoundary(int64_t pos, UErrorCode &status) {
    int64_t result = pos;

#if UCONFIG_NO_BREAK_ITERATION==0
    // Note: this point will never be reached if break iteration is configured out.
    //       Regex patterns that would require this function will fail to compile.

    // If we haven't yet created a break iterator for this matcher, do it now.
    if (fGCBreakItr == nullptr) {
        fGCBreakItr = BreakIterator::createCharacterInstance(Locale::getEnglish(), status);
        if (U_FAILURE(status)) {
            return pos;
        }
        fGCBreakItr->setText(fInputText, status);
    }
    result = fGCBreakItr->following(pos);
    if (result == BreakIterator::DONE) {
        result = pos;
    }
#endif
    return result;
}

//--------------------------------------------------------------------------------
//
//   IncrementTime     This function is called once each TIMER_INITIAL_VALUE state
//                     saves. Increment the "time" counter, and call the
//                     user callback function if there is one installed.
//
//                     If the match operation needs to be aborted, either for a time-out
//                     or because the user callback asked for it, just set an error status.
//                     The engine will pick that up and stop in its outer loop.
//
//--------------------------------------------------------------------------------
void RegexMatcher::IncrementTime(UErrorCode &status) {
    fTickCounter = TIMER_INITIAL_VALUE;
    fTime++;
    if (fCallbackFn != nullptr) {
        if ((*fCallbackFn)(fCallbackContext, fTime) == false) {
            status = U_REGEX_STOPPED_BY_CALLER;
            return;
        }
    }
    if (fTimeLimit > 0 && fTime >= fTimeLimit) {
        status = U_REGEX_TIME_OUT;
    }
}

//--------------------------------------------------------------------------------
//
//   StateSave
//       Make a new stack frame, initialized as a copy of the current stack frame.
//       Set the pattern index in the original stack frame from the operand value
//       in the opcode.  Execution of the engine continues with the state in
//       the newly created stack frame
//
//       Note that reserveBlock() may grow the stack, resulting in the
//       whole thing being relocated in memory.
//
//    Parameters:
//       fp           The top frame pointer when called.  At return, a new
//                    fame will be present
//       savePatIdx   An index into the compiled pattern.  Goes into the original
//                    (not new) frame.  If execution ever back-tracks out of the
//                    new frame, this will be where we continue from in the pattern.
//    Return
//                    The new frame pointer.
//
//--------------------------------------------------------------------------------
inline REStackFrame *RegexMatcher::StateSave(REStackFrame *fp, int64_t savePatIdx, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return fp;
    }
    // push storage for a new frame.
    int64_t *newFP = fStack->reserveBlock(fFrameSize, status);
    if (U_FAILURE(status)) {
        // Failure on attempted stack expansion.
        //   Stack function set some other error code, change it to a more
        //   specific one for regular expressions.
        status = U_REGEX_STACK_OVERFLOW;
        // We need to return a writable stack frame, so just return the
        //    previous frame.  The match operation will stop quickly
        //    because of the error status, after which the frame will never
        //    be looked at again.
        return fp;
    }
    fp = reinterpret_cast<REStackFrame*>(newFP - fFrameSize); // in case of realloc of stack.

    // New stack frame = copy of old top frame.
    int64_t* source = reinterpret_cast<int64_t*>(fp);
    int64_t *dest   = newFP;
    for (;;) {
        *dest++ = *source++;
        if (source == newFP) {
            break;
        }
    }

    fTickCounter--;
    if (fTickCounter <= 0) {
       IncrementTime(status);    // Re-initializes fTickCounter
    }
    fp->fPatIdx = savePatIdx;
    return reinterpret_cast<REStackFrame*>(newFP);
}

#if defined(REGEX_DEBUG)
namespace {
UnicodeString StringFromUText(UText *ut) {
    UnicodeString result;
    for (UChar32 c = utext_next32From(ut, 0); c != U_SENTINEL; c = UTEXT_NEXT32(ut)) {
        result.append(c);
    }
    return result;
}
}
#endif // REGEX_DEBUG


//--------------------------------------------------------------------------------
//
//   MatchAt      This is the actual matching engine.
//
//                  startIdx:    begin matching a this index.
//                  toEnd:       if true, match must extend to end of the input region
//
//--------------------------------------------------------------------------------
void RegexMatcher::MatchAt(int64_t startIdx, UBool toEnd, UErrorCode &status) {
    UBool       isMatch  = false;      // True if the we have a match.

    int64_t     backSearchIndex = U_INT64_MAX; // used after greedy single-character matches for searching backwards

    int32_t     op;                    // Operation from the compiled pattern, split into
    int32_t     opType;                //    the opcode
    int32_t     opValue;               //    and the operand value.

#ifdef REGEX_RUN_DEBUG
    if (fTraceDebug) {
        printf("MatchAt(startIdx=%ld)\n", startIdx);
        printf("Original Pattern: \"%s\"\n", CStr(StringFromUText(fPattern->fPattern))());
        printf("Input String:     \"%s\"\n\n", CStr(StringFromUText(fInputText))());
    }
#endif

    if (U_FAILURE(status)) {
        return;
    }

    //  Cache frequently referenced items from the compiled pattern
    //
    int64_t             *pat           = fPattern->fCompiledPat->getBuffer();

    const char16_t      *litText       = fPattern->fLiteralText.getBuffer();
    UVector             *fSets         = fPattern->fSets;

    fFrameSize = fPattern->fFrameSize;
    REStackFrame        *fp            = resetStack();
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return;
    }

    fp->fPatIdx   = 0;
    fp->fInputIdx = startIdx;

    // Zero out the pattern's static data
    int32_t i;
    for (i = 0; i<fPattern->fDataSize; i++) {
        fData[i] = 0;
    }

    //
    //  Main loop for interpreting the compiled pattern.
    //  One iteration of the loop per pattern operation performed.
    //
    for (;;) {
        op = static_cast<int32_t>(pat[fp->fPatIdx]);
        opType  = URX_TYPE(op);
        opValue = URX_VAL(op);
#ifdef REGEX_RUN_DEBUG
        if (fTraceDebug) {
            UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
            printf("inputIdx=%ld   inputChar=%x   sp=%3ld   activeLimit=%ld  ", fp->fInputIdx,
                UTEXT_CURRENT32(fInputText), (int64_t *)fp-fStack->getBuffer(), fActiveLimit);
            fPattern->dumpOp(fp->fPatIdx);
        }
#endif
        fp->fPatIdx++;

        switch (opType) {


        case URX_NOP:
            break;


        case URX_BACKTRACK:
            // Force a backtrack.  In some circumstances, the pattern compiler
            //   will notice that the pattern can't possibly match anything, and will
            //   emit one of these at that point.
            fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            break;


        case URX_ONECHAR:
            if (fp->fInputIdx < fActiveLimit) {
                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                UChar32 c = UTEXT_NEXT32(fInputText);
                if (c == opValue) {
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                    break;
                }
            } else {
                fHitEnd = true;
            }
            fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            break;


        case URX_STRING:
            {
                // Test input against a literal string.
                // Strings require two slots in the compiled pattern, one for the
                //   offset to the string text, and one for the length.

                int32_t   stringStartIdx = opValue;
                op = static_cast<int32_t>(pat[fp->fPatIdx]); // Fetch the second operand
                fp->fPatIdx++;
                opType    = URX_TYPE(op);
                int32_t stringLen = URX_VAL(op);
                U_ASSERT(opType == URX_STRING_LEN);
                U_ASSERT(stringLen >= 2);

                const char16_t *patternString = litText+stringStartIdx;
                int32_t patternStringIndex = 0;
                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                UChar32 inputChar;
                UChar32 patternChar;
                UBool success = true;
                while (patternStringIndex < stringLen) {
                    if (UTEXT_GETNATIVEINDEX(fInputText) >= fActiveLimit) {
                        success = false;
                        fHitEnd = true;
                        break;
                    }
                    inputChar = UTEXT_NEXT32(fInputText);
                    U16_NEXT(patternString, patternStringIndex, stringLen, patternChar);
                    if (patternChar != inputChar) {
                        success = false;
                        break;
                    }
                }

                if (success) {
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_STATE_SAVE:
            fp = StateSave(fp, opValue, status);
            break;


        case URX_END:
            // The match loop will exit via this path on a successful match,
            //   when we reach the end of the pattern.
            if (toEnd && fp->fInputIdx != fActiveLimit) {
                // The pattern matched, but not to the end of input.  Try some more.
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                break;
            }
            isMatch = true;
            goto  breakFromLoop;

        // Start and End Capture stack frame variables are laid out out like this:
            //  fp->fExtra[opValue]  - The start of a completed capture group
            //             opValue+1 - The end   of a completed capture group
            //             opValue+2 - the start of a capture group whose end
            //                          has not yet been reached (and might not ever be).
        case URX_START_CAPTURE:
            U_ASSERT(opValue >= 0 && opValue < fFrameSize-3);
            fp->fExtra[opValue+2] = fp->fInputIdx;
            break;


        case URX_END_CAPTURE:
            U_ASSERT(opValue >= 0 && opValue < fFrameSize-3);
            U_ASSERT(fp->fExtra[opValue+2] >= 0);            // Start pos for this group must be set.
            fp->fExtra[opValue]   = fp->fExtra[opValue+2];   // Tentative start becomes real.
            fp->fExtra[opValue+1] = fp->fInputIdx;           // End position
            U_ASSERT(fp->fExtra[opValue] <= fp->fExtra[opValue+1]);
            break;


        case URX_DOLLAR:                   //  $, test for End of line
                                           //     or for position before new line at end of input
            {
                if (fp->fInputIdx >= fAnchorLimit) {
                    // We really are at the end of input.  Success.
                    fHitEnd = true;
                    fRequireEnd = true;
                    break;
                }

                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);

                // If we are positioned just before a new-line that is located at the
                //   end of input, succeed.
                UChar32 c = UTEXT_NEXT32(fInputText);
                if (UTEXT_GETNATIVEINDEX(fInputText) >= fAnchorLimit) {
                    if (isLineTerminator(c)) {
                        // If not in the middle of a CR/LF sequence
                        if ( !(c==0x0a && fp->fInputIdx>fAnchorStart && ((void)UTEXT_PREVIOUS32(fInputText), UTEXT_PREVIOUS32(fInputText))==0x0d)) {
                            // At new-line at end of input. Success
                            fHitEnd = true;
                            fRequireEnd = true;

                            break;
                        }
                    }
                } else {
                    UChar32 nextC = UTEXT_NEXT32(fInputText);
                    if (c == 0x0d && nextC == 0x0a && UTEXT_GETNATIVEINDEX(fInputText) >= fAnchorLimit) {
                        fHitEnd = true;
                        fRequireEnd = true;
                        break;                         // At CR/LF at end of input.  Success
                    }
                }

                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


         case URX_DOLLAR_D:                   //  $, test for End of Line, in UNIX_LINES mode.
            if (fp->fInputIdx >= fAnchorLimit) {
                // Off the end of input.  Success.
                fHitEnd = true;
                fRequireEnd = true;
                break;
            } else {
                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                UChar32 c = UTEXT_NEXT32(fInputText);
                // Either at the last character of input, or off the end.
                if (c == 0x0a && UTEXT_GETNATIVEINDEX(fInputText) == fAnchorLimit) {
                    fHitEnd = true;
                    fRequireEnd = true;
                    break;
                }
            }

            // Not at end of input.  Back-track out.
            fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            break;


         case URX_DOLLAR_M:                //  $, test for End of line in multi-line mode
             {
                 if (fp->fInputIdx >= fAnchorLimit) {
                     // We really are at the end of input.  Success.
                     fHitEnd = true;
                     fRequireEnd = true;
                     break;
                 }
                 // If we are positioned just before a new-line, succeed.
                 // It makes no difference where the new-line is within the input.
                 UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                 UChar32 c = UTEXT_CURRENT32(fInputText);
                 if (isLineTerminator(c)) {
                     // At a line end, except for the odd chance of  being in the middle of a CR/LF sequence
                     //  In multi-line mode, hitting a new-line just before the end of input does not
                     //   set the hitEnd or requireEnd flags
                     if ( !(c==0x0a && fp->fInputIdx>fAnchorStart && UTEXT_PREVIOUS32(fInputText)==0x0d)) {
                        break;
                     }
                 }
                 // not at a new line.  Fail.
                 fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
             }
             break;


         case URX_DOLLAR_MD:                //  $, test for End of line in multi-line and UNIX_LINES mode
             {
                 if (fp->fInputIdx >= fAnchorLimit) {
                     // We really are at the end of input.  Success.
                     fHitEnd = true;
                     fRequireEnd = true;  // Java set requireEnd in this case, even though
                     break;               //   adding a new-line would not lose the match.
                 }
                 // If we are not positioned just before a new-line, the test fails; backtrack out.
                 // It makes no difference where the new-line is within the input.
                 UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                 if (UTEXT_CURRENT32(fInputText) != 0x0a) {
                     fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                 }
             }
             break;


       case URX_CARET:                    //  ^, test for start of line
            if (fp->fInputIdx != fAnchorStart) {
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


       case URX_CARET_M:                   //  ^, test for start of line in mulit-line mode
           {
               if (fp->fInputIdx == fAnchorStart) {
                   // We are at the start input.  Success.
                   break;
               }
               // Check whether character just before the current pos is a new-line
               //   unless we are at the end of input
               UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
               UChar32  c = UTEXT_PREVIOUS32(fInputText);
               if ((fp->fInputIdx < fAnchorLimit) && isLineTerminator(c)) {
                   //  It's a new-line.  ^ is true.  Success.
                   //  TODO:  what should be done with positions between a CR and LF?
                   break;
               }
               // Not at the start of a line.  Fail.
               fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
           }
           break;


       case URX_CARET_M_UNIX:       //  ^, test for start of line in mulit-line + Unix-line mode
           {
               U_ASSERT(fp->fInputIdx >= fAnchorStart);
               if (fp->fInputIdx <= fAnchorStart) {
                   // We are at the start input.  Success.
                   break;
               }
               // Check whether character just before the current pos is a new-line
               U_ASSERT(fp->fInputIdx <= fAnchorLimit);
               UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
               UChar32  c = UTEXT_PREVIOUS32(fInputText);
               if (c != 0x0a) {
                   // Not at the start of a line.  Back-track out.
                   fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
               }
           }
           break;

        case URX_BACKSLASH_B:          // Test for word boundaries
            {
                UBool success = isWordBoundary(fp->fInputIdx);
                success ^= static_cast<UBool>(opValue != 0); // flip sense for \B
                if (!success) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_BU:          // Test for word boundaries, Unicode-style
            {
                UBool success = isUWordBoundary(fp->fInputIdx, status);
                success ^= static_cast<UBool>(opValue != 0); // flip sense for \B
                if (!success) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_D:            // Test for decimal digit
            {
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);

                UChar32 c = UTEXT_NEXT32(fInputText);
                int8_t ctype = u_charType(c);     // TODO:  make a unicode set for this.  Will be faster.
                UBool success = (ctype == U_DECIMAL_DIGIT_NUMBER);
                success ^= static_cast<UBool>(opValue != 0); // flip sense for \D
                if (success) {
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_G:          // Test for position at end of previous match
            if (!((fMatch && fp->fInputIdx==fMatchEnd) || (fMatch==false && fp->fInputIdx==fActiveStart))) {
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_BACKSLASH_H:            // Test for \h, horizontal white space.
            {
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }
                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                UChar32 c = UTEXT_NEXT32(fInputText);
                int8_t ctype = u_charType(c);
                UBool success = (ctype == U_SPACE_SEPARATOR || c == 9);  // SPACE_SEPARATOR || TAB
                success ^= static_cast<UBool>(opValue != 0);  // flip sense for \H
                if (success) {
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_R:            // Test for \R, any line break sequence.
            {
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }
                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                UChar32 c = UTEXT_NEXT32(fInputText);
                if (isLineTerminator(c)) {
                    if (c == 0x0d && utext_current32(fInputText) == 0x0a) {
                        utext_next32(fInputText);
                    }
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_V:            // \v, any single line ending character.
            {
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }
                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                UChar32 c = UTEXT_NEXT32(fInputText);
                UBool success = isLineTerminator(c);
                success ^= static_cast<UBool>(opValue != 0); // flip sense for \V
                if (success) {
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_X:
            //  Match a Grapheme, as defined by Unicode UAX 29.

            // Fail if at end of input
            if (fp->fInputIdx >= fActiveLimit) {
                fHitEnd = true;
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                break;
            }

            fp->fInputIdx = followingGCBoundary(fp->fInputIdx, status);
            if (fp->fInputIdx >= fActiveLimit) {
                fHitEnd = true;
                fp->fInputIdx = fActiveLimit;
            }
            break;


        case URX_BACKSLASH_Z:          // Test for end of Input
            if (fp->fInputIdx < fAnchorLimit) {
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            } else {
                fHitEnd = true;
                fRequireEnd = true;
            }
            break;



        case URX_STATIC_SETREF:
            {
                // Test input character against one of the predefined sets
                //    (Word Characters, for example)
                // The high bit of the op value is a flag for the match polarity.
                //    0:   success if input char is in set.
                //    1:   success if input char is not in set.
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                UBool success = ((opValue & URX_NEG_SET) == URX_NEG_SET);
                opValue &= ~URX_NEG_SET;
                U_ASSERT(opValue > 0 && opValue < URX_LAST_SET);

                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                UChar32 c = UTEXT_NEXT32(fInputText);
                if (c < 256) {
                    Regex8BitSet &s8 = RegexStaticSets::gStaticSets->fPropSets8[opValue];
                    if (s8.contains(c)) {
                        success = !success;
                    }
                } else {
                    const UnicodeSet &s = RegexStaticSets::gStaticSets->fPropSets[opValue];
                    if (s.contains(c)) {
                        success = !success;
                    }
                }
                if (success) {
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                } else {
                    // the character wasn't in the set.
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_STAT_SETREF_N:
            {
                // Test input character for NOT being a member of  one of
                //    the predefined sets (Word Characters, for example)
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                U_ASSERT(opValue > 0 && opValue < URX_LAST_SET);

                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);

                UChar32 c = UTEXT_NEXT32(fInputText);
                if (c < 256) {
                    Regex8BitSet &s8 = RegexStaticSets::gStaticSets->fPropSets8[opValue];
                    if (s8.contains(c) == false) {
                        fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                        break;
                    }
                } else {
                    const UnicodeSet &s = RegexStaticSets::gStaticSets->fPropSets[opValue];
                    if (s.contains(c) == false) {
                        fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                        break;
                    }
                }
                // the character wasn't in the set.
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_SETREF:
            if (fp->fInputIdx >= fActiveLimit) {
                fHitEnd = true;
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                break;
            } else {
                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);

                // There is input left.  Pick up one char and test it for set membership.
                UChar32 c = UTEXT_NEXT32(fInputText);
                U_ASSERT(opValue > 0 && opValue < fSets->size());
                if (c<256) {
                    Regex8BitSet *s8 = &fPattern->fSets8[opValue];
                    if (s8->contains(c)) {
                        fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                        break;
                    }
                } else {
                    UnicodeSet* s = static_cast<UnicodeSet*>(fSets->elementAt(opValue));
                    if (s->contains(c)) {
                        // The character is in the set.  A Match.
                        fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                        break;
                    }
                }

                // the character wasn't in the set.
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_DOTANY:
            {
                // . matches anything, but stops at end-of-line.
                if (fp->fInputIdx >= fActiveLimit) {
                    // At end of input.  Match failed.  Backtrack out.
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);

                // There is input left.  Advance over one char, unless we've hit end-of-line
                UChar32 c = UTEXT_NEXT32(fInputText);
                if (isLineTerminator(c)) {
                    // End of line in normal mode.   . does not match.
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }
                fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
            }
            break;


        case URX_DOTANY_ALL:
            {
                // ., in dot-matches-all (including new lines) mode
                if (fp->fInputIdx >= fActiveLimit) {
                    // At end of input.  Match failed.  Backtrack out.
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);

                // There is input left.  Advance over one char, except if we are
                //   at a cr/lf, advance over both of them.
                UChar32 c;
                c = UTEXT_NEXT32(fInputText);
                fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                if (c==0x0d && fp->fInputIdx < fActiveLimit) {
                    // In the case of a CR/LF, we need to advance over both.
                    UChar32 nextc = UTEXT_CURRENT32(fInputText);
                    if (nextc == 0x0a) {
                        (void)UTEXT_NEXT32(fInputText);
                        fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                    }
                }
            }
            break;


        case URX_DOTANY_UNIX:
            {
                // '.' operator, matches all, but stops at end-of-line.
                //   UNIX_LINES mode, so 0x0a is the only recognized line ending.
                if (fp->fInputIdx >= fActiveLimit) {
                    // At end of input.  Match failed.  Backtrack out.
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);

                // There is input left.  Advance over one char, unless we've hit end-of-line
                UChar32 c = UTEXT_NEXT32(fInputText);
                if (c == 0x0a) {
                    // End of line in normal mode.   '.' does not match the \n
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                } else {
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                }
            }
            break;


        case URX_JMP:
            fp->fPatIdx = opValue;
            break;

        case URX_FAIL:
            isMatch = false;
            goto breakFromLoop;

        case URX_JMP_SAV:
            U_ASSERT(opValue < fPattern->fCompiledPat->size());
            fp = StateSave(fp, fp->fPatIdx, status);       // State save to loc following current
            fp->fPatIdx = opValue;                         // Then JMP.
            break;

        case URX_JMP_SAV_X:
            // This opcode is used with (x)+, when x can match a zero length string.
            // Same as JMP_SAV, except conditional on the match having made forward progress.
            // Destination of the JMP must be a URX_STO_INP_LOC, from which we get the
            //   data address of the input position at the start of the loop.
            {
                U_ASSERT(opValue > 0 && opValue < fPattern->fCompiledPat->size());
                int32_t stoOp = static_cast<int32_t>(pat[opValue - 1]);
                U_ASSERT(URX_TYPE(stoOp) == URX_STO_INP_LOC);
                int32_t  frameLoc = URX_VAL(stoOp);
                U_ASSERT(frameLoc >= 0 && frameLoc < fFrameSize);
                int64_t prevInputIdx = fp->fExtra[frameLoc];
                U_ASSERT(prevInputIdx <= fp->fInputIdx);
                if (prevInputIdx < fp->fInputIdx) {
                    // The match did make progress.  Repeat the loop.
                    fp = StateSave(fp, fp->fPatIdx, status);  // State save to loc following current
                    fp->fPatIdx = opValue;
                    fp->fExtra[frameLoc] = fp->fInputIdx;
                }
                // If the input position did not advance, we do nothing here,
                //   execution will fall out of the loop.
            }
            break;

        case URX_CTR_INIT:
            {
                U_ASSERT(opValue >= 0 && opValue < fFrameSize-2);
                fp->fExtra[opValue] = 0;                 //  Set the loop counter variable to zero

                // Pick up the three extra operands that CTR_INIT has, and
                //    skip the pattern location counter past
                int32_t instrOperandLoc = static_cast<int32_t>(fp->fPatIdx);
                fp->fPatIdx += 3;
                int32_t loopLoc  = URX_VAL(pat[instrOperandLoc]);
                int32_t minCount = static_cast<int32_t>(pat[instrOperandLoc + 1]);
                int32_t maxCount = static_cast<int32_t>(pat[instrOperandLoc + 2]);
                U_ASSERT(minCount>=0);
                U_ASSERT(maxCount>=minCount || maxCount==-1);
                U_ASSERT(loopLoc>=fp->fPatIdx);

                if (minCount == 0) {
                    fp = StateSave(fp, loopLoc+1, status);
                }
                if (maxCount == -1) {
                    fp->fExtra[opValue+1] = fp->fInputIdx;   //  For loop breaking.
                } else if (maxCount == 0) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;

        case URX_CTR_LOOP:
            {
                U_ASSERT(opValue>0 && opValue < fp->fPatIdx-2);
                int32_t initOp = static_cast<int32_t>(pat[opValue]);
                U_ASSERT(URX_TYPE(initOp) == URX_CTR_INIT);
                int64_t *pCounter = &fp->fExtra[URX_VAL(initOp)];
                int32_t minCount = static_cast<int32_t>(pat[opValue + 2]);
                int32_t maxCount = static_cast<int32_t>(pat[opValue + 3]);
                (*pCounter)++;
                if (static_cast<uint64_t>(*pCounter) >= static_cast<uint32_t>(maxCount) && maxCount != -1) {
                    U_ASSERT(*pCounter == maxCount);
                    break;
                }
                if (*pCounter >= minCount) {
                    if (maxCount == -1) {
                        // Loop has no hard upper bound.
                        // Check that it is progressing through the input, break if it is not.
                        int64_t *pLastInputIdx =  &fp->fExtra[URX_VAL(initOp) + 1];
                        if (fp->fInputIdx == *pLastInputIdx) {
                            break;
                        } else {
                            *pLastInputIdx = fp->fInputIdx;
                        }
                    }
                    fp = StateSave(fp, fp->fPatIdx, status);
                } else {
                    // Increment time-out counter. (StateSave() does it if count >= minCount)
                    fTickCounter--;
                    if (fTickCounter <= 0) {
                        IncrementTime(status);    // Re-initializes fTickCounter
                    }
                }

                fp->fPatIdx = opValue + 4;    // Loop back.
            }
            break;

        case URX_CTR_INIT_NG:
            {
                // Initialize a non-greedy loop
                U_ASSERT(opValue >= 0 && opValue < fFrameSize-2);
                fp->fExtra[opValue] = 0;                 //  Set the loop counter variable to zero

                // Pick up the three extra operands that CTR_INIT_NG has, and
                //    skip the pattern location counter past
                int32_t instrOperandLoc = static_cast<int32_t>(fp->fPatIdx);
                fp->fPatIdx += 3;
                int32_t loopLoc  = URX_VAL(pat[instrOperandLoc]);
                int32_t minCount = static_cast<int32_t>(pat[instrOperandLoc + 1]);
                int32_t maxCount = static_cast<int32_t>(pat[instrOperandLoc + 2]);
                U_ASSERT(minCount>=0);
                U_ASSERT(maxCount>=minCount || maxCount==-1);
                U_ASSERT(loopLoc>fp->fPatIdx);
                if (maxCount == -1) {
                    fp->fExtra[opValue+1] = fp->fInputIdx;   //  Save initial input index for loop breaking.
                }

                if (minCount == 0) {
                    if (maxCount != 0) {
                        fp = StateSave(fp, fp->fPatIdx, status);
                    }
                    fp->fPatIdx = loopLoc+1;   // Continue with stuff after repeated block
                }
            }
            break;

        case URX_CTR_LOOP_NG:
            {
                // Non-greedy {min, max} loops
                U_ASSERT(opValue>0 && opValue < fp->fPatIdx-2);
                int32_t initOp = static_cast<int32_t>(pat[opValue]);
                U_ASSERT(URX_TYPE(initOp) == URX_CTR_INIT_NG);
                int64_t *pCounter = &fp->fExtra[URX_VAL(initOp)];
                int32_t minCount = static_cast<int32_t>(pat[opValue + 2]);
                int32_t maxCount = static_cast<int32_t>(pat[opValue + 3]);

                (*pCounter)++;
                if (static_cast<uint64_t>(*pCounter) >= static_cast<uint32_t>(maxCount) && maxCount != -1) {
                    // The loop has matched the maximum permitted number of times.
                    //   Break out of here with no action.  Matching will
                    //   continue with the following pattern.
                    U_ASSERT(*pCounter == maxCount);
                    break;
                }

                if (*pCounter < minCount) {
                    // We haven't met the minimum number of matches yet.
                    //   Loop back for another one.
                    fp->fPatIdx = opValue + 4;    // Loop back.
                    // Increment time-out counter. (StateSave() does it if count >= minCount)
                    fTickCounter--;
                    if (fTickCounter <= 0) {
                        IncrementTime(status);    // Re-initializes fTickCounter
                    }
                } else {
                    // We do have the minimum number of matches.

                    // If there is no upper bound on the loop iterations, check that the input index
                    // is progressing, and stop the loop if it is not.
                    if (maxCount == -1) {
                        int64_t *pLastInputIdx =  &fp->fExtra[URX_VAL(initOp) + 1];
                        if (fp->fInputIdx == *pLastInputIdx) {
                            break;
                        }
                        *pLastInputIdx = fp->fInputIdx;
                    }

                    // Loop Continuation: we will fall into the pattern following the loop
                    //   (non-greedy, don't execute loop body first), but first do
                    //   a state save to the top of the loop, so that a match failure
                    //   in the following pattern will try another iteration of the loop.
                    fp = StateSave(fp, opValue + 4, status);
                }
            }
            break;

        case URX_STO_SP:
            U_ASSERT(opValue >= 0 && opValue < fPattern->fDataSize);
            fData[opValue] = fStack->size();
            break;

        case URX_LD_SP:
            {
                U_ASSERT(opValue >= 0 && opValue < fPattern->fDataSize);
                int32_t newStackSize = static_cast<int32_t>(fData[opValue]);
                U_ASSERT(newStackSize <= fStack->size());
                int64_t *newFP = fStack->getBuffer() + newStackSize - fFrameSize;
                if (newFP == reinterpret_cast<int64_t*>(fp)) {
                    break;
                }
                int32_t j;
                for (j=0; j<fFrameSize; j++) {
                    newFP[j] = reinterpret_cast<int64_t*>(fp)[j];
                }
                fp = reinterpret_cast<REStackFrame*>(newFP);
                fStack->setSize(newStackSize);
            }
            break;

        case URX_BACKREF:
            {
                U_ASSERT(opValue < fFrameSize);
                int64_t groupStartIdx = fp->fExtra[opValue];
                int64_t groupEndIdx   = fp->fExtra[opValue+1];
                U_ASSERT(groupStartIdx <= groupEndIdx);
                if (groupStartIdx < 0) {
                    // This capture group has not participated in the match thus far,
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize)); // FAIL, no match.
                    break;
                }
                UTEXT_SETNATIVEINDEX(fAltInputText, groupStartIdx);
                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);

                //   Note: if the capture group match was of an empty string the backref
                //         match succeeds.  Verified by testing:  Perl matches succeed
                //         in this case, so we do too.

                UBool success = true;
                for (;;) {
                    if (utext_getNativeIndex(fAltInputText) >= groupEndIdx) {
                        success = true;
                        break;
                    }
                    if (utext_getNativeIndex(fInputText) >= fActiveLimit) {
                        success = false;
                        fHitEnd = true;
                        break;
                    }
                    UChar32 captureGroupChar = utext_next32(fAltInputText);
                    UChar32 inputChar = utext_next32(fInputText);
                    if (inputChar != captureGroupChar) {
                        success = false;
                        break;
                    }
                }

                if (success) {
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;



        case URX_BACKREF_I:
            {
                U_ASSERT(opValue < fFrameSize);
                int64_t groupStartIdx = fp->fExtra[opValue];
                int64_t groupEndIdx   = fp->fExtra[opValue+1];
                U_ASSERT(groupStartIdx <= groupEndIdx);
                if (groupStartIdx < 0) {
                    // This capture group has not participated in the match thus far,
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize)); // FAIL, no match.
                    break;
                }
                utext_setNativeIndex(fAltInputText, groupStartIdx);
                utext_setNativeIndex(fInputText, fp->fInputIdx);
                CaseFoldingUTextIterator captureGroupItr(*fAltInputText);
                CaseFoldingUTextIterator inputItr(*fInputText);

                //   Note: if the capture group match was of an empty string the backref
                //         match succeeds.  Verified by testing:  Perl matches succeed
                //         in this case, so we do too.

                UBool success = true;
                for (;;) {
                    if (!captureGroupItr.inExpansion() && utext_getNativeIndex(fAltInputText) >= groupEndIdx) {
                        success = true;
                        break;
                    }
                    if (!inputItr.inExpansion() && utext_getNativeIndex(fInputText) >= fActiveLimit) {
                        success = false;
                        fHitEnd = true;
                        break;
                    }
                    UChar32 captureGroupChar = captureGroupItr.next();
                    UChar32 inputChar = inputItr.next();
                    if (inputChar != captureGroupChar) {
                        success = false;
                        break;
                    }
                }

                if (success && inputItr.inExpansion()) {
                    // We obtained a match by consuming part of a string obtained from
                    // case-folding a single code point of the input text.
                    // This does not count as an overall match.
                    success = false;
                }

                if (success) {
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }

            }
            break;

        case URX_STO_INP_LOC:
            {
                U_ASSERT(opValue >= 0 && opValue < fFrameSize);
                fp->fExtra[opValue] = fp->fInputIdx;
            }
            break;

        case URX_JMPX:
            {
                int32_t instrOperandLoc = static_cast<int32_t>(fp->fPatIdx);
                fp->fPatIdx += 1;
                int32_t dataLoc  = URX_VAL(pat[instrOperandLoc]);
                U_ASSERT(dataLoc >= 0 && dataLoc < fFrameSize);
                int64_t savedInputIdx = fp->fExtra[dataLoc];
                U_ASSERT(savedInputIdx <= fp->fInputIdx);
                if (savedInputIdx < fp->fInputIdx) {
                    fp->fPatIdx = opValue;                               // JMP
                } else {
                     fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize)); // FAIL, no progress in loop.
                }
            }
            break;

        case URX_LA_START:
            {
                // Entering a look around block.
                // Save Stack Ptr, Input Pos.
                U_ASSERT(opValue>=0 && opValue+3<fPattern->fDataSize);
                fData[opValue]   = fStack->size();
                fData[opValue+1] = fp->fInputIdx;
                fData[opValue+2] = fActiveStart;
                fData[opValue+3] = fActiveLimit;
                fActiveStart     = fLookStart;          // Set the match region change for
                fActiveLimit     = fLookLimit;          //   transparent bounds.
            }
            break;

        case URX_LA_END:
            {
                // Leaving a look-ahead block.
                //  restore Stack Ptr, Input Pos to positions they had on entry to block.
                U_ASSERT(opValue>=0 && opValue+3<fPattern->fDataSize);
                int32_t stackSize = fStack->size();
                int32_t newStackSize = static_cast<int32_t>(fData[opValue]);
                U_ASSERT(stackSize >= newStackSize);
                if (stackSize > newStackSize) {
                    // Copy the current top frame back to the new (cut back) top frame.
                    //   This makes the capture groups from within the look-ahead
                    //   expression available.
                    int64_t *newFP = fStack->getBuffer() + newStackSize - fFrameSize;
                    int32_t j;
                    for (j=0; j<fFrameSize; j++) {
                        newFP[j] = reinterpret_cast<int64_t*>(fp)[j];
                    }
                    fp = reinterpret_cast<REStackFrame*>(newFP);
                    fStack->setSize(newStackSize);
                }
                fp->fInputIdx = fData[opValue+1];

                // Restore the active region bounds in the input string; they may have
                //    been changed because of transparent bounds on a Region.
                fActiveStart = fData[opValue+2];
                fActiveLimit = fData[opValue+3];
                U_ASSERT(fActiveStart >= 0);
                U_ASSERT(fActiveLimit <= fInputLength);
            }
            break;

        case URX_ONECHAR_I:
            // Case insensitive one char.  The char from the pattern is already case folded.
            // Input text is not, but case folding the input can not reduce two or more code
            // points to one.
            if (fp->fInputIdx < fActiveLimit) {
                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);

                UChar32 c = UTEXT_NEXT32(fInputText);
                if (u_foldCase(c, U_FOLD_CASE_DEFAULT) == opValue) {
                    fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                    break;
                }
            } else {
                fHitEnd = true;
            }

            fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            break;

        case URX_STRING_I:
            {
                // Case-insensitive test input against a literal string.
                // Strings require two slots in the compiled pattern, one for the
                //   offset to the string text, and one for the length.
                //   The compiled string has already been case folded.
                {
                    const char16_t *patternString = litText + opValue;
                    int32_t      patternStringIdx  = 0;

                    op = static_cast<int32_t>(pat[fp->fPatIdx]);
                    fp->fPatIdx++;
                    opType  = URX_TYPE(op);
                    opValue = URX_VAL(op);
                    U_ASSERT(opType == URX_STRING_LEN);
                    int32_t patternStringLen = opValue;  // Length of the string from the pattern.


                    UChar32   cPattern;
                    UChar32   cText;
                    UBool     success = true;

                    UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                    CaseFoldingUTextIterator inputIterator(*fInputText);
                    while (patternStringIdx < patternStringLen) {
                        if (!inputIterator.inExpansion() && UTEXT_GETNATIVEINDEX(fInputText) >= fActiveLimit) {
                            success = false;
                            fHitEnd = true;
                            break;
                        }
                        U16_NEXT(patternString, patternStringIdx, patternStringLen, cPattern);
                        cText = inputIterator.next();
                        if (cText != cPattern) {
                            success = false;
                            break;
                        }
                    }
                    if (inputIterator.inExpansion()) {
                        success = false;
                    }

                    if (success) {
                        fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                    } else {
                        fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    }
                }
            }
            break;

        case URX_LB_START:
            {
                // Entering a look-behind block.
                // Save Stack Ptr, Input Pos and active input region.
                //   TODO:  implement transparent bounds.  Ticket #6067
                U_ASSERT(opValue>=0 && opValue+4<fPattern->fDataSize);
                fData[opValue]   = fStack->size();
                fData[opValue+1] = fp->fInputIdx;
                // Save input string length, then reset to pin any matches to end at
                //   the current position.
                fData[opValue+2] = fActiveStart;
                fData[opValue+3] = fActiveLimit;
                fActiveStart     = fRegionStart;
                fActiveLimit     = fp->fInputIdx;
                // Init the variable containing the start index for attempted matches.
                fData[opValue+4] = -1;
            }
            break;


        case URX_LB_CONT:
            {
                // Positive Look-Behind, at top of loop checking for matches of LB expression
                //    at all possible input starting positions.

                // Fetch the min and max possible match lengths.  They are the operands
                //   of this op in the pattern.
                int32_t minML = static_cast<int32_t>(pat[fp->fPatIdx++]);
                int32_t maxML = static_cast<int32_t>(pat[fp->fPatIdx++]);
                if (!UTEXT_USES_U16(fInputText)) {
                    // utf-8 fix to maximum match length. The pattern compiler assumes utf-16.
                    // The max length need not be exact; it just needs to be >= actual maximum.
                    maxML *= 3;
                }
                U_ASSERT(minML <= maxML);
                U_ASSERT(minML >= 0);

                // Fetch (from data) the last input index where a match was attempted.
                U_ASSERT(opValue>=0 && opValue+4<fPattern->fDataSize);
                int64_t  &lbStartIdx = fData[opValue+4];
                if (lbStartIdx < 0) {
                    // First time through loop.
                    lbStartIdx = fp->fInputIdx - minML;
                    if (lbStartIdx > 0) {
                        // move index to a code point boundary, if it's not on one already.
                        UTEXT_SETNATIVEINDEX(fInputText, lbStartIdx);
                        lbStartIdx = UTEXT_GETNATIVEINDEX(fInputText);
                    }
                } else {
                    // 2nd through nth time through the loop.
                    // Back up start position for match by one.
                    if (lbStartIdx == 0) {
                        (lbStartIdx)--;
                    } else {
                        UTEXT_SETNATIVEINDEX(fInputText, lbStartIdx);
                        (void)UTEXT_PREVIOUS32(fInputText);
                        lbStartIdx = UTEXT_GETNATIVEINDEX(fInputText);
                    }
                }

                if (lbStartIdx < 0 || lbStartIdx < fp->fInputIdx - maxML) {
                    // We have tried all potential match starting points without
                    //  getting a match.  Backtrack out, and out of the
                    //   Look Behind altogether.
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    fActiveStart = fData[opValue+2];
                    fActiveLimit = fData[opValue+3];
                    U_ASSERT(fActiveStart >= 0);
                    U_ASSERT(fActiveLimit <= fInputLength);
                    break;
                }

                //    Save state to this URX_LB_CONT op, so failure to match will repeat the loop.
                //      (successful match will fall off the end of the loop.)
                fp = StateSave(fp, fp->fPatIdx-3, status);
                fp->fInputIdx = lbStartIdx;
            }
            break;

        case URX_LB_END:
            // End of a look-behind block, after a successful match.
            {
                U_ASSERT(opValue>=0 && opValue+4<fPattern->fDataSize);
                if (fp->fInputIdx != fActiveLimit) {
                    //  The look-behind expression matched, but the match did not
                    //    extend all the way to the point that we are looking behind from.
                    //  FAIL out of here, which will take us back to the LB_CONT, which
                    //     will retry the match starting at another position or fail
                    //     the look-behind altogether, whichever is appropriate.
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                // Look-behind match is good.  Restore the original input string region,
                //   which had been truncated to pin the end of the lookbehind match to the
                //   position being looked-behind.
                fActiveStart = fData[opValue+2];
                fActiveLimit = fData[opValue+3];
                U_ASSERT(fActiveStart >= 0);
                U_ASSERT(fActiveLimit <= fInputLength);
            }
            break;


        case URX_LBN_CONT:
            {
                // Negative Look-Behind, at top of loop checking for matches of LB expression
                //    at all possible input starting positions.

                // Fetch the extra parameters of this op.
                int32_t minML = static_cast<int32_t>(pat[fp->fPatIdx++]);
                int32_t maxML = static_cast<int32_t>(pat[fp->fPatIdx++]);
                if (!UTEXT_USES_U16(fInputText)) {
                    // utf-8 fix to maximum match length. The pattern compiler assumes utf-16.
                    // The max length need not be exact; it just needs to be >= actual maximum.
                    maxML *= 3;
                }
                int32_t continueLoc = static_cast<int32_t>(pat[fp->fPatIdx++]);
                        continueLoc = URX_VAL(continueLoc);
                U_ASSERT(minML <= maxML);
                U_ASSERT(minML >= 0);
                U_ASSERT(continueLoc > fp->fPatIdx);

                // Fetch (from data) the last input index where a match was attempted.
                U_ASSERT(opValue>=0 && opValue+4<fPattern->fDataSize);
                int64_t  &lbStartIdx = fData[opValue+4];
                if (lbStartIdx < 0) {
                    // First time through loop.
                    lbStartIdx = fp->fInputIdx - minML;
                    if (lbStartIdx > 0) {
                        // move index to a code point boundary, if it's not on one already.
                        UTEXT_SETNATIVEINDEX(fInputText, lbStartIdx);
                        lbStartIdx = UTEXT_GETNATIVEINDEX(fInputText);
                    }
                } else {
                    // 2nd through nth time through the loop.
                    // Back up start position for match by one.
                    if (lbStartIdx == 0) {
                        (lbStartIdx)--;
                    } else {
                        UTEXT_SETNATIVEINDEX(fInputText, lbStartIdx);
                        (void)UTEXT_PREVIOUS32(fInputText);
                        lbStartIdx = UTEXT_GETNATIVEINDEX(fInputText);
                    }
                }

                if (lbStartIdx < 0 || lbStartIdx < fp->fInputIdx - maxML) {
                    // We have tried all potential match starting points without
                    //  getting a match, which means that the negative lookbehind as
                    //  a whole has succeeded.  Jump forward to the continue location
                    fActiveStart = fData[opValue+2];
                    fActiveLimit = fData[opValue+3];
                    U_ASSERT(fActiveStart >= 0);
                    U_ASSERT(fActiveLimit <= fInputLength);
                    fp->fPatIdx = continueLoc;
                    break;
                }

                //    Save state to this URX_LB_CONT op, so failure to match will repeat the loop.
                //      (successful match will cause a FAIL out of the loop altogether.)
                fp = StateSave(fp, fp->fPatIdx-4, status);
                fp->fInputIdx = lbStartIdx;
            }
            break;

        case URX_LBN_END:
            // End of a negative look-behind block, after a successful match.
            {
                U_ASSERT(opValue>=0 && opValue+4<fPattern->fDataSize);
                if (fp->fInputIdx != fActiveLimit) {
                    //  The look-behind expression matched, but the match did not
                    //    extend all the way to the point that we are looking behind from.
                    //  FAIL out of here, which will take us back to the LB_CONT, which
                    //     will retry the match starting at another position or succeed
                    //     the look-behind altogether, whichever is appropriate.
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                // Look-behind expression matched, which means look-behind test as
                //   a whole Fails

                //   Restore the original input string length, which had been truncated
                //   inorder to pin the end of the lookbehind match
                //   to the position being looked-behind.
                fActiveStart = fData[opValue+2];
                fActiveLimit = fData[opValue+3];
                U_ASSERT(fActiveStart >= 0);
                U_ASSERT(fActiveLimit <= fInputLength);

                // Restore original stack position, discarding any state saved
                //   by the successful pattern match.
                U_ASSERT(opValue>=0 && opValue+1<fPattern->fDataSize);
                int32_t newStackSize = static_cast<int32_t>(fData[opValue]);
                U_ASSERT(fStack->size() > newStackSize);
                fStack->setSize(newStackSize);

                //  FAIL, which will take control back to someplace
                //  prior to entering the look-behind test.
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_LOOP_SR_I:
            // Loop Initialization for the optimized implementation of
            //     [some character set]*
            //   This op scans through all matching input.
            //   The following LOOP_C op emulates stack unwinding if the following pattern fails.
            {
                U_ASSERT(opValue > 0 && opValue < fSets->size());
                Regex8BitSet *s8 = &fPattern->fSets8[opValue];
                UnicodeSet* s = static_cast<UnicodeSet*>(fSets->elementAt(opValue));

                // Loop through input, until either the input is exhausted or
                //   we reach a character that is not a member of the set.
                int64_t ix = fp->fInputIdx;
                UTEXT_SETNATIVEINDEX(fInputText, ix);
                for (;;) {
                    if (ix >= fActiveLimit) {
                        fHitEnd = true;
                        break;
                    }
                    UChar32 c = UTEXT_NEXT32(fInputText);
                    if (c<256) {
                        if (s8->contains(c) == false) {
                            break;
                        }
                    } else {
                        if (s->contains(c) == false) {
                            break;
                        }
                    }
                    ix = UTEXT_GETNATIVEINDEX(fInputText);
                }

                // If there were no matching characters, skip over the loop altogether.
                //   The loop doesn't run at all, a * op always succeeds.
                if (ix == fp->fInputIdx) {
                    fp->fPatIdx++;   // skip the URX_LOOP_C op.
                    break;
                }

                // Peek ahead in the compiled pattern, to the URX_LOOP_C that
                //   must follow.  It's operand is the stack location
                //   that holds the starting input index for the match of this [set]*
                int32_t loopcOp = static_cast<int32_t>(pat[fp->fPatIdx]);
                U_ASSERT(URX_TYPE(loopcOp) == URX_LOOP_C);
                int32_t stackLoc = URX_VAL(loopcOp);
                U_ASSERT(stackLoc >= 0 && stackLoc < fFrameSize);
                fp->fExtra[stackLoc] = fp->fInputIdx;
                fp->fInputIdx = ix;

                // Save State to the URX_LOOP_C op that follows this one,
                //   so that match failures in the following code will return to there.
                //   Then bump the pattern idx so the LOOP_C is skipped on the way out of here.
                fp = StateSave(fp, fp->fPatIdx, status);
                fp->fPatIdx++;
            }
            break;


        case URX_LOOP_DOT_I:
            // Loop Initialization for the optimized implementation of .*
            //   This op scans through all remaining input.
            //   The following LOOP_C op emulates stack unwinding if the following pattern fails.
            {
                // Loop through input until the input is exhausted (we reach an end-of-line)
                // In DOTALL mode, we can just go straight to the end of the input.
                int64_t ix;
                if ((opValue & 1) == 1) {
                    // Dot-matches-All mode.  Jump straight to the end of the string.
                    ix = fActiveLimit;
                    fHitEnd = true;
                } else {
                    // NOT DOT ALL mode.  Line endings do not match '.'
                    // Scan forward until a line ending or end of input.
                    ix = fp->fInputIdx;
                    UTEXT_SETNATIVEINDEX(fInputText, ix);
                    for (;;) {
                        if (ix >= fActiveLimit) {
                            fHitEnd = true;
                            break;
                        }
                        UChar32 c = UTEXT_NEXT32(fInputText);
                        if ((c & 0x7f) <= 0x29) {          // Fast filter of non-new-line-s
                            if ((c == 0x0a) ||             //  0x0a is newline in both modes.
                               (((opValue & 2) == 0) &&    // IF not UNIX_LINES mode
                                    isLineTerminator(c))) {
                                //  char is a line ending.  Exit the scanning loop.
                                break;
                            }
                        }
                        ix = UTEXT_GETNATIVEINDEX(fInputText);
                    }
                }

                // If there were no matching characters, skip over the loop altogether.
                //   The loop doesn't run at all, a * op always succeeds.
                if (ix == fp->fInputIdx) {
                    fp->fPatIdx++;   // skip the URX_LOOP_C op.
                    break;
                }

                // Peek ahead in the compiled pattern, to the URX_LOOP_C that
                //   must follow.  It's operand is the stack location
                //   that holds the starting input index for the match of this .*
                int32_t loopcOp = static_cast<int32_t>(pat[fp->fPatIdx]);
                U_ASSERT(URX_TYPE(loopcOp) == URX_LOOP_C);
                int32_t stackLoc = URX_VAL(loopcOp);
                U_ASSERT(stackLoc >= 0 && stackLoc < fFrameSize);
                fp->fExtra[stackLoc] = fp->fInputIdx;
                fp->fInputIdx = ix;

                // Save State to the URX_LOOP_C op that follows this one,
                //   so that match failures in the following code will return to there.
                //   Then bump the pattern idx so the LOOP_C is skipped on the way out of here.
                fp = StateSave(fp, fp->fPatIdx, status);
                fp->fPatIdx++;
            }
            break;


        case URX_LOOP_C:
            {
                U_ASSERT(opValue>=0 && opValue<fFrameSize);
                backSearchIndex = fp->fExtra[opValue];
                U_ASSERT(backSearchIndex <= fp->fInputIdx);
                if (backSearchIndex == fp->fInputIdx) {
                    // We've backed up the input idx to the point that the loop started.
                    // The loop is done.  Leave here without saving state.
                    //  Subsequent failures won't come back here.
                    break;
                }
                // Set up for the next iteration of the loop, with input index
                //   backed up by one from the last time through,
                //   and a state save to this instruction in case the following code fails again.
                //   (We're going backwards because this loop emulates stack unwinding, not
                //    the initial scan forward.)
                U_ASSERT(fp->fInputIdx > 0);
                UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
                UChar32 prevC = UTEXT_PREVIOUS32(fInputText);
                fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);

                UChar32 twoPrevC = UTEXT_PREVIOUS32(fInputText);
                if (prevC == 0x0a &&
                    fp->fInputIdx > backSearchIndex &&
                    twoPrevC == 0x0d) {
                    int32_t prevOp = static_cast<int32_t>(pat[fp->fPatIdx - 2]);
                    if (URX_TYPE(prevOp) == URX_LOOP_DOT_I) {
                        // .*, stepping back over CRLF pair.
                        fp->fInputIdx = UTEXT_GETNATIVEINDEX(fInputText);
                    }
                }


                fp = StateSave(fp, fp->fPatIdx-1, status);
            }
            break;



        default:
            // Trouble.  The compiled pattern contains an entry with an
            //           unrecognized type tag.
            UPRV_UNREACHABLE_ASSERT;
            // Unknown opcode type in opType = URX_TYPE(pat[fp->fPatIdx]). But we have
            // reports of this in production code, don't use UPRV_UNREACHABLE_EXIT.
            // See ICU-21669.
            status = U_INTERNAL_PROGRAM_ERROR;
        }

        if (U_FAILURE(status)) {
            isMatch = false;
            break;
        }
    }

breakFromLoop:
    fMatch = isMatch;
    if (isMatch) {
        fLastMatchEnd = fMatchEnd;
        fMatchStart   = startIdx;
        fMatchEnd     = fp->fInputIdx;
    }

#ifdef REGEX_RUN_DEBUG
    if (fTraceDebug) {
        if (isMatch) {
            printf("Match.  start=%ld   end=%ld\n\n", fMatchStart, fMatchEnd);
        } else {
            printf("No match\n\n");
        }
    }
#endif

    fFrame = fp;                // The active stack frame when the engine stopped.
                                //   Contains the capture group results that we need to
                                //    access later.
}


//--------------------------------------------------------------------------------
//
//   MatchChunkAt   This is the actual matching engine. Like MatchAt, but with the
//                  assumption that the entire string is available in the UText's
//                  chunk buffer. For now, that means we can use int32_t indexes,
//                  except for anything that needs to be saved (like group starts
//                  and ends).
//
//                  startIdx:    begin matching a this index.
//                  toEnd:       if true, match must extend to end of the input region
//
//--------------------------------------------------------------------------------
void RegexMatcher::MatchChunkAt(int32_t startIdx, UBool toEnd, UErrorCode &status) {
    UBool       isMatch  = false;      // True if the we have a match.

    int32_t     backSearchIndex = INT32_MAX; // used after greedy single-character matches for searching backwards

    int32_t     op;                    // Operation from the compiled pattern, split into
    int32_t     opType;                //    the opcode
    int32_t     opValue;               //    and the operand value.

#ifdef REGEX_RUN_DEBUG
    if (fTraceDebug) {
        printf("MatchAt(startIdx=%d)\n", startIdx);
        printf("Original Pattern: \"%s\"\n", CStr(StringFromUText(fPattern->fPattern))());
        printf("Input String:     \"%s\"\n\n", CStr(StringFromUText(fInputText))());
    }
#endif

    if (U_FAILURE(status)) {
        return;
    }

    //  Cache frequently referenced items from the compiled pattern
    //
    int64_t             *pat           = fPattern->fCompiledPat->getBuffer();

    const char16_t      *litText       = fPattern->fLiteralText.getBuffer();
    UVector             *fSets         = fPattern->fSets;

    const char16_t      *inputBuf      = fInputText->chunkContents;

    fFrameSize = fPattern->fFrameSize;
    REStackFrame        *fp            = resetStack();
    if (U_FAILURE(fDeferredStatus)) {
        status = fDeferredStatus;
        return;
    }

    fp->fPatIdx   = 0;
    fp->fInputIdx = startIdx;

    // Zero out the pattern's static data
    int32_t i;
    for (i = 0; i<fPattern->fDataSize; i++) {
        fData[i] = 0;
    }

    //
    //  Main loop for interpreting the compiled pattern.
    //  One iteration of the loop per pattern operation performed.
    //
    for (;;) {
        op = static_cast<int32_t>(pat[fp->fPatIdx]);
        opType  = URX_TYPE(op);
        opValue = URX_VAL(op);
#ifdef REGEX_RUN_DEBUG
        if (fTraceDebug) {
            UTEXT_SETNATIVEINDEX(fInputText, fp->fInputIdx);
            printf("inputIdx=%ld   inputChar=%x   sp=%3ld   activeLimit=%ld  ", fp->fInputIdx,
                   UTEXT_CURRENT32(fInputText), (int64_t *)fp-fStack->getBuffer(), fActiveLimit);
            fPattern->dumpOp(fp->fPatIdx);
        }
#endif
        fp->fPatIdx++;

        switch (opType) {


        case URX_NOP:
            break;


        case URX_BACKTRACK:
            // Force a backtrack.  In some circumstances, the pattern compiler
            //   will notice that the pattern can't possibly match anything, and will
            //   emit one of these at that point.
            fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            break;


        case URX_ONECHAR:
            if (fp->fInputIdx < fActiveLimit) {
                UChar32 c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                if (c == opValue) {
                    break;
                }
            } else {
                fHitEnd = true;
            }
            fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            break;


        case URX_STRING:
            {
                // Test input against a literal string.
                // Strings require two slots in the compiled pattern, one for the
                //   offset to the string text, and one for the length.
                int32_t   stringStartIdx = opValue;
                int32_t   stringLen;

                op = static_cast<int32_t>(pat[fp->fPatIdx]); // Fetch the second operand
                fp->fPatIdx++;
                opType    = URX_TYPE(op);
                stringLen = URX_VAL(op);
                U_ASSERT(opType == URX_STRING_LEN);
                U_ASSERT(stringLen >= 2);

                const char16_t * pInp = inputBuf + fp->fInputIdx;
                const char16_t * pInpLimit = inputBuf + fActiveLimit;
                const char16_t * pPat = litText+stringStartIdx;
                const char16_t * pEnd = pInp + stringLen;
                UBool success = true;
                while (pInp < pEnd) {
                    if (pInp >= pInpLimit) {
                        fHitEnd = true;
                        success = false;
                        break;
                    }
                    if (*pInp++ != *pPat++) {
                        success = false;
                        break;
                    }
                }

                if (success) {
                    fp->fInputIdx += stringLen;
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_STATE_SAVE:
            fp = StateSave(fp, opValue, status);
            break;


        case URX_END:
            // The match loop will exit via this path on a successful match,
            //   when we reach the end of the pattern.
            if (toEnd && fp->fInputIdx != fActiveLimit) {
                // The pattern matched, but not to the end of input.  Try some more.
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                break;
            }
            isMatch = true;
            goto  breakFromLoop;

            // Start and End Capture stack frame variables are laid out out like this:
            //  fp->fExtra[opValue]  - The start of a completed capture group
            //             opValue+1 - The end   of a completed capture group
            //             opValue+2 - the start of a capture group whose end
            //                          has not yet been reached (and might not ever be).
        case URX_START_CAPTURE:
            U_ASSERT(opValue >= 0 && opValue < fFrameSize-3);
            fp->fExtra[opValue+2] = fp->fInputIdx;
            break;


        case URX_END_CAPTURE:
            U_ASSERT(opValue >= 0 && opValue < fFrameSize-3);
            U_ASSERT(fp->fExtra[opValue+2] >= 0);            // Start pos for this group must be set.
            fp->fExtra[opValue]   = fp->fExtra[opValue+2];   // Tentative start becomes real.
            fp->fExtra[opValue+1] = fp->fInputIdx;           // End position
            U_ASSERT(fp->fExtra[opValue] <= fp->fExtra[opValue+1]);
            break;


        case URX_DOLLAR:                   //  $, test for End of line
            //     or for position before new line at end of input
            if (fp->fInputIdx < fAnchorLimit-2) {
                // We are no where near the end of input.  Fail.
                //   This is the common case.  Keep it first.
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                break;
            }
            if (fp->fInputIdx >= fAnchorLimit) {
                // We really are at the end of input.  Success.
                fHitEnd = true;
                fRequireEnd = true;
                break;
            }

            // If we are positioned just before a new-line that is located at the
            //   end of input, succeed.
            if (fp->fInputIdx == fAnchorLimit-1) {
                UChar32 c;
                U16_GET(inputBuf, fAnchorStart, fp->fInputIdx, fAnchorLimit, c);

                if (isLineTerminator(c)) {
                    if ( !(c==0x0a && fp->fInputIdx>fAnchorStart && inputBuf[fp->fInputIdx-1]==0x0d)) {
                        // At new-line at end of input. Success
                        fHitEnd = true;
                        fRequireEnd = true;
                        break;
                    }
                }
            } else if (fp->fInputIdx == fAnchorLimit-2 &&
                inputBuf[fp->fInputIdx]==0x0d && inputBuf[fp->fInputIdx+1]==0x0a) {
                    fHitEnd = true;
                    fRequireEnd = true;
                    break;                         // At CR/LF at end of input.  Success
            }

            fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));

            break;


        case URX_DOLLAR_D:                   //  $, test for End of Line, in UNIX_LINES mode.
            if (fp->fInputIdx >= fAnchorLimit-1) {
                // Either at the last character of input, or off the end.
                if (fp->fInputIdx == fAnchorLimit-1) {
                    // At last char of input.  Success if it's a new line.
                    if (inputBuf[fp->fInputIdx] == 0x0a) {
                        fHitEnd = true;
                        fRequireEnd = true;
                        break;
                    }
                } else {
                    // Off the end of input.  Success.
                    fHitEnd = true;
                    fRequireEnd = true;
                    break;
                }
            }

            // Not at end of input.  Back-track out.
            fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            break;


        case URX_DOLLAR_M:                //  $, test for End of line in multi-line mode
            {
                if (fp->fInputIdx >= fAnchorLimit) {
                    // We really are at the end of input.  Success.
                    fHitEnd = true;
                    fRequireEnd = true;
                    break;
                }
                // If we are positioned just before a new-line, succeed.
                // It makes no difference where the new-line is within the input.
                UChar32 c = inputBuf[fp->fInputIdx];
                if (isLineTerminator(c)) {
                    // At a line end, except for the odd chance of  being in the middle of a CR/LF sequence
                    //  In multi-line mode, hitting a new-line just before the end of input does not
                    //   set the hitEnd or requireEnd flags
                    if ( !(c==0x0a && fp->fInputIdx>fAnchorStart && inputBuf[fp->fInputIdx-1]==0x0d)) {
                        break;
                    }
                }
                // not at a new line.  Fail.
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_DOLLAR_MD:                //  $, test for End of line in multi-line and UNIX_LINES mode
            {
                if (fp->fInputIdx >= fAnchorLimit) {
                    // We really are at the end of input.  Success.
                    fHitEnd = true;
                    fRequireEnd = true;  // Java set requireEnd in this case, even though
                    break;               //   adding a new-line would not lose the match.
                }
                // If we are not positioned just before a new-line, the test fails; backtrack out.
                // It makes no difference where the new-line is within the input.
                if (inputBuf[fp->fInputIdx] != 0x0a) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_CARET:                    //  ^, test for start of line
            if (fp->fInputIdx != fAnchorStart) {
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_CARET_M:                   //  ^, test for start of line in mulit-line mode
            {
                if (fp->fInputIdx == fAnchorStart) {
                    // We are at the start input.  Success.
                    break;
                }
                // Check whether character just before the current pos is a new-line
                //   unless we are at the end of input
                char16_t  c = inputBuf[fp->fInputIdx - 1];
                if ((fp->fInputIdx < fAnchorLimit) &&
                    isLineTerminator(c)) {
                    //  It's a new-line.  ^ is true.  Success.
                    //  TODO:  what should be done with positions between a CR and LF?
                    break;
                }
                // Not at the start of a line.  Fail.
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_CARET_M_UNIX:       //  ^, test for start of line in mulit-line + Unix-line mode
            {
                U_ASSERT(fp->fInputIdx >= fAnchorStart);
                if (fp->fInputIdx <= fAnchorStart) {
                    // We are at the start input.  Success.
                    break;
                }
                // Check whether character just before the current pos is a new-line
                U_ASSERT(fp->fInputIdx <= fAnchorLimit);
                char16_t  c = inputBuf[fp->fInputIdx - 1];
                if (c != 0x0a) {
                    // Not at the start of a line.  Back-track out.
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;

        case URX_BACKSLASH_B:          // Test for word boundaries
            {
                UBool success = isChunkWordBoundary(static_cast<int32_t>(fp->fInputIdx));
                success ^= static_cast<UBool>(opValue != 0); // flip sense for \B
                if (!success) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_BU:          // Test for word boundaries, Unicode-style
            {
                UBool success = isUWordBoundary(fp->fInputIdx, status);
                success ^= static_cast<UBool>(opValue != 0); // flip sense for \B
                if (!success) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_D:            // Test for decimal digit
            {
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                UChar32 c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                int8_t ctype = u_charType(c);     // TODO:  make a unicode set for this.  Will be faster.
                UBool success = (ctype == U_DECIMAL_DIGIT_NUMBER);
                success ^= static_cast<UBool>(opValue != 0); // flip sense for \D
                if (!success) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_G:          // Test for position at end of previous match
            if (!((fMatch && fp->fInputIdx==fMatchEnd) || (fMatch==false && fp->fInputIdx==fActiveStart))) {
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_BACKSLASH_H:            // Test for \h, horizontal white space.
            {
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }
                UChar32 c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                int8_t ctype = u_charType(c);
                UBool success = (ctype == U_SPACE_SEPARATOR || c == 9);  // SPACE_SEPARATOR || TAB
                success ^= static_cast<UBool>(opValue != 0);  // flip sense for \H
                if (!success) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_R:            // Test for \R, any line break sequence.
            {
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }
                UChar32 c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                if (isLineTerminator(c)) {
                    if (c == 0x0d && fp->fInputIdx < fActiveLimit) {
                        // Check for CR/LF sequence. Consume both together when found.
                        char16_t c2;
                        U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c2);
                        if (c2 != 0x0a) {
                            U16_PREV(inputBuf, 0, fp->fInputIdx, c2);
                        }
                    }
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_V:         // Any single code point line ending.
            {
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }
                UChar32 c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                UBool success = isLineTerminator(c);
                success ^= static_cast<UBool>(opValue != 0); // flip sense for \V
                if (!success) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_BACKSLASH_X:
            //  Match a Grapheme, as defined by Unicode UAX 29.

            // Fail if at end of input
            if (fp->fInputIdx >= fActiveLimit) {
                fHitEnd = true;
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                break;
            }

            fp->fInputIdx = followingGCBoundary(fp->fInputIdx, status);
            if (fp->fInputIdx >= fActiveLimit) {
                fHitEnd = true;
                fp->fInputIdx = fActiveLimit;
            }
            break;


        case URX_BACKSLASH_Z:          // Test for end of Input
            if (fp->fInputIdx < fAnchorLimit) {
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            } else {
                fHitEnd = true;
                fRequireEnd = true;
            }
            break;



        case URX_STATIC_SETREF:
            {
                // Test input character against one of the predefined sets
                //    (Word Characters, for example)
                // The high bit of the op value is a flag for the match polarity.
                //    0:   success if input char is in set.
                //    1:   success if input char is not in set.
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                UBool success = ((opValue & URX_NEG_SET) == URX_NEG_SET);
                opValue &= ~URX_NEG_SET;
                U_ASSERT(opValue > 0 && opValue < URX_LAST_SET);

                UChar32 c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                if (c < 256) {
                    Regex8BitSet &s8 = RegexStaticSets::gStaticSets->fPropSets8[opValue];
                    if (s8.contains(c)) {
                        success = !success;
                    }
                } else {
                    const UnicodeSet &s = RegexStaticSets::gStaticSets->fPropSets[opValue];
                    if (s.contains(c)) {
                        success = !success;
                    }
                }
                if (!success) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_STAT_SETREF_N:
            {
                // Test input character for NOT being a member of  one of
                //    the predefined sets (Word Characters, for example)
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                U_ASSERT(opValue > 0 && opValue < URX_LAST_SET);

                UChar32  c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                if (c < 256) {
                    Regex8BitSet &s8 = RegexStaticSets::gStaticSets->fPropSets8[opValue];
                    if (s8.contains(c) == false) {
                        break;
                    }
                } else {
                    const UnicodeSet &s = RegexStaticSets::gStaticSets->fPropSets[opValue];
                    if (s.contains(c) == false) {
                        break;
                    }
                }
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_SETREF:
            {
                if (fp->fInputIdx >= fActiveLimit) {
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                U_ASSERT(opValue > 0 && opValue < fSets->size());

                // There is input left.  Pick up one char and test it for set membership.
                UChar32  c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                if (c<256) {
                    Regex8BitSet *s8 = &fPattern->fSets8[opValue];
                    if (s8->contains(c)) {
                        // The character is in the set.  A Match.
                        break;
                    }
                } else {
                    UnicodeSet* s = static_cast<UnicodeSet*>(fSets->elementAt(opValue));
                    if (s->contains(c)) {
                        // The character is in the set.  A Match.
                        break;
                    }
                }

                // the character wasn't in the set.
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_DOTANY:
            {
                // . matches anything, but stops at end-of-line.
                if (fp->fInputIdx >= fActiveLimit) {
                    // At end of input.  Match failed.  Backtrack out.
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                // There is input left.  Advance over one char, unless we've hit end-of-line
                UChar32  c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                if (isLineTerminator(c)) {
                    // End of line in normal mode.   . does not match.
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }
            }
            break;


        case URX_DOTANY_ALL:
            {
                // . in dot-matches-all (including new lines) mode
                if (fp->fInputIdx >= fActiveLimit) {
                    // At end of input.  Match failed.  Backtrack out.
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                // There is input left.  Advance over one char, except if we are
                //   at a cr/lf, advance over both of them.
                UChar32 c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                if (c==0x0d && fp->fInputIdx < fActiveLimit) {
                    // In the case of a CR/LF, we need to advance over both.
                    if (inputBuf[fp->fInputIdx] == 0x0a) {
                        U16_FWD_1(inputBuf, fp->fInputIdx, fActiveLimit);
                    }
                }
            }
            break;


        case URX_DOTANY_UNIX:
            {
                // '.' operator, matches all, but stops at end-of-line.
                //   UNIX_LINES mode, so 0x0a is the only recognized line ending.
                if (fp->fInputIdx >= fActiveLimit) {
                    // At end of input.  Match failed.  Backtrack out.
                    fHitEnd = true;
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                // There is input left.  Advance over one char, unless we've hit end-of-line
                UChar32 c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                if (c == 0x0a) {
                    // End of line in normal mode.   '.' does not match the \n
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;


        case URX_JMP:
            fp->fPatIdx = opValue;
            break;

        case URX_FAIL:
            isMatch = false;
            goto breakFromLoop;

        case URX_JMP_SAV:
            U_ASSERT(opValue < fPattern->fCompiledPat->size());
            fp = StateSave(fp, fp->fPatIdx, status);       // State save to loc following current
            fp->fPatIdx = opValue;                         // Then JMP.
            break;

        case URX_JMP_SAV_X:
            // This opcode is used with (x)+, when x can match a zero length string.
            // Same as JMP_SAV, except conditional on the match having made forward progress.
            // Destination of the JMP must be a URX_STO_INP_LOC, from which we get the
            //   data address of the input position at the start of the loop.
            {
                U_ASSERT(opValue > 0 && opValue < fPattern->fCompiledPat->size());
                int32_t stoOp = static_cast<int32_t>(pat[opValue - 1]);
                U_ASSERT(URX_TYPE(stoOp) == URX_STO_INP_LOC);
                int32_t  frameLoc = URX_VAL(stoOp);
                U_ASSERT(frameLoc >= 0 && frameLoc < fFrameSize);
                int32_t prevInputIdx = static_cast<int32_t>(fp->fExtra[frameLoc]);
                U_ASSERT(prevInputIdx <= fp->fInputIdx);
                if (prevInputIdx < fp->fInputIdx) {
                    // The match did make progress.  Repeat the loop.
                    fp = StateSave(fp, fp->fPatIdx, status);  // State save to loc following current
                    fp->fPatIdx = opValue;
                    fp->fExtra[frameLoc] = fp->fInputIdx;
                }
                // If the input position did not advance, we do nothing here,
                //   execution will fall out of the loop.
            }
            break;

        case URX_CTR_INIT:
            {
                U_ASSERT(opValue >= 0 && opValue < fFrameSize-2);
                fp->fExtra[opValue] = 0;                 //  Set the loop counter variable to zero

                // Pick up the three extra operands that CTR_INIT has, and
                //    skip the pattern location counter past
                int32_t instrOperandLoc = static_cast<int32_t>(fp->fPatIdx);
                fp->fPatIdx += 3;
                int32_t loopLoc  = URX_VAL(pat[instrOperandLoc]);
                int32_t minCount = static_cast<int32_t>(pat[instrOperandLoc + 1]);
                int32_t maxCount = static_cast<int32_t>(pat[instrOperandLoc + 2]);
                U_ASSERT(minCount>=0);
                U_ASSERT(maxCount>=minCount || maxCount==-1);
                U_ASSERT(loopLoc>=fp->fPatIdx);

                if (minCount == 0) {
                    fp = StateSave(fp, loopLoc+1, status);
                }
                if (maxCount == -1) {
                    fp->fExtra[opValue+1] = fp->fInputIdx;   //  For loop breaking.
                } else if (maxCount == 0) {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;

        case URX_CTR_LOOP:
            {
                U_ASSERT(opValue>0 && opValue < fp->fPatIdx-2);
                int32_t initOp = static_cast<int32_t>(pat[opValue]);
                U_ASSERT(URX_TYPE(initOp) == URX_CTR_INIT);
                int64_t *pCounter = &fp->fExtra[URX_VAL(initOp)];
                int32_t minCount = static_cast<int32_t>(pat[opValue + 2]);
                int32_t maxCount = static_cast<int32_t>(pat[opValue + 3]);
                (*pCounter)++;
                if (static_cast<uint64_t>(*pCounter) >= static_cast<uint32_t>(maxCount) && maxCount != -1) {
                    U_ASSERT(*pCounter == maxCount);
                    break;
                }
                if (*pCounter >= minCount) {
                    if (maxCount == -1) {
                        // Loop has no hard upper bound.
                        // Check that it is progressing through the input, break if it is not.
                        int64_t *pLastInputIdx =  &fp->fExtra[URX_VAL(initOp) + 1];
                        if (fp->fInputIdx == *pLastInputIdx) {
                            break;
                        } else {
                            *pLastInputIdx = fp->fInputIdx;
                        }
                    }
                    fp = StateSave(fp, fp->fPatIdx, status);
                } else {
                    // Increment time-out counter. (StateSave() does it if count >= minCount)
                    fTickCounter--;
                    if (fTickCounter <= 0) {
                        IncrementTime(status);    // Re-initializes fTickCounter
                    }
                }
                fp->fPatIdx = opValue + 4;    // Loop back.
            }
            break;

        case URX_CTR_INIT_NG:
            {
                // Initialize a non-greedy loop
                U_ASSERT(opValue >= 0 && opValue < fFrameSize-2);
                fp->fExtra[opValue] = 0;                 //  Set the loop counter variable to zero

                // Pick up the three extra operands that CTR_INIT_NG has, and
                //    skip the pattern location counter past
                int32_t instrOperandLoc = static_cast<int32_t>(fp->fPatIdx);
                fp->fPatIdx += 3;
                int32_t loopLoc  = URX_VAL(pat[instrOperandLoc]);
                int32_t minCount = static_cast<int32_t>(pat[instrOperandLoc + 1]);
                int32_t maxCount = static_cast<int32_t>(pat[instrOperandLoc + 2]);
                U_ASSERT(minCount>=0);
                U_ASSERT(maxCount>=minCount || maxCount==-1);
                U_ASSERT(loopLoc>fp->fPatIdx);
                if (maxCount == -1) {
                    fp->fExtra[opValue+1] = fp->fInputIdx;   //  Save initial input index for loop breaking.
                }

                if (minCount == 0) {
                    if (maxCount != 0) {
                        fp = StateSave(fp, fp->fPatIdx, status);
                    }
                    fp->fPatIdx = loopLoc+1;   // Continue with stuff after repeated block
                }
            }
            break;

        case URX_CTR_LOOP_NG:
            {
                // Non-greedy {min, max} loops
                U_ASSERT(opValue>0 && opValue < fp->fPatIdx-2);
                int32_t initOp = static_cast<int32_t>(pat[opValue]);
                U_ASSERT(URX_TYPE(initOp) == URX_CTR_INIT_NG);
                int64_t *pCounter = &fp->fExtra[URX_VAL(initOp)];
                int32_t minCount = static_cast<int32_t>(pat[opValue + 2]);
                int32_t maxCount = static_cast<int32_t>(pat[opValue + 3]);

                (*pCounter)++;
                if (static_cast<uint64_t>(*pCounter) >= static_cast<uint32_t>(maxCount) && maxCount != -1) {
                    // The loop has matched the maximum permitted number of times.
                    //   Break out of here with no action.  Matching will
                    //   continue with the following pattern.
                    U_ASSERT(*pCounter == maxCount);
                    break;
                }

                if (*pCounter < minCount) {
                    // We haven't met the minimum number of matches yet.
                    //   Loop back for another one.
                    fp->fPatIdx = opValue + 4;    // Loop back.
                    fTickCounter--;
                    if (fTickCounter <= 0) {
                        IncrementTime(status);    // Re-initializes fTickCounter
                    }
                } else {
                    // We do have the minimum number of matches.

                    // If there is no upper bound on the loop iterations, check that the input index
                    // is progressing, and stop the loop if it is not.
                    if (maxCount == -1) {
                        int64_t *pLastInputIdx =  &fp->fExtra[URX_VAL(initOp) + 1];
                        if (fp->fInputIdx == *pLastInputIdx) {
                            break;
                        }
                        *pLastInputIdx = fp->fInputIdx;
                    }

                    // Loop Continuation: we will fall into the pattern following the loop
                    //   (non-greedy, don't execute loop body first), but first do
                    //   a state save to the top of the loop, so that a match failure
                    //   in the following pattern will try another iteration of the loop.
                    fp = StateSave(fp, opValue + 4, status);
                }
            }
            break;

        case URX_STO_SP:
            U_ASSERT(opValue >= 0 && opValue < fPattern->fDataSize);
            fData[opValue] = fStack->size();
            break;

        case URX_LD_SP:
            {
                U_ASSERT(opValue >= 0 && opValue < fPattern->fDataSize);
                int32_t newStackSize = static_cast<int32_t>(fData[opValue]);
                U_ASSERT(newStackSize <= fStack->size());
                int64_t *newFP = fStack->getBuffer() + newStackSize - fFrameSize;
                if (newFP == reinterpret_cast<int64_t*>(fp)) {
                    break;
                }
                int32_t j;
                for (j=0; j<fFrameSize; j++) {
                    newFP[j] = reinterpret_cast<int64_t*>(fp)[j];
                }
                fp = reinterpret_cast<REStackFrame*>(newFP);
                fStack->setSize(newStackSize);
            }
            break;

        case URX_BACKREF:
            {
                U_ASSERT(opValue < fFrameSize);
                int64_t groupStartIdx = fp->fExtra[opValue];
                int64_t groupEndIdx   = fp->fExtra[opValue+1];
                U_ASSERT(groupStartIdx <= groupEndIdx);
                int64_t inputIndex = fp->fInputIdx;
                if (groupStartIdx < 0) {
                    // This capture group has not participated in the match thus far,
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize)); // FAIL, no match.
                    break;
                }
                UBool success = true;
                for (int64_t groupIndex = groupStartIdx; groupIndex < groupEndIdx; ++groupIndex,++inputIndex) {
                    if (inputIndex >= fActiveLimit) {
                        success = false;
                        fHitEnd = true;
                        break;
                    }
                    if (inputBuf[groupIndex] != inputBuf[inputIndex]) {
                        success = false;
                        break;
                    }
                }
                if (success && groupStartIdx < groupEndIdx && U16_IS_LEAD(inputBuf[groupEndIdx-1]) &&
                        inputIndex < fActiveLimit && U16_IS_TRAIL(inputBuf[inputIndex])) {
                    // Capture group ended with an unpaired lead surrogate.
                    // Back reference is not permitted to match lead only of a surrogatge pair.
                    success = false;
                }
                if (success) {
                    fp->fInputIdx = inputIndex;
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;

        case URX_BACKREF_I:
            {
                U_ASSERT(opValue < fFrameSize);
                int64_t groupStartIdx = fp->fExtra[opValue];
                int64_t groupEndIdx   = fp->fExtra[opValue+1];
                U_ASSERT(groupStartIdx <= groupEndIdx);
                if (groupStartIdx < 0) {
                    // This capture group has not participated in the match thus far,
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize)); // FAIL, no match.
                    break;
                }
                CaseFoldingUCharIterator captureGroupItr(inputBuf, groupStartIdx, groupEndIdx);
                CaseFoldingUCharIterator inputItr(inputBuf, fp->fInputIdx, fActiveLimit);

                //   Note: if the capture group match was of an empty string the backref
                //         match succeeds.  Verified by testing:  Perl matches succeed
                //         in this case, so we do too.

                UBool success = true;
                for (;;) {
                    UChar32 captureGroupChar = captureGroupItr.next();
                    if (captureGroupChar == U_SENTINEL) {
                        success = true;
                        break;
                    }
                    UChar32 inputChar = inputItr.next();
                    if (inputChar == U_SENTINEL) {
                        success = false;
                        fHitEnd = true;
                        break;
                    }
                    if (inputChar != captureGroupChar) {
                        success = false;
                        break;
                    }
                }

                if (success && inputItr.inExpansion()) {
                    // We obtained a match by consuming part of a string obtained from
                    // case-folding a single code point of the input text.
                    // This does not count as an overall match.
                    success = false;
                }

                if (success) {
                    fp->fInputIdx = inputItr.getIndex();
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;

        case URX_STO_INP_LOC:
            {
                U_ASSERT(opValue >= 0 && opValue < fFrameSize);
                fp->fExtra[opValue] = fp->fInputIdx;
            }
            break;

        case URX_JMPX:
            {
                int32_t instrOperandLoc = static_cast<int32_t>(fp->fPatIdx);
                fp->fPatIdx += 1;
                int32_t dataLoc  = URX_VAL(pat[instrOperandLoc]);
                U_ASSERT(dataLoc >= 0 && dataLoc < fFrameSize);
                int32_t savedInputIdx = static_cast<int32_t>(fp->fExtra[dataLoc]);
                U_ASSERT(savedInputIdx <= fp->fInputIdx);
                if (savedInputIdx < fp->fInputIdx) {
                    fp->fPatIdx = opValue;                               // JMP
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize)); // FAIL, no progress in loop.
                }
            }
            break;

        case URX_LA_START:
            {
                // Entering a look around block.
                // Save Stack Ptr, Input Pos.
                U_ASSERT(opValue>=0 && opValue+3<fPattern->fDataSize);
                fData[opValue]   = fStack->size();
                fData[opValue+1] = fp->fInputIdx;
                fData[opValue+2] = fActiveStart;
                fData[opValue+3] = fActiveLimit;
                fActiveStart     = fLookStart;          // Set the match region change for
                fActiveLimit     = fLookLimit;          //   transparent bounds.
            }
            break;

        case URX_LA_END:
            {
                // Leaving a look around block.
                //  restore Stack Ptr, Input Pos to positions they had on entry to block.
                U_ASSERT(opValue>=0 && opValue+3<fPattern->fDataSize);
                int32_t stackSize = fStack->size();
                int32_t newStackSize = static_cast<int32_t>(fData[opValue]);
                U_ASSERT(stackSize >= newStackSize);
                if (stackSize > newStackSize) {
                    // Copy the current top frame back to the new (cut back) top frame.
                    //   This makes the capture groups from within the look-ahead
                    //   expression available.
                    int64_t *newFP = fStack->getBuffer() + newStackSize - fFrameSize;
                    int32_t j;
                    for (j=0; j<fFrameSize; j++) {
                        newFP[j] = reinterpret_cast<int64_t*>(fp)[j];
                    }
                    fp = reinterpret_cast<REStackFrame*>(newFP);
                    fStack->setSize(newStackSize);
                }
                fp->fInputIdx = fData[opValue+1];

                // Restore the active region bounds in the input string; they may have
                //    been changed because of transparent bounds on a Region.
                fActiveStart = fData[opValue+2];
                fActiveLimit = fData[opValue+3];
                U_ASSERT(fActiveStart >= 0);
                U_ASSERT(fActiveLimit <= fInputLength);
            }
            break;

        case URX_ONECHAR_I:
            if (fp->fInputIdx < fActiveLimit) {
                UChar32 c;
                U16_NEXT(inputBuf, fp->fInputIdx, fActiveLimit, c);
                if (u_foldCase(c, U_FOLD_CASE_DEFAULT) == opValue) {
                    break;
                }
            } else {
                fHitEnd = true;
            }
            fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            break;

        case URX_STRING_I:
            // Case-insensitive test input against a literal string.
            // Strings require two slots in the compiled pattern, one for the
            //   offset to the string text, and one for the length.
            //   The compiled string has already been case folded.
            {
                const char16_t *patternString = litText + opValue;

                op = static_cast<int32_t>(pat[fp->fPatIdx]);
                fp->fPatIdx++;
                opType  = URX_TYPE(op);
                opValue = URX_VAL(op);
                U_ASSERT(opType == URX_STRING_LEN);
                int32_t patternStringLen = opValue;  // Length of the string from the pattern.

                UChar32      cText;
                UChar32      cPattern;
                UBool        success = true;
                int32_t      patternStringIdx  = 0;
                CaseFoldingUCharIterator inputIterator(inputBuf, fp->fInputIdx, fActiveLimit);
                while (patternStringIdx < patternStringLen) {
                    U16_NEXT(patternString, patternStringIdx, patternStringLen, cPattern);
                    cText = inputIterator.next();
                    if (cText != cPattern) {
                        success = false;
                        if (cText == U_SENTINEL) {
                            fHitEnd = true;
                        }
                        break;
                    }
                }
                if (inputIterator.inExpansion()) {
                    success = false;
                }

                if (success) {
                    fp->fInputIdx = inputIterator.getIndex();
                } else {
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                }
            }
            break;

        case URX_LB_START:
            {
                // Entering a look-behind block.
                // Save Stack Ptr, Input Pos and active input region.
                //   TODO:  implement transparent bounds.  Ticket #6067
                U_ASSERT(opValue>=0 && opValue+4<fPattern->fDataSize);
                fData[opValue]   = fStack->size();
                fData[opValue+1] = fp->fInputIdx;
                // Save input string length, then reset to pin any matches to end at
                //   the current position.
                fData[opValue+2] = fActiveStart;
                fData[opValue+3] = fActiveLimit;
                fActiveStart     = fRegionStart;
                fActiveLimit     = fp->fInputIdx;
                // Init the variable containing the start index for attempted matches.
                fData[opValue+4] = -1;
            }
            break;


        case URX_LB_CONT:
            {
                // Positive Look-Behind, at top of loop checking for matches of LB expression
                //    at all possible input starting positions.

                // Fetch the min and max possible match lengths.  They are the operands
                //   of this op in the pattern.
                int32_t minML = static_cast<int32_t>(pat[fp->fPatIdx++]);
                int32_t maxML = static_cast<int32_t>(pat[fp->fPatIdx++]);
                U_ASSERT(minML <= maxML);
                U_ASSERT(minML >= 0);

                // Fetch (from data) the last input index where a match was attempted.
                U_ASSERT(opValue>=0 && opValue+4<fPattern->fDataSize);
                int64_t  &lbStartIdx = fData[opValue+4];
                if (lbStartIdx < 0) {
                    // First time through loop.
                    lbStartIdx = fp->fInputIdx - minML;
                    if (lbStartIdx > 0 && lbStartIdx < fInputLength) {
                        U16_SET_CP_START(inputBuf, 0, lbStartIdx);
                    }
                } else {
                    // 2nd through nth time through the loop.
                    // Back up start position for match by one.
                    if (lbStartIdx == 0) {
                        lbStartIdx--;
                    } else {
                        U16_BACK_1(inputBuf, 0, lbStartIdx);
                    }
                }

                if (lbStartIdx < 0 || lbStartIdx < fp->fInputIdx - maxML) {
                    // We have tried all potential match starting points without
                    //  getting a match.  Backtrack out, and out of the
                    //   Look Behind altogether.
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    fActiveStart = fData[opValue+2];
                    fActiveLimit = fData[opValue+3];
                    U_ASSERT(fActiveStart >= 0);
                    U_ASSERT(fActiveLimit <= fInputLength);
                    break;
                }

                //    Save state to this URX_LB_CONT op, so failure to match will repeat the loop.
                //      (successful match will fall off the end of the loop.)
                fp = StateSave(fp, fp->fPatIdx-3, status);
                fp->fInputIdx =  lbStartIdx;
            }
            break;

        case URX_LB_END:
            // End of a look-behind block, after a successful match.
            {
                U_ASSERT(opValue>=0 && opValue+4<fPattern->fDataSize);
                if (fp->fInputIdx != fActiveLimit) {
                    //  The look-behind expression matched, but the match did not
                    //    extend all the way to the point that we are looking behind from.
                    //  FAIL out of here, which will take us back to the LB_CONT, which
                    //     will retry the match starting at another position or fail
                    //     the look-behind altogether, whichever is appropriate.
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                // Look-behind match is good.  Restore the original input string region,
                //   which had been truncated to pin the end of the lookbehind match to the
                //   position being looked-behind.
                fActiveStart = fData[opValue+2];
                fActiveLimit = fData[opValue+3];
                U_ASSERT(fActiveStart >= 0);
                U_ASSERT(fActiveLimit <= fInputLength);
            }
            break;


        case URX_LBN_CONT:
            {
                // Negative Look-Behind, at top of loop checking for matches of LB expression
                //    at all possible input starting positions.

                // Fetch the extra parameters of this op.
                int32_t minML = static_cast<int32_t>(pat[fp->fPatIdx++]);
                int32_t maxML = static_cast<int32_t>(pat[fp->fPatIdx++]);
                int32_t continueLoc = static_cast<int32_t>(pat[fp->fPatIdx++]);
                continueLoc = URX_VAL(continueLoc);
                U_ASSERT(minML <= maxML);
                U_ASSERT(minML >= 0);
                U_ASSERT(continueLoc > fp->fPatIdx);

                // Fetch (from data) the last input index where a match was attempted.
                U_ASSERT(opValue>=0 && opValue+4<fPattern->fDataSize);
                int64_t  &lbStartIdx = fData[opValue+4];
                if (lbStartIdx < 0) {
                    // First time through loop.
                    lbStartIdx = fp->fInputIdx - minML;
                    if (lbStartIdx > 0 && lbStartIdx < fInputLength) {
                        U16_SET_CP_START(inputBuf, 0, lbStartIdx);
                    }
                } else {
                    // 2nd through nth time through the loop.
                    // Back up start position for match by one.
                    if (lbStartIdx == 0) {
                        lbStartIdx--;   // Because U16_BACK is unsafe starting at 0.
                    } else {
                        U16_BACK_1(inputBuf, 0, lbStartIdx);
                    }
                }

                if (lbStartIdx < 0 || lbStartIdx < fp->fInputIdx - maxML) {
                    // We have tried all potential match starting points without
                    //  getting a match, which means that the negative lookbehind as
                    //  a whole has succeeded.  Jump forward to the continue location
                    fActiveStart = fData[opValue+2];
                    fActiveLimit = fData[opValue+3];
                    U_ASSERT(fActiveStart >= 0);
                    U_ASSERT(fActiveLimit <= fInputLength);
                    fp->fPatIdx = continueLoc;
                    break;
                }

                //    Save state to this URX_LB_CONT op, so failure to match will repeat the loop.
                //      (successful match will cause a FAIL out of the loop altogether.)
                fp = StateSave(fp, fp->fPatIdx-4, status);
                fp->fInputIdx =  lbStartIdx;
            }
            break;

        case URX_LBN_END:
            // End of a negative look-behind block, after a successful match.
            {
                U_ASSERT(opValue>=0 && opValue+4<fPattern->fDataSize);
                if (fp->fInputIdx != fActiveLimit) {
                    //  The look-behind expression matched, but the match did not
                    //    extend all the way to the point that we are looking behind from.
                    //  FAIL out of here, which will take us back to the LB_CONT, which
                    //     will retry the match starting at another position or succeed
                    //     the look-behind altogether, whichever is appropriate.
                    fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
                    break;
                }

                // Look-behind expression matched, which means look-behind test as
                //   a whole Fails

                //   Restore the original input string length, which had been truncated
                //   inorder to pin the end of the lookbehind match
                //   to the position being looked-behind.
                fActiveStart = fData[opValue+2];
                fActiveLimit = fData[opValue+3];
                U_ASSERT(fActiveStart >= 0);
                U_ASSERT(fActiveLimit <= fInputLength);

                // Restore original stack position, discarding any state saved
                //   by the successful pattern match.
                U_ASSERT(opValue>=0 && opValue+1<fPattern->fDataSize);
                int32_t newStackSize = static_cast<int32_t>(fData[opValue]);
                U_ASSERT(fStack->size() > newStackSize);
                fStack->setSize(newStackSize);

                //  FAIL, which will take control back to someplace
                //  prior to entering the look-behind test.
                fp = reinterpret_cast<REStackFrame*>(fStack->popFrame(fFrameSize));
            }
            break;


        case URX_LOOP_SR_I:
            // Loop Initialization for the optimized implementation of
            //     [some character set]*
            //   This op scans through all matching input.
            //   The following LOOP_C op emulates stack unwinding if the following pattern fails.
            {
                U_ASSERT(opValue > 0 && opValue < fSets->size());
                Regex8BitSet *s8 = &fPattern->fSets8[opValue];
                UnicodeSet* s = static_cast<UnicodeSet*>(fSets->elementAt(opValue));

                // Loop through input, until either the input is exhausted or
                //   we reach a character that is not a member of the set.
                int32_t ix = static_cast<int32_t>(fp->fInputIdx);
                for (;;) {
                    if (ix >= fActiveLimit) {
                        fHitEnd = true;
                        break;
                    }
                    UChar32   c;
                    U16_NEXT(inputBuf, ix, fActiveLimit, c);
                    if (c<256) {
                        if (s8->contains(c) == false) {
                            U16_BACK_1(inputBuf, 0, ix);
                            break;
                        }
                    } else {
                        if (s->contains(c) == false) {
                            U16_BACK_1(inputBuf, 0, ix);
                            break;
                        }
                    }
                }

                // If there were no matching characters, skip over the loop altogether.
                //   The loop doesn't run at all, a * op always succeeds.
                if (ix == fp->fInputIdx) {
                    fp->fPatIdx++;   // skip the URX_LOOP_C op.
                    break;
                }

                // Peek ahead in the compiled pattern, to the URX_LOOP_C that
                //   must follow.  It's operand is the stack location
                //   that holds the starting input index for the match of this [set]*
                int32_t loopcOp = static_cast<int32_t>(pat[fp->fPatIdx]);
                U_ASSERT(URX_TYPE(loopcOp) == URX_LOOP_C);
                int32_t stackLoc = URX_VAL(loopcOp);
                U_ASSERT(stackLoc >= 0 && stackLoc < fFrameSize);
                fp->fExtra[stackLoc] = fp->fInputIdx;
                fp->fInputIdx = ix;

                // Save State to the URX_LOOP_C op that follows this one,
                //   so that match failures in the following code will return to there.
                //   Then bump the pattern idx so the LOOP_C is skipped on the way out of here.
                fp = StateSave(fp, fp->fPatIdx, status);
                fp->fPatIdx++;
            }
            break;


        case URX_LOOP_DOT_I:
            // Loop Initialization for the optimized implementation of .*
            //   This op scans through all remaining input.
            //   The following LOOP_C op emulates stack unwinding if the following pattern fails.
            {
                // Loop through input until the input is exhausted (we reach an end-of-line)
                // In DOTALL mode, we can just go straight to the end of the input.
                int32_t ix;
                if ((opValue & 1) == 1) {
                    // Dot-matches-All mode.  Jump straight to the end of the string.
                    ix = static_cast<int32_t>(fActiveLimit);
                    fHitEnd = true;
                } else {
                    // NOT DOT ALL mode.  Line endings do not match '.'
                    // Scan forward until a line ending or end of input.
                    ix = static_cast<int32_t>(fp->fInputIdx);
                    for (;;) {
                        if (ix >= fActiveLimit) {
                            fHitEnd = true;
                            break;
                        }
                        UChar32   c;
                        U16_NEXT(inputBuf, ix, fActiveLimit, c);   // c = inputBuf[ix++]
                        if ((c & 0x7f) <= 0x29) {          // Fast filter of non-new-line-s
                            if ((c == 0x0a) ||             //  0x0a is newline in both modes.
                                (((opValue & 2) == 0) &&    // IF not UNIX_LINES mode
                                   isLineTerminator(c))) {
                                //  char is a line ending.  Put the input pos back to the
                                //    line ending char, and exit the scanning loop.
                                U16_BACK_1(inputBuf, 0, ix);
                                break;
                            }
                        }
                    }
                }

                // If there were no matching characters, skip over the loop altogether.
                //   The loop doesn't run at all, a * op always succeeds.
                if (ix == fp->fInputIdx) {
                    fp->fPatIdx++;   // skip the URX_LOOP_C op.
                    break;
                }

                // Peek ahead in the compiled pattern, to the URX_LOOP_C that
                //   must follow.  It's operand is the stack location
                //   that holds the starting input index for the match of this .*
                int32_t loopcOp = static_cast<int32_t>(pat[fp->fPatIdx]);
                U_ASSERT(URX_TYPE(loopcOp) == URX_LOOP_C);
                int32_t stackLoc = URX_VAL(loopcOp);
                U_ASSERT(stackLoc >= 0 && stackLoc < fFrameSize);
                fp->fExtra[stackLoc] = fp->fInputIdx;
                fp->fInputIdx = ix;

                // Save State to the URX_LOOP_C op that follows this one,
                //   so that match failures in the following code will return to there.
                //   Then bump the pattern idx so the LOOP_C is skipped on the way out of here.
                fp = StateSave(fp, fp->fPatIdx, status);
                fp->fPatIdx++;
            }
            break;


        case URX_LOOP_C:
            {
                U_ASSERT(opValue>=0 && opValue<fFrameSize);
                backSearchIndex = static_cast<int32_t>(fp->fExtra[opValue]);
                U_ASSERT(backSearchIndex <= fp->fInputIdx);
                if (backSearchIndex == fp->fInputIdx) {
                    // We've backed up the input idx to the point that the loop started.
                    // The loop is done.  Leave here without saving state.
                    //  Subsequent failures won't come back here.
                    break;
                }
                // Set up for the next iteration of the loop, with input index
                //   backed up by one from the last time through,
                //   and a state save to this instruction in case the following code fails again.
                //   (We're going backwards because this loop emulates stack unwinding, not
                //    the initial scan forward.)
                U_ASSERT(fp->fInputIdx > 0);
                UChar32 prevC;
                U16_PREV(inputBuf, 0, fp->fInputIdx, prevC); // !!!: should this 0 be one of f*Limit?

                if (prevC == 0x0a &&
                    fp->fInputIdx > backSearchIndex &&
                    inputBuf[fp->fInputIdx-1] == 0x0d) {
                    int32_t prevOp = static_cast<int32_t>(pat[fp->fPatIdx - 2]);
                    if (URX_TYPE(prevOp) == URX_LOOP_DOT_I) {
                        // .*, stepping back over CRLF pair.
                        U16_BACK_1(inputBuf, 0, fp->fInputIdx);
                    }
                }


                fp = StateSave(fp, fp->fPatIdx-1, status);
            }
            break;



        default:
            // Trouble.  The compiled pattern contains an entry with an
            //           unrecognized type tag.
            UPRV_UNREACHABLE_ASSERT;
            // Unknown opcode type in opType = URX_TYPE(pat[fp->fPatIdx]). But we have
            // reports of this in production code, don't use UPRV_UNREACHABLE_EXIT.
            // See ICU-21669.
            status = U_INTERNAL_PROGRAM_ERROR;
        }

        if (U_FAILURE(status)) {
            isMatch = false;
            break;
        }
    }

breakFromLoop:
    fMatch = isMatch;
    if (isMatch) {
        fLastMatchEnd = fMatchEnd;
        fMatchStart   = startIdx;
        fMatchEnd     = fp->fInputIdx;
    }

#ifdef REGEX_RUN_DEBUG
    if (fTraceDebug) {
        if (isMatch) {
            printf("Match.  start=%ld   end=%ld\n\n", fMatchStart, fMatchEnd);
        } else {
            printf("No match\n\n");
        }
    }
#endif

    fFrame = fp;                // The active stack frame when the engine stopped.
                                //   Contains the capture group results that we need to
                                //    access later.
}


UOBJECT_DEFINE_RTTI_IMPLEMENTATION(RegexMatcher)

U_NAMESPACE_END

#endif  // !UCONFIG_NO_REGULAR_EXPRESSIONS

