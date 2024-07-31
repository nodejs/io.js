// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_TYPES_H__
#define __NUMBER_TYPES_H__

#include <cstdint>
#include "unicode/decimfmt.h"
#include "unicode/unum.h"
#include "unicode/numsys.h"
#include "unicode/numberformatter.h"
#include "unicode/utf16.h"
#include "uassert.h"
#include "unicode/platform.h"
#include "unicode/uniset.h"
#include "standardplural.h"
#include "formatted_string_builder.h"

U_NAMESPACE_BEGIN
namespace number::impl {

// For convenience and historical reasons, import the Field typedef to the namespace.
typedef FormattedStringBuilder::Field Field;

// Typedef several enums for brevity and for easier comparison to Java.

typedef UNumberFormatRoundingMode RoundingMode;

typedef UNumberFormatPadPosition PadPosition;

typedef UNumberCompactStyle CompactStyle;

// ICU4J Equivalent: RoundingUtils.MAX_INT_FRAC_SIG
static constexpr int32_t kMaxIntFracSig = 999;

// ICU4J Equivalent: RoundingUtils.DEFAULT_ROUNDING_MODE
static constexpr RoundingMode kDefaultMode = RoundingMode::UNUM_FOUND_HALFEVEN;

// ICU4J Equivalent: Padder.FALLBACK_PADDING_STRING
static constexpr char16_t kFallbackPaddingString[] = u" ";

// Forward declarations:

class Modifier;
class MutablePatternModifier;
class DecimalQuantity;
class ModifierStore;
struct MicroProps;


enum AffixPatternType {
    // Represents a literal character; the value is stored in the code point field.
            TYPE_CODEPOINT = 0,

    // Represents a minus sign symbol '-'.
            TYPE_MINUS_SIGN = -1,

    // Represents a plus sign symbol '+'.
            TYPE_PLUS_SIGN = -2,

    // Represents an approximately sign symbol '~'.
            TYPE_APPROXIMATELY_SIGN = -3,

    // Represents a percent sign symbol '%'.
            TYPE_PERCENT = -4,

    // Represents a permille sign symbol '‰'.
            TYPE_PERMILLE = -5,

    // Represents a single currency symbol '¤'.
            TYPE_CURRENCY_SINGLE = -6,

    // Represents a double currency symbol '¤¤'.
            TYPE_CURRENCY_DOUBLE = -7,

    // Represents a triple currency symbol '¤¤¤'.
            TYPE_CURRENCY_TRIPLE = -8,

    // Represents a quadruple currency symbol '¤¤¤¤'.
            TYPE_CURRENCY_QUAD = -9,

    // Represents a quintuple currency symbol '¤¤¤¤¤'.
            TYPE_CURRENCY_QUINT = -10,

    // Represents a sequence of six or more currency symbols.
            TYPE_CURRENCY_OVERFLOW = -15
};

enum CompactType {
    TYPE_DECIMAL, TYPE_CURRENCY
};

enum Signum {
    SIGNUM_NEG = 0,
    SIGNUM_NEG_ZERO = 1,
    SIGNUM_POS_ZERO = 2,
    SIGNUM_POS = 3,
    SIGNUM_COUNT = 4,
};


class U_I18N_API AffixPatternProvider {
  public:
    static const int32_t AFFIX_PLURAL_MASK = 0xff;
    static const int32_t AFFIX_PREFIX = 0x100;
    static const int32_t AFFIX_NEGATIVE_SUBPATTERN = 0x200;
    static const int32_t AFFIX_PADDING = 0x400;

    // Convenience compound flags
    static const int32_t AFFIX_POS_PREFIX = AFFIX_PREFIX;
    static const int32_t AFFIX_POS_SUFFIX = 0;
    static const int32_t AFFIX_NEG_PREFIX = AFFIX_PREFIX | AFFIX_NEGATIVE_SUBPATTERN;
    static const int32_t AFFIX_NEG_SUFFIX = AFFIX_NEGATIVE_SUBPATTERN;

    virtual ~AffixPatternProvider();

    virtual char16_t charAt(int flags, int i) const = 0;

    virtual int length(int flags) const = 0;

    virtual UnicodeString getString(int flags) const = 0;

    virtual bool hasCurrencySign() const = 0;

    virtual bool positiveHasPlusSign() const = 0;

    virtual bool hasNegativeSubpattern() const = 0;

    virtual bool negativeHasMinusSign() const = 0;

    virtual bool containsSymbolType(AffixPatternType, UErrorCode&) const = 0;

    /**
     * True if the pattern has a number placeholder like "0" or "#,##0.00"; false if the pattern does not
     * have one. This is used in cases like compact notation, where the pattern replaces the entire
     * number instead of rendering the number.
     */
    virtual bool hasBody() const = 0;

    /**
     * True if the currency symbol should replace the decimal separator.
     */
    virtual bool currencyAsDecimal() const = 0;
};


/**
 * A Modifier is an object that can be passed through the formatting pipeline until it is finally applied to the string
 * builder. A Modifier usually contains a prefix and a suffix that are applied, but it could contain something else,
 * like a {@link com.ibm.icu.text.SimpleFormatter} pattern.
 *
 * A Modifier is usually immutable, except in cases such as {@link MutablePatternModifier}, which are mutable for performance
 * reasons.
 *
 * Exported as U_I18N_API because it is a base class for other exported types
 */
class U_I18N_API Modifier {
  public:
    virtual ~Modifier();

    /**
     * Apply this Modifier to the string builder.
     *
     * @param output
     *            The string builder to which to apply this modifier.
     * @param leftIndex
     *            The left index of the string within the builder. Equal to 0 when only one number is being formatted.
     * @param rightIndex
     *            The right index of the string within the string builder. Equal to length when only one number is being
     *            formatted.
     * @return The number of characters (UTF-16 code units) that were added to the string builder.
     */
    virtual int32_t apply(FormattedStringBuilder& output, int leftIndex, int rightIndex,
                          UErrorCode& status) const = 0;

    /**
     * Gets the length of the prefix. This information can be used in combination with {@link #apply} to extract the
     * prefix and suffix strings.
     *
     * @return The number of characters (UTF-16 code units) in the prefix.
     */
    virtual int32_t getPrefixLength() const = 0;

    /**
     * Returns the number of code points in the modifier, prefix plus suffix.
     */
    virtual int32_t getCodePointCount() const = 0;

    /**
     * Whether this modifier is strong. If a modifier is strong, it should always be applied immediately and not allowed
     * to bubble up. With regard to padding, strong modifiers are considered to be on the inside of the prefix and
     * suffix.
     *
     * @return Whether the modifier is strong.
     */
    virtual bool isStrong() const = 0;

    /**
     * Whether the modifier contains at least one occurrence of the given field.
     */
    virtual bool containsField(Field field) const = 0;

    /**
     * A fill-in for getParameters(). obj will always be set; if non-null, the other
     * two fields are also safe to read.
     */
    struct U_I18N_API Parameters {
        const ModifierStore* obj = nullptr;
        Signum signum;
        StandardPlural::Form plural;

        Parameters();
        Parameters(const ModifierStore* _obj, Signum _signum, StandardPlural::Form _plural);
    };

    /**
     * Gets a set of "parameters" for this Modifier.
     *
     * TODO: Make this return a `const Parameters*` more like Java?
     */
    virtual void getParameters(Parameters& output) const = 0;

    /**
     * Returns whether this Modifier equals another Modifier.
     */
    virtual bool strictEquals(const Modifier& other) const = 0;

    /**
     * Returns whether this Modifier is *semantically equivalent* to the other Modifier;
     * in many cases, this is the same as equal, but parameters should be ignored.
     */
    bool semanticallyEquivalent(const Modifier& other) const;
};


/**
 * This is *not* a modifier; rather, it is an object that can return modifiers
 * based on given parameters.
 *
 * Exported as U_I18N_API because it is a base class for other exported types.
 */
class U_I18N_API ModifierStore {
  public:
    virtual ~ModifierStore();

    /**
     * Returns a Modifier with the given parameters (best-effort).
     */
    virtual const Modifier* getModifier(Signum signum, StandardPlural::Form plural) const = 0;
};


/**
 * This interface is used when all number formatting settings, including the locale, are known, except for the quantity
 * itself. The {@link #processQuantity} method performs the final step in the number processing pipeline: it uses the
 * quantity to generate a finalized {@link MicroProps}, which can be used to render the number to output.
 *
 * In other words, this interface is used for the parts of number processing that are <em>quantity-dependent</em>.
 *
 * In order to allow for multiple different objects to all mutate the same MicroProps, a "chain" of MicroPropsGenerators
 * are linked together, and each one is responsible for manipulating a certain quantity-dependent part of the
 * MicroProps. At the tail of the linked list is a base instance of {@link MicroProps} with properties that are not
 * quantity-dependent. Each element in the linked list calls {@link #processQuantity} on its "parent", then does its
 * work, and then returns the result.
 *
 * This chain of MicroPropsGenerators is typically constructed by NumberFormatterImpl::macrosToMicroGenerator() when
 * constructing a NumberFormatter.
 *
 * Exported as U_I18N_API because it is a base class for other exported types
 *
 */
class U_I18N_API MicroPropsGenerator {
  public:
    virtual ~MicroPropsGenerator() = default;

    /**
     * Considers the given {@link DecimalQuantity}, optionally mutates it, and
     * populates a {@link MicroProps} instance.
     *
     * @param quantity The quantity for consideration and optional mutation.
     * @param micros The MicroProps instance to populate. It will be modified as
     *   needed for the given quantity.
     */
    virtual void processQuantity(DecimalQuantity& quantity, MicroProps& micros,
                                 UErrorCode& status) const = 0;
};

/**
 * An interface used by compact notation and scientific notation to choose a multiplier while rounding.
 */
class MultiplierProducer {
  public:
    virtual ~MultiplierProducer();

    /**
     * Maps a magnitude to a multiplier in powers of ten. For example, in compact notation in English, a magnitude of 5
     * (e.g., 100,000) should return a multiplier of -3, since the number is displayed in thousands.
     *
     * @param magnitude
     *            The power of ten of the input number.
     * @return The shift in powers of ten.
     */
    virtual int32_t getMultiplier(int32_t magnitude) const = 0;
};

// Exported as U_I18N_API because it is a public member field of exported DecimalFormatProperties
template<typename T>
class U_I18N_API NullableValue {
  public:
    NullableValue()
            : fNull(true) {}

    NullableValue(const NullableValue<T>& other) = default;

    explicit NullableValue(const T& other) {
        fValue = other;
        fNull = false;
    }

    NullableValue<T>& operator=(const NullableValue<T>& other) {
        fNull = other.fNull;
        if (!fNull) {
            fValue = other.fValue;
        }
        return *this;
    }

    NullableValue<T>& operator=(const T& other) {
        fValue = other;
        fNull = false;
        return *this;
    }

    bool operator==(const NullableValue& other) const {
        // "fValue == other.fValue" returns UBool, not bool (causes compiler warnings)
        return fNull ? other.fNull : (other.fNull ? false : static_cast<bool>(fValue == other.fValue));
    }

    void nullify() {
        // TODO: It might be nice to call the destructor here.
        fNull = true;
    }

    bool isNull() const {
        return fNull;
    }

    T get(UErrorCode& status) const {
        if (fNull) {
            status = U_UNDEFINED_VARIABLE;
        }
        return fValue;
    }

    T getNoError() const {
        return fValue;
    }

    T getOrDefault(T defaultValue) const {
        return fNull ? defaultValue : fValue;
    }

  private:
    bool fNull;
    T fValue;
};

} // namespace number::impl
U_NAMESPACE_END

#endif //__NUMBER_TYPES_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
