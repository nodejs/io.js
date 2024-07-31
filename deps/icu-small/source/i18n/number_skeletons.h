// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __SOURCE_NUMBER_SKELETONS_H__
#define __SOURCE_NUMBER_SKELETONS_H__

#include "number_types.h"
#include "numparse_types.h"
#include "unicode/ucharstrie.h"
#include "string_segment.h"

U_NAMESPACE_BEGIN
namespace number::impl {

// Forward-declaration
struct SeenMacroProps;

// namespace for enums and entrypoint functions
namespace skeleton {

////////////////////////////////////////////////////////////////////////////////////////
// NOTE: For examples of how to add a new stem to the number skeleton parser, see:    //
// https://github.com/unicode-org/icu/commit/a2a7982216b2348070dc71093775ac7195793d73 //
// and                                                                                //
// https://github.com/unicode-org/icu/commit/6fe86f3934a8a5701034f648a8f7c5087e84aa28 //
////////////////////////////////////////////////////////////////////////////////////////

/**
 * While parsing a skeleton, this enum records what type of option we expect to find next.
 */
enum ParseState {

    // Section 0: We expect whitespace or a stem, but not an option:

    STATE_NULL,

    // Section 1: We might accept an option, but it is not required:

    STATE_SCIENTIFIC,
    STATE_FRACTION_PRECISION,
    STATE_PRECISION,

    // Section 2: An option is required:

    STATE_INCREMENT_PRECISION,
    STATE_MEASURE_UNIT,
    STATE_PER_MEASURE_UNIT,
    STATE_IDENTIFIER_UNIT,
    STATE_UNIT_USAGE,
    STATE_CURRENCY_UNIT,
    STATE_INTEGER_WIDTH,
    STATE_NUMBERING_SYSTEM,
    STATE_SCALE,
};

/**
 * All possible stem literals have an entry in the StemEnum. The enum name is the kebab case stem
 * string literal written in upper snake case.
 *
 * @see StemToObject
 * @see #SERIALIZED_STEM_TRIE
 */
enum StemEnum {

    // Section 1: Stems that do not require an option:

    STEM_COMPACT_SHORT,
    STEM_COMPACT_LONG,
    STEM_SCIENTIFIC,
    STEM_ENGINEERING,
    STEM_NOTATION_SIMPLE,
    STEM_BASE_UNIT,
    STEM_PERCENT,
    STEM_PERMILLE,
    STEM_PERCENT_100, // concise-only
    STEM_PRECISION_INTEGER,
    STEM_PRECISION_UNLIMITED,
    STEM_PRECISION_CURRENCY_STANDARD,
    STEM_PRECISION_CURRENCY_CASH,
    STEM_ROUNDING_MODE_CEILING,
    STEM_ROUNDING_MODE_FLOOR,
    STEM_ROUNDING_MODE_DOWN,
    STEM_ROUNDING_MODE_UP,
    STEM_ROUNDING_MODE_HALF_EVEN,
    STEM_ROUNDING_MODE_HALF_ODD,
    STEM_ROUNDING_MODE_HALF_CEILING,
    STEM_ROUNDING_MODE_HALF_FLOOR,
    STEM_ROUNDING_MODE_HALF_DOWN,
    STEM_ROUNDING_MODE_HALF_UP,
    STEM_ROUNDING_MODE_UNNECESSARY,
    STEM_INTEGER_WIDTH_TRUNC,
    STEM_GROUP_OFF,
    STEM_GROUP_MIN2,
    STEM_GROUP_AUTO,
    STEM_GROUP_ON_ALIGNED,
    STEM_GROUP_THOUSANDS,
    STEM_LATIN,
    STEM_UNIT_WIDTH_NARROW,
    STEM_UNIT_WIDTH_SHORT,
    STEM_UNIT_WIDTH_FULL_NAME,
    STEM_UNIT_WIDTH_ISO_CODE,
    STEM_UNIT_WIDTH_FORMAL,
    STEM_UNIT_WIDTH_VARIANT,
    STEM_UNIT_WIDTH_HIDDEN,
    STEM_SIGN_AUTO,
    STEM_SIGN_ALWAYS,
    STEM_SIGN_NEVER,
    STEM_SIGN_ACCOUNTING,
    STEM_SIGN_ACCOUNTING_ALWAYS,
    STEM_SIGN_EXCEPT_ZERO,
    STEM_SIGN_ACCOUNTING_EXCEPT_ZERO,
    STEM_SIGN_NEGATIVE,
    STEM_SIGN_ACCOUNTING_NEGATIVE,
    STEM_DECIMAL_AUTO,
    STEM_DECIMAL_ALWAYS,

    // Section 2: Stems that DO require an option:

    STEM_PRECISION_INCREMENT,
    STEM_MEASURE_UNIT,
    STEM_PER_MEASURE_UNIT,
    STEM_UNIT,
    STEM_UNIT_USAGE,
    STEM_CURRENCY,
    STEM_INTEGER_WIDTH,
    STEM_NUMBERING_SYSTEM,
    STEM_SCALE,
};

/** Default wildcard char, accepted on input and printed in output */
constexpr char16_t kWildcardChar = u'*';

/** Alternative wildcard char, accept on input but not printed in output */
constexpr char16_t kAltWildcardChar = u'+';

/** Checks whether the char is a wildcard on input */
inline bool isWildcardChar(char16_t c) {
    return c == kWildcardChar || c == kAltWildcardChar;
}

/**
 * Creates a NumberFormatter corresponding to the given skeleton string.
 *
 * @param skeletonString
 *            A number skeleton string, possibly not in its shortest form.
 * @return An UnlocalizedNumberFormatter with behavior defined by the given skeleton string.
 */
UnlocalizedNumberFormatter create(
    const UnicodeString& skeletonString, UParseError* perror, UErrorCode& status);

/**
 * Create a skeleton string corresponding to the given NumberFormatter.
 *
 * @param macros
 *            The NumberFormatter options object.
 * @return A skeleton string in normalized form.
 */
UnicodeString generate(const MacroProps& macros, UErrorCode& status);

/**
 * Converts from a skeleton string to a MacroProps. This method contains the primary parse loop.
 *
 * Internal: use the create() endpoint instead of this function.
 */
MacroProps parseSkeleton(const UnicodeString& skeletonString, int32_t& errOffset, UErrorCode& status);

/**
 * Given that the current segment represents a stem, parse it and save the result.
 *
 * @return The next state after parsing this stem, corresponding to what subset of options to expect.
 */
ParseState parseStem(const StringSegment& segment, const UCharsTrie& stemTrie, SeenMacroProps& seen,
                     MacroProps& macros, UErrorCode& status);

/**
 * Given that the current segment represents an option, parse it and save the result.
 *
 * @return The next state after parsing this option, corresponding to what subset of options to
 *         expect next.
 */
ParseState
parseOption(ParseState stem, const StringSegment& segment, MacroProps& macros, UErrorCode& status);

} // namespace skeleton


/**
 * Namespace for utility methods that convert from StemEnum to corresponding objects or enums. This
 * applies to only the "Section 1" stems, those that are well-defined without an option.
 */
namespace stem_to_object {

Notation notation(skeleton::StemEnum stem);

MeasureUnit unit(skeleton::StemEnum stem);

Precision precision(skeleton::StemEnum stem);

UNumberFormatRoundingMode roundingMode(skeleton::StemEnum stem);

UNumberGroupingStrategy groupingStrategy(skeleton::StemEnum stem);

UNumberUnitWidth unitWidth(skeleton::StemEnum stem);

UNumberSignDisplay signDisplay(skeleton::StemEnum stem);

UNumberDecimalSeparatorDisplay decimalSeparatorDisplay(skeleton::StemEnum stem);

} // namespace stem_to_object

/**
 * Namespace for utility methods that convert from enums to stem strings. More complex object conversions
 * take place in the object_to_stem_string namespace.
 */
namespace enum_to_stem_string {

void roundingMode(UNumberFormatRoundingMode value, UnicodeString& sb);

void groupingStrategy(UNumberGroupingStrategy value, UnicodeString& sb);

void unitWidth(UNumberUnitWidth value, UnicodeString& sb);

void signDisplay(UNumberSignDisplay value, UnicodeString& sb);

void decimalSeparatorDisplay(UNumberDecimalSeparatorDisplay value, UnicodeString& sb);

} // namespace enum_to_stem_string

/**
 * Namespace for utility methods for processing stems and options that cannot be interpreted literally.
 */
namespace blueprint_helpers {

/** @return Whether we successfully found and parsed an exponent width option. */
bool parseExponentWidthOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void generateExponentWidthOption(int32_t minExponentDigits, UnicodeString& sb, UErrorCode& status);

/** @return Whether we successfully found and parsed an exponent sign option. */
bool parseExponentSignOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void parseCurrencyOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void generateCurrencyOption(const CurrencyUnit& currency, UnicodeString& sb, UErrorCode& status);

// "measure-unit/" is deprecated in favour of "unit/".
void parseMeasureUnitOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

// "per-measure-unit/" is deprecated in favour of "unit/".
void parseMeasurePerUnitOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

/**
 * Parses unit identifiers like "meter-per-second" and "foot-and-inch", as
 * specified via a "unit/" concise skeleton.
 */
void parseIdentifierUnitOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void parseUnitUsageOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void parseFractionStem(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void generateFractionStem(int32_t minFrac, int32_t maxFrac, UnicodeString& sb, UErrorCode& status);

void parseDigitsStem(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void generateDigitsStem(int32_t minSig, int32_t maxSig, UnicodeString& sb, UErrorCode& status);

void parseScientificStem(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

// Note: no generateScientificStem since this syntax was added later in ICU 67

void parseIntegerStem(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

// Note: no generateIntegerStem since this syntax was added later in ICU 67

/** @return Whether we successfully found and parsed a frac-sig option. */
bool parseFracSigOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

/** @return Whether we successfully found and parsed a trailing zero option. */
bool parseTrailingZeroOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void parseIncrementOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void
generateIncrementOption(uint32_t increment, digits_t incrementMagnitude, int32_t minFrac, UnicodeString& sb, UErrorCode& status);

void parseIntegerWidthOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void generateIntegerWidthOption(int32_t minInt, int32_t maxInt, UnicodeString& sb, UErrorCode& status);

void parseNumberingSystemOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void generateNumberingSystemOption(const NumberingSystem& ns, UnicodeString& sb, UErrorCode& status);

void parseScaleOption(const StringSegment& segment, MacroProps& macros, UErrorCode& status);

void generateScaleOption(int32_t magnitude, const DecNum* arbitrary, UnicodeString& sb,
                              UErrorCode& status);

} // namespace blueprint_helpers

/**
 * Class for utility methods for generating a token corresponding to each macro-prop. Each method
 * returns whether or not a token was written to the string builder.
 *
 * This needs to be a class, not a namespace, so it can be friended.
 */
class GeneratorHelpers {
  public:
    /**
     * Main skeleton generator function. Appends the normalized skeleton for the MacroProps to the given
     * StringBuilder.
     *
     * Internal: use the create() endpoint instead of this function.
     */
    static void generateSkeleton(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

  private:
    static bool notation(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool unit(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool usage(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool precision(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool roundingMode(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool grouping(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool integerWidth(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool symbols(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool unitWidth(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool sign(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool decimal(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

    static bool scale(const MacroProps& macros, UnicodeString& sb, UErrorCode& status);

};

/**
 * Struct for null-checking.
 * In Java, we can just check the object reference. In C++, we need a different method.
 */
struct SeenMacroProps {
    bool notation = false;
    bool unit = false;
    bool perUnit = false;
    bool usage = false;
    bool precision = false;
    bool roundingMode = false;
    bool grouper = false;
    bool padder = false;
    bool integerWidth = false;
    bool symbols = false;
    bool unitWidth = false;
    bool sign = false;
    bool decimal = false;
    bool scale = false;
};

namespace {

#define SKELETON_UCHAR_TO_CHAR(dest, src, start, end, status) (void)(dest); \
UPRV_BLOCK_MACRO_BEGIN { \
    UErrorCode conversionStatus = U_ZERO_ERROR; \
    (dest).appendInvariantChars({false, (src).getBuffer() + (start), (end) - (start)}, conversionStatus); \
    if (conversionStatus == U_INVARIANT_CONVERSION_ERROR) { \
        /* Don't propagate the invariant conversion error; it is a skeleton syntax error */ \
        (status) = U_NUMBER_SKELETON_SYNTAX_ERROR; \
        return; \
    } else if (U_FAILURE(conversionStatus)) { \
        (status) = conversionStatus; \
        return; \
    } \
} UPRV_BLOCK_MACRO_END

} // namespace

} // namespace number::impl
U_NAMESPACE_END

#endif //__SOURCE_NUMBER_SKELETONS_H__
#endif /* #if !UCONFIG_NO_FORMATTING */
