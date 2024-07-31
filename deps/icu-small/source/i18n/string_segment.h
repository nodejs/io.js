// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMPARSE_STRINGSEGMENT_H__
#define __NUMPARSE_STRINGSEGMENT_H__

#include "unicode/unistr.h"
#include "unicode/uniset.h"

U_NAMESPACE_BEGIN


/**
 * A mutable UnicodeString wrapper with a variable offset and length and
 * support for case folding. The charAt, length, and subSequence methods all
 * operate relative to the fixed offset into the UnicodeString.
 *
 * Intended to be useful for parsing.
 *
 * CAUTION: Since this class is mutable, it must not be used anywhere that an
 * immutable object is required, like in a cache or as the key of a hash map.
 *
 * @author sffc (Shane Carr)
 */
// Exported as U_I18N_API for tests
class U_I18N_API StringSegment : public UMemory {
  public:
    StringSegment(const UnicodeString& str, bool ignoreCase);

    int32_t getOffset() const;

    void setOffset(int32_t start);

    /**
     * Equivalent to <code>setOffset(getOffset()+delta)</code>.
     *
     * <p>
     * This method is usually called by a Matcher to register that a char was consumed. If the char is
     * strong (it usually is, except for things like whitespace), follow this with a call to
     * {@link ParsedNumber#setCharsConsumed}. For more information on strong chars, see that method.
     */
    void adjustOffset(int32_t delta);

    /**
     * Adjusts the offset by the width of the current code point, either 1 or 2 chars.
     */
    void adjustOffsetByCodePoint();

    void setLength(int32_t length);

    void resetLength();

    int32_t length() const;

    char16_t charAt(int32_t index) const;

    UChar32 codePointAt(int32_t index) const;

    UnicodeString toUnicodeString() const;

    UnicodeString toTempUnicodeString() const;

    /**
     * Returns the first code point in the string segment, or -1 if the string starts with an invalid
     * code point.
     *
     * <p>
     * <strong>Important:</strong> Most of the time, you should use {@link #startsWith}, which handles case
     * folding logic, instead of this method.
     */
    UChar32 getCodePoint() const;

    /**
     * Returns true if the first code point of this StringSegment equals the given code point.
     *
     * <p>
     * This method will perform case folding if case folding is enabled for the parser.
     */
    bool startsWith(UChar32 otherCp) const;

    /**
     * Returns true if the first code point of this StringSegment is in the given UnicodeSet.
     */
    bool startsWith(const UnicodeSet& uniset) const;

    /**
     * Returns true if there is at least one code point of overlap between this StringSegment and the
     * given UnicodeString.
     */
    bool startsWith(const UnicodeString& other) const;

    /**
     * Returns the length of the prefix shared by this StringSegment and the given UnicodeString. For
     * example, if this string segment is "aab", and the char sequence is "aac", this method returns 2,
     * since the first 2 characters are the same.
     *
     * <p>
     * This method only returns offsets along code point boundaries.
     *
     * <p>
     * This method will perform case folding if case folding was enabled in the constructor.
     *
     * <p>
     * IMPORTANT: The given UnicodeString must not be empty! It is the caller's responsibility to check.
     */
    int32_t getCommonPrefixLength(const UnicodeString& other);

    /**
     * Like {@link #getCommonPrefixLength}, but never performs case folding, even if case folding is
     * enabled for the parser.
     */
    int32_t getCaseSensitivePrefixLength(const UnicodeString& other);

    bool operator==(const UnicodeString& other) const;

  private:
    const UnicodeString& fStr;
    int32_t fStart;
    int32_t fEnd;
    bool fFoldCase;

    int32_t getPrefixLengthInternal(const UnicodeString& other, bool foldCase);

    static bool codePointsEqual(UChar32 cp1, UChar32 cp2, bool foldCase);
};


U_NAMESPACE_END

#endif //__NUMPARSE_STRINGSEGMENT_H__
#endif /* #if !UCONFIG_NO_FORMATTING */
