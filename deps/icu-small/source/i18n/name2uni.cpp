// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 2001-2011, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   06/07/01    aliu        Creation.
**********************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/unifilt.h"
#include "unicode/uchar.h"
#include "unicode/uniset.h"
#include "unicode/utf16.h"
#include "cmemory.h"
#include "name2uni.h"
#include "patternprops.h"
#include "uprops.h"
#include "uinvchar.h"
#include "util.h"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(NameUnicodeTransliterator)

static const char16_t OPEN[] = {92,78,126,123,126,0}; // "\N~{~"
static const char16_t OPEN_DELIM  = 92;  // '\\' first char of OPEN
static const char16_t CLOSE_DELIM = 125; // '}'
static const char16_t SPACE       = 32;  // ' '

U_CDECL_BEGIN

// USetAdder implementation
// Does not use uset.h to reduce code dependencies
static void U_CALLCONV
_set_add(USet *set, UChar32 c) {
    uset_add(set, c);
}

// These functions aren't used.
/*static void U_CALLCONV
_set_addRange(USet *set, UChar32 start, UChar32 end) {
    ((UnicodeSet *)set)->add(start, end);
}

static void U_CALLCONV
_set_addString(USet *set, const char16_t *str, int32_t length) {
    ((UnicodeSet *)set)->add(UnicodeString((UBool)(length<0), str, length));
}*/

U_CDECL_END

/**
 * Constructs a transliterator with the default delimiters '{' and
 * '}'.
 */
NameUnicodeTransliterator::NameUnicodeTransliterator(UnicodeFilter* adoptedFilter) :
    Transliterator(UNICODE_STRING("Name-Any", 8), adoptedFilter) {

    UnicodeSet *legalPtr = &legal;
    // Get the legal character set
    USetAdder sa = {
        reinterpret_cast<USet*>(legalPtr), // USet* == UnicodeSet*
        _set_add,
        nullptr, // Don't need _set_addRange
        nullptr, // Don't need _set_addString
        nullptr, // Don't need remove()
        nullptr
    };
    uprv_getCharNameCharacters(&sa);
}

/**
 * Destructor.
 */
NameUnicodeTransliterator::~NameUnicodeTransliterator() {}

/**
 * Copy constructor.
 */
NameUnicodeTransliterator::NameUnicodeTransliterator(const NameUnicodeTransliterator& o) :
    Transliterator(o), legal(o.legal) {}

/**
 * Assignment operator.
 */
/*NameUnicodeTransliterator& NameUnicodeTransliterator::operator=(
                             const NameUnicodeTransliterator& o) {
    Transliterator::operator=(o);
    // not necessary: the legal sets should all be the same -- legal=o.legal;
    return *this;
}*/

/**
 * Transliterator API.
 */
NameUnicodeTransliterator* NameUnicodeTransliterator::clone() const {
    return new NameUnicodeTransliterator(*this);
}

/**
 * Implements {@link Transliterator#handleTransliterate}.
 */
void NameUnicodeTransliterator::handleTransliterate(Replaceable& text, UTransPosition& offsets,
                                                    UBool isIncremental) const {
    // The failure mode, here and below, is to behave like Any-Null,
    // if either there is no name data (max len == 0) or there is no
    // memory (malloc() => nullptr).

    int32_t maxLen = uprv_getMaxCharNameLength();
    if (maxLen == 0) {
        offsets.start = offsets.limit;
        return;
    }

    // Accommodate the longest possible name
    ++maxLen; // allow for temporary trailing space
    char* cbuf = static_cast<char*>(uprv_malloc(maxLen));
    if (cbuf == nullptr) {
        offsets.start = offsets.limit;
        return;
    }

    UnicodeString openPat(true, OPEN, -1);
    UnicodeString str, name;

    int32_t cursor = offsets.start;
    int32_t limit = offsets.limit;

    // Modes:
    // 0 - looking for open delimiter
    // 1 - after open delimiter
    int32_t mode = 0;
    int32_t openPos = -1; // open delim candidate pos

    UChar32 c;
    while (cursor < limit) {
        c = text.char32At(cursor);

        switch (mode) {
        case 0: // looking for open delimiter
            if (c == OPEN_DELIM) { // quick check first
                openPos = cursor;
                int32_t i =
                    ICU_Utility::parsePattern(openPat, text, cursor, limit);
                if (i >= 0 && i < limit) {
                    mode = 1;
                    name.truncate(0);
                    cursor = i;
                    continue; // *** reprocess char32At(cursor)
                }
            }
            break;

        case 1: // after open delimiter
            // Look for legal chars.  If \s+ is found, convert it
            // to a single space.  If closeDelimiter is found, exit
            // the loop.  If any other character is found, exit the
            // loop.  If the limit is reached, exit the loop.

            // Convert \s+ => SPACE.  This assumes there are no
            // runs of >1 space characters in names.
            if (PatternProps::isWhiteSpace(c)) {
                // Ignore leading whitespace
                if (name.length() > 0 &&
                    name.charAt(name.length()-1) != SPACE) {
                    name.append(SPACE);
                    // If we are too long then abort.  maxLen includes
                    // temporary trailing space, so use '>'.
                    if (name.length() > maxLen) {
                        mode = 0;
                    }
                }
                break;
            }

            if (c == CLOSE_DELIM) {
                int32_t len = name.length();

                // Delete trailing space, if any
                if (len > 0 &&
                    name.charAt(len-1) == SPACE) {
                    --len;
                }

                if (uprv_isInvariantUString(name.getBuffer(), len)) {
                    cbuf[0] = 0;
                    name.extract(0, len, cbuf, maxLen, US_INV);

                    UErrorCode status = U_ZERO_ERROR;
                    c = u_charFromName(U_EXTENDED_CHAR_NAME, cbuf, &status);
                    if (U_SUCCESS(status)) {
                        // Lookup succeeded

                        // assert(U16_LENGTH(CLOSE_DELIM) == 1);
                        cursor++; // advance over CLOSE_DELIM

                        str.truncate(0);
                        str.append(c);
                        text.handleReplaceBetween(openPos, cursor, str);

                        // Adjust indices for the change in the length of
                        // the string.  Do not assume that str.length() ==
                        // 1, in case of surrogates.
                        int32_t delta = cursor - openPos - str.length();
                        cursor -= delta;
                        limit -= delta;
                        // assert(cursor == openPos + str.length());
                    }
                }
                // If the lookup failed, we leave things as-is and
                // still switch to mode 0 and continue.
                mode = 0;
                openPos = -1; // close off candidate
                continue; // *** reprocess char32At(cursor)
            }
            
            // Check if c is a legal char.  We assume here that
            // legal.contains(OPEN_DELIM) is false, so when we abort a
            // name, we don't have to go back to openPos+1.
            if (legal.contains(c)) {
                name.append(c);
                // If we go past the longest possible name then abort.
                // maxLen includes temporary trailing space, so use '>='.
                if (name.length() >= maxLen) {
                    mode = 0;
                }
            }
            
            // Invalid character
            else {
                --cursor; // Backup and reprocess this character
                mode = 0;
            }

            break;
        }

        cursor += U16_LENGTH(c);
    }
        
    offsets.contextLimit += limit - offsets.limit;
    offsets.limit = limit;
    // In incremental mode, only advance the cursor up to the last
    // open delimiter candidate.
    offsets.start = (isIncremental && openPos >= 0) ? openPos : cursor;

    uprv_free(cbuf);
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */
