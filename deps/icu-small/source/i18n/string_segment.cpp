// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

// Allow implicit conversion from char16_t* to UnicodeString for this file:
// Helpful in toString methods and elsewhere.
#define UNISTR_FROM_STRING_EXPLICIT

#include "numparse_types.h"
#include "string_segment.h"
#include "putilimp.h"
#include "unicode/utf16.h"
#include "unicode/uniset.h"

U_NAMESPACE_BEGIN


StringSegment::StringSegment(const UnicodeString& str, bool ignoreCase)
        : fStr(str), fStart(0), fEnd(str.length()),
          fFoldCase(ignoreCase) {}

int32_t StringSegment::getOffset() const {
    return fStart;
}

void StringSegment::setOffset(int32_t start) {
    fStart = start;
}

void StringSegment::adjustOffset(int32_t delta) {
    fStart += delta;
}

void StringSegment::adjustOffsetByCodePoint() {
    fStart += U16_LENGTH(getCodePoint());
}

void StringSegment::setLength(int32_t length) {
    fEnd = fStart + length;
}

void StringSegment::resetLength() {
    fEnd = fStr.length();
}

int32_t StringSegment::length() const {
    return fEnd - fStart;
}

char16_t StringSegment::charAt(int32_t index) const {
    return fStr.charAt(index + fStart);
}

UChar32 StringSegment::codePointAt(int32_t index) const {
    return fStr.char32At(index + fStart);
}

UnicodeString StringSegment::toUnicodeString() const {
    return UnicodeString(fStr.getBuffer() + fStart, fEnd - fStart);
}

UnicodeString StringSegment::toTempUnicodeString() const {
    // Use the readonly-aliasing constructor for efficiency.
    return UnicodeString(false, fStr.getBuffer() + fStart, fEnd - fStart);
}

UChar32 StringSegment::getCodePoint() const {
    char16_t lead = fStr.charAt(fStart);
    if (U16_IS_LEAD(lead) && fStart + 1 < fEnd) {
        return fStr.char32At(fStart);
    } else if (U16_IS_SURROGATE(lead)) {
        return -1;
    } else {
        return lead;
    }
}

bool StringSegment::startsWith(UChar32 otherCp) const {
    return codePointsEqual(getCodePoint(), otherCp, fFoldCase);
}

bool StringSegment::startsWith(const UnicodeSet& uniset) const {
    // TODO: Move UnicodeSet case-folding logic here.
    // TODO: Handle string matches here instead of separately.
    UChar32 cp = getCodePoint();
    if (cp == -1) {
        return false;
    }
    return uniset.contains(cp);
}

bool StringSegment::startsWith(const UnicodeString& other) const {
    if (other.isBogus() || other.length() == 0 || length() == 0) {
        return false;
    }
    int cp1 = getCodePoint();
    int cp2 = other.char32At(0);
    return codePointsEqual(cp1, cp2, fFoldCase);
}

int32_t StringSegment::getCommonPrefixLength(const UnicodeString& other) {
    return getPrefixLengthInternal(other, fFoldCase);
}

int32_t StringSegment::getCaseSensitivePrefixLength(const UnicodeString& other) {
    return getPrefixLengthInternal(other, false);
}

int32_t StringSegment::getPrefixLengthInternal(const UnicodeString& other, bool foldCase) {
    U_ASSERT(other.length() > 0);
    int32_t offset = 0;
    for (; offset < uprv_min(length(), other.length());) {
        // TODO: case-fold code points, not chars
        char16_t c1 = charAt(offset);
        char16_t c2 = other.charAt(offset);
        if (!codePointsEqual(c1, c2, foldCase)) {
            break;
        }
        offset++;
    }
    return offset;
}

bool StringSegment::codePointsEqual(UChar32 cp1, UChar32 cp2, bool foldCase) {
    if (cp1 == cp2) {
        return true;
    }
    if (!foldCase) {
        return false;
    }
    cp1 = u_foldCase(cp1, true);
    cp2 = u_foldCase(cp2, true);
    return cp1 == cp2;
}

bool StringSegment::operator==(const UnicodeString& other) const {
    return toTempUnicodeString() == other;
}


U_NAMESPACE_END
#endif /* #if !UCONFIG_NO_FORMATTING */
