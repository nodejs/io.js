// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 2001-2011, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   06/06/01    aliu        Creation.
**********************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/unifilt.h"
#include "unicode/uchar.h"
#include "unicode/utf16.h"
#include "uni2name.h"
#include "cstring.h"
#include "cmemory.h"
#include "uprops.h"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(UnicodeNameTransliterator)

static const char16_t OPEN_DELIM[] = {92,78,123,0}; // "\N{"
static const char16_t CLOSE_DELIM  = 125; // "}"
#define OPEN_DELIM_LEN 3

/**
 * Constructs a transliterator.
 */
UnicodeNameTransliterator::UnicodeNameTransliterator(UnicodeFilter* adoptedFilter) :
    Transliterator(UNICODE_STRING("Any-Name", 8), adoptedFilter) {
}

/**
 * Destructor.
 */
UnicodeNameTransliterator::~UnicodeNameTransliterator() {}

/**
 * Copy constructor.
 */
UnicodeNameTransliterator::UnicodeNameTransliterator(const UnicodeNameTransliterator& o) :
    Transliterator(o) {}

/**
 * Assignment operator.
 */
/*UnicodeNameTransliterator& UnicodeNameTransliterator::operator=(
                             const UnicodeNameTransliterator& o) {
    Transliterator::operator=(o);
    return *this;
}*/

/**
 * Transliterator API.
 */
UnicodeNameTransliterator* UnicodeNameTransliterator::clone() const {
    return new UnicodeNameTransliterator(*this);
}

/**
 * Implements {@link Transliterator#handleTransliterate}.
 * Ignore isIncremental since we don't need the context, and
 * we work on codepoints.
 */
void UnicodeNameTransliterator::handleTransliterate(Replaceable& text, UTransPosition& offsets,
                                                    UBool /*isIncremental*/) const {
    // The failure mode, here and below, is to behave like Any-Null,
    // if either there is no name data (max len == 0) or there is no
    // memory (malloc() => nullptr).

    int32_t maxLen = uprv_getMaxCharNameLength();
    if (maxLen == 0) {
        offsets.start = offsets.limit;
        return;
    }

    // Accommodate the longest possible name plus padding
    char* buf = static_cast<char*>(uprv_malloc(maxLen));
    if (buf == nullptr) {
        offsets.start = offsets.limit;
        return;
    }
    
    int32_t cursor = offsets.start;
    int32_t limit = offsets.limit;

    UnicodeString str(false, OPEN_DELIM, OPEN_DELIM_LEN);
    UErrorCode status;
    int32_t len;

    while (cursor < limit) {
        UChar32 c = text.char32At(cursor);
        int32_t clen = U16_LENGTH(c);
        status = U_ZERO_ERROR;
        if ((len = u_charName(c, U_EXTENDED_CHAR_NAME, buf, maxLen, &status)) >0 && !U_FAILURE(status)) {
            str.truncate(OPEN_DELIM_LEN);
            str.append(UnicodeString(buf, len, US_INV)).append(CLOSE_DELIM);
            text.handleReplaceBetween(cursor, cursor+clen, str);
            len += OPEN_DELIM_LEN + 1; // adjust for delimiters
            cursor += len; // advance cursor and adjust for new text
            limit += len-clen; // change in length
        } else {
            cursor += clen;
        }
    }

    offsets.contextLimit += limit - offsets.limit;
    offsets.limit = limit;
    offsets.start = cursor;

    uprv_free(buf);
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */
