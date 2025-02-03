// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*
*   Copyright (C) 2007, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
******************************************************************************
*   file name:  unisetspan.h
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2007mar01
*   created by: Markus W. Scherer
*/

#ifndef __UNISETSPAN_H__
#define __UNISETSPAN_H__

#include "unicode/utypes.h"
#include "unicode/uniset.h"

U_NAMESPACE_BEGIN

/*
 * Implement span() etc. for a set with strings.
 * Avoid recursion because of its exponential complexity.
 * Instead, try multiple paths at once and track them with an IndexList.
 */
class UnicodeSetStringSpan : public UMemory {
public:
    /*
     * Which span() variant will be used?
     * The object is either built for one variant and used once,
     * or built for all and may be used many times.
     */
    enum {
        FWD             = 0x20,
        BACK            = 0x10,
        UTF16           = 8,
        UTF8            = 4,
        CONTAINED       = 2,
        NOT_CONTAINED   = 1,

        ALL             = 0x3f,

        FWD_UTF16_CONTAINED     = FWD  | UTF16 |     CONTAINED,
        FWD_UTF16_NOT_CONTAINED = FWD  | UTF16 | NOT_CONTAINED,
        FWD_UTF8_CONTAINED      = FWD  | UTF8  |     CONTAINED,
        FWD_UTF8_NOT_CONTAINED  = FWD  | UTF8  | NOT_CONTAINED,
        BACK_UTF16_CONTAINED    = BACK | UTF16 |     CONTAINED,
        BACK_UTF16_NOT_CONTAINED= BACK | UTF16 | NOT_CONTAINED,
        BACK_UTF8_CONTAINED     = BACK | UTF8  |     CONTAINED,
        BACK_UTF8_NOT_CONTAINED = BACK | UTF8  | NOT_CONTAINED
    };

    UnicodeSetStringSpan(const UnicodeSet &set, const UVector &setStrings, uint32_t which);

    // Copy constructor. Assumes which==ALL for a frozen set.
    UnicodeSetStringSpan(const UnicodeSetStringSpan &otherStringSpan, const UVector &newParentSetStrings);

    ~UnicodeSetStringSpan();

    /*
     * Do the strings need to be checked in span() etc.?
     * @return true if strings need to be checked (call span() here),
     *         false if not (use a BMPSet for best performance).
     */
    inline UBool needsStringSpanUTF16();
    inline UBool needsStringSpanUTF8();

    // For fast UnicodeSet::contains(c).
    inline UBool contains(UChar32 c) const;

    int32_t span(const char16_t *s, int32_t length, USetSpanCondition spanCondition) const;

    int32_t spanBack(const char16_t *s, int32_t length, USetSpanCondition spanCondition) const;

    int32_t spanUTF8(const uint8_t *s, int32_t length, USetSpanCondition spanCondition) const;

    int32_t spanBackUTF8(const uint8_t *s, int32_t length, USetSpanCondition spanCondition) const;

private:
    // Special spanLength byte values.
    enum {
        // The spanLength is >=0xfe.
        LONG_SPAN=0xfe,
        // All code points in the string are contained in the parent set.
        ALL_CP_CONTAINED=0xff
    };

    // Add a starting or ending string character to the spanNotSet
    // so that a character span ends before any string.
    void addToSpanNotSet(UChar32 c);

    int32_t spanNot(const char16_t *s, int32_t length) const;
    int32_t spanNotBack(const char16_t *s, int32_t length) const;
    int32_t spanNotUTF8(const uint8_t *s, int32_t length) const;
    int32_t spanNotBackUTF8(const uint8_t *s, int32_t length) const;

    // Set for span(). Same as parent but without strings.
    UnicodeSet spanSet;

    // Set for span(not contained).
    // Same as spanSet, plus characters that start or end strings.
    UnicodeSet *pSpanNotSet;

    // The strings of the parent set.
    const UVector &strings;

    // Pointer to the UTF-8 string lengths.
    // Also pointer to further allocated storage for meta data and
    // UTF-8 string contents as necessary.
    int32_t *utf8Lengths;

    // Pointer to the part of the (utf8Lengths) memory block that stores
    // the lengths of span(), spanBack() etc. for each string.
    uint8_t *spanLengths;

    // Pointer to the part of the (utf8Lengths) memory block that stores
    // the UTF-8 versions of the parent set's strings.
    uint8_t *utf8;

    // Number of bytes for all UTF-8 versions of strings together.
    int32_t utf8Length;

    // Maximum lengths of relevant strings.
    int32_t maxLength16;
    int32_t maxLength8;

    // Set up for all variants of span()?
    UBool all;

    // Memory for small numbers and lengths of strings.
    // For example, for 8 strings:
    // 8 UTF-8 lengths, 8*4 bytes span lengths, 8*2 3-byte UTF-8 characters
    // = 112 bytes = int32_t[28].
    int32_t staticLengths[32];
};

UBool UnicodeSetStringSpan::needsStringSpanUTF16() {
    return maxLength16 != 0;
}

UBool UnicodeSetStringSpan::needsStringSpanUTF8() {
    return maxLength8 != 0;
}

UBool UnicodeSetStringSpan::contains(UChar32 c) const {
    return spanSet.contains(c);
}

U_NAMESPACE_END

#endif
