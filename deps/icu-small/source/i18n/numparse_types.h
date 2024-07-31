// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMPARSE_TYPES_H__
#define __NUMPARSE_TYPES_H__

#include "unicode/uobject.h"
#include "number_decimalquantity.h"
#include "string_segment.h"

U_NAMESPACE_BEGIN
namespace numparse::impl {

// Forward-declarations
class ParsedNumber;

typedef int32_t result_flags_t;
typedef int32_t parse_flags_t;

/** Flags for the type result_flags_t */
enum ResultFlags {
    FLAG_NEGATIVE = 0x0001,
    FLAG_PERCENT = 0x0002,
    FLAG_PERMILLE = 0x0004,
    FLAG_HAS_EXPONENT = 0x0008,
    // FLAG_HAS_DEFAULT_CURRENCY = 0x0010, // no longer used
    FLAG_HAS_DECIMAL_SEPARATOR = 0x0020,
    FLAG_NAN = 0x0040,
    FLAG_INFINITY = 0x0080,
    FLAG_FAIL = 0x0100,
};

/** Flags for the type parse_flags_t */
enum ParseFlags {
    PARSE_FLAG_IGNORE_CASE = 0x0001,
    PARSE_FLAG_MONETARY_SEPARATORS = 0x0002,
    PARSE_FLAG_STRICT_SEPARATORS = 0x0004,
    PARSE_FLAG_STRICT_GROUPING_SIZE = 0x0008,
    PARSE_FLAG_INTEGER_ONLY = 0x0010,
    PARSE_FLAG_GROUPING_DISABLED = 0x0020,
    // PARSE_FLAG_FRACTION_GROUPING_ENABLED = 0x0040, // see #10794
    PARSE_FLAG_INCLUDE_UNPAIRED_AFFIXES = 0x0080,
    PARSE_FLAG_USE_FULL_AFFIXES = 0x0100,
    PARSE_FLAG_EXACT_AFFIX = 0x0200,
    PARSE_FLAG_PLUS_SIGN_ALLOWED = 0x0400,
    // PARSE_FLAG_OPTIMIZE = 0x0800, // no longer used
    // PARSE_FLAG_FORCE_BIG_DECIMAL = 0x1000, // not used in ICU4C
    PARSE_FLAG_NO_FOREIGN_CURRENCY = 0x2000,
    PARSE_FLAG_ALLOW_INFINITE_RECURSION = 0x4000,
    PARSE_FLAG_STRICT_IGNORABLES = 0x8000,
};


// TODO: Is this class worthwhile?
template<int32_t stackCapacity>
class CompactUnicodeString {
  public:
    CompactUnicodeString() {
        static_assert(stackCapacity > 0, "cannot have zero space on stack");
        fBuffer[0] = 0;
    }

    CompactUnicodeString(const UnicodeString& text, UErrorCode& status)
            : fBuffer(text.length() + 1, status) {
        if (U_FAILURE(status)) { return; }
        uprv_memcpy(fBuffer.getAlias(), text.getBuffer(), sizeof(char16_t) * text.length());
        fBuffer[text.length()] = 0;
    }

    inline UnicodeString toAliasedUnicodeString() const {
        return UnicodeString(true, fBuffer.getAlias(), -1);
    }

    bool operator==(const CompactUnicodeString& other) const {
        // Use the alias-only constructor and then call UnicodeString operator==
        return toAliasedUnicodeString() == other.toAliasedUnicodeString();
    }

  private:
    MaybeStackArray<char16_t, stackCapacity> fBuffer;
};


/**
 * Struct-like class to hold the results of a parsing routine.
 *
 * @author sffc
 */
// Exported as U_I18N_API for tests
class U_I18N_API ParsedNumber {
  public:

    /**
     * The numerical value that was parsed.
     */
    ::icu::number::impl::DecimalQuantity quantity;

    /**
     * The index of the last char consumed during parsing. If parsing started at index 0, this is equal
     * to the number of chars consumed. This is NOT necessarily the same as the StringSegment offset;
     * "weak" chars, like whitespace, change the offset, but the charsConsumed is not touched until a
     * "strong" char is encountered.
     */
    int32_t charEnd;

    /**
     * Boolean flags (see constants above).
     */
    result_flags_t flags;

    /**
     * The pattern string corresponding to the prefix that got consumed.
     */
    UnicodeString prefix;

    /**
     * The pattern string corresponding to the suffix that got consumed.
     */
    UnicodeString suffix;

    /**
     * The currency that got consumed.
     */
    char16_t currencyCode[4];

    ParsedNumber();

    ParsedNumber(const ParsedNumber& other) = default;

    ParsedNumber& operator=(const ParsedNumber& other) = default;

    void clear();

    /**
     * Call this method to register that a "strong" char was consumed. This should be done after calling
     * {@link StringSegment#setOffset} or {@link StringSegment#adjustOffset} except when the char is
     * "weak", like whitespace.
     *
     * <p>
     * <strong>What is a strong versus weak char?</strong> The behavior of number parsing is to "stop"
     * after reading the number, even if there is other content following the number. For example, after
     * parsing the string "123 " (123 followed by a space), the cursor should be set to 3, not 4, even
     * though there are matchers that accept whitespace. In this example, the digits are strong, whereas
     * the whitespace is weak. Grouping separators are weak, whereas decimal separators are strong. Most
     * other chars are strong.
     *
     * @param segment
     *            The current StringSegment, usually immediately following a call to setOffset.
     */
    void setCharsConsumed(const StringSegment& segment);

    /** Apply certain number-related flags to the DecimalQuantity. */
    void postProcess();

    /**
     * Returns whether this the parse was successful. To be successful, at least one char must have been
     * consumed, and the failure flag must not be set.
     */
    bool success() const;

    bool seenNumber() const;

    double getDouble(UErrorCode& status) const;

    void populateFormattable(Formattable& output, parse_flags_t parseFlags) const;

    bool isBetterThan(const ParsedNumber& other);
};


/**
 * The core interface implemented by all matchers used for number parsing.
 *
 * Given a string, there should NOT be more than one way to consume the string with the same matcher
 * applied multiple times. If there is, the non-greedy parsing algorithm will be unhappy and may enter an
 * exponential-time loop. For example, consider the "A Matcher" that accepts "any number of As". Given
 * the string "AAAA", there are 2^N = 8 ways to apply the A Matcher to this string: you could have the A
 * Matcher apply 4 times to each character; you could have it apply just once to all the characters; you
 * could have it apply to the first 2 characters and the second 2 characters; and so on. A better version
 * of the "A Matcher" would be for it to accept exactly one A, and allow the algorithm to run it
 * repeatedly to consume a string of multiple As. The A Matcher can implement the Flexible interface
 * below to signal that it can be applied multiple times in a row.
 *
 * @author sffc
 */
// Exported as U_I18N_API for tests
class U_I18N_API NumberParseMatcher {
  public:
    virtual ~NumberParseMatcher();

    /**
     * Matchers can override this method to return true to indicate that they are optional and can be run
     * repeatedly. Used by SeriesMatcher, primarily in the context of IgnorablesMatcher.
     */
    virtual bool isFlexible() const {
        return false;
    }

    /**
     * Runs this matcher starting at the beginning of the given StringSegment. If this matcher finds
     * something interesting in the StringSegment, it should update the offset of the StringSegment
     * corresponding to how many chars were matched.
     *
     * This method is thread-safe.
     *
     * @param segment
     *            The StringSegment to match against. Matches always start at the beginning of the
     *            segment. The segment is guaranteed to contain at least one char.
     * @param result
     *            The data structure to store results if the match succeeds.
     * @return Whether this matcher thinks there may be more interesting chars beyond the end of the
     *         string segment.
     */
    virtual bool match(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const = 0;

    /**
     * Performs a fast "smoke check" for whether or not this matcher could possibly match against the
     * given string segment. The test should be as fast as possible but also as restrictive as possible.
     * For example, matchers can maintain a UnicodeSet of all code points that count possibly start a
     * match. Matchers should use the {@link StringSegment#startsWith} method in order to correctly
     * handle case folding.
     *
     * @param segment
     *            The segment to check against.
     * @return true if the matcher might be able to match against this segment; false if it definitely
     *         will not be able to match.
     */
    virtual bool smokeTest(const StringSegment& segment) const = 0;

    /**
     * Method called at the end of a parse, after all matchers have failed to consume any more chars.
     * Allows a matcher to make final modifications to the result given the knowledge that no more
     * matches are possible.
     *
     * @param result
     *            The data structure to store results.
     */
    virtual void postProcess(ParsedNumber&) const {
        // Default implementation: no-op
    }

    // String for debugging
    virtual UnicodeString toString() const = 0;

  protected:
    // No construction except by subclasses!
    NumberParseMatcher() = default;
};


/**
 * Interface for use in arguments.
 */
// Exported as U_I18N_API for tests
class U_I18N_API MutableMatcherCollection {
  public:
    virtual ~MutableMatcherCollection() = default;

    virtual void addMatcher(NumberParseMatcher& matcher) = 0;
};

} // namespace numparse::impl
U_NAMESPACE_END

#endif //__NUMPARSE_TYPES_H__
#endif /* #if !UCONFIG_NO_FORMATTING */
