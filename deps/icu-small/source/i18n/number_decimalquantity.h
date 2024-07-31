// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_DECIMALQUANTITY_H__
#define __NUMBER_DECIMALQUANTITY_H__

#include <cstdint>
#include "unicode/umachine.h"
#include "standardplural.h"
#include "plurrule_impl.h"
#include "number_types.h"

U_NAMESPACE_BEGIN
namespace number::impl {

// Forward-declare (maybe don't want number_utils.h included here):
class DecNum;

/**
 * A class for representing a number to be processed by the decimal formatting pipeline. Includes
 * methods for rounding, plural rules, and decimal digit extraction.
 *
 * <p>By design, this is NOT IMMUTABLE and NOT THREAD SAFE. It is intended to be an intermediate
 * object holding state during a pass through the decimal formatting pipeline.
 *
 * <p>Represents numbers and digit display properties using Binary Coded Decimal (BCD).
 *
 * <p>Java has multiple implementations for testing, but C++ has only one implementation.
 */
class U_I18N_API DecimalQuantity : public IFixedDecimal, public UMemory {
  public:
    /** Copy constructor. */
    DecimalQuantity(const DecimalQuantity &other);

    /** Move constructor. */
    DecimalQuantity(DecimalQuantity &&src) noexcept;

    DecimalQuantity();

    ~DecimalQuantity() override;

    /**
     * Sets this instance to be equal to another instance.
     *
     * @param other The instance to copy from.
     */
    DecimalQuantity &operator=(const DecimalQuantity &other);

    /** Move assignment */
    DecimalQuantity &operator=(DecimalQuantity&& src) noexcept;

    /**
     * If the minimum integer digits are greater than `minInt`,
     * sets it to `minInt`.
     *
     * @param minInt The minimum number of integer digits.
     */
    void decreaseMinIntegerTo(int32_t minInt);

    /**
     * Sets the minimum integer digits that this {@link DecimalQuantity} should generate.
     * This method does not perform rounding.
     *
     * @param minInt The minimum number of integer digits.
     */
    void increaseMinIntegerTo(int32_t minInt);

    /**
     * Sets the minimum fraction digits that this {@link DecimalQuantity} should generate.
     * This method does not perform rounding.
     *
     * @param minFrac The minimum number of fraction digits.
     */
    void setMinFraction(int32_t minFrac);

    /**
     * Truncates digits from the upper magnitude of the number in order to satisfy the
     * specified maximum number of integer digits.
     *
     * @param maxInt The maximum number of integer digits.
     */
    void applyMaxInteger(int32_t maxInt);

    /**
     * Rounds the number to a specified interval, such as 0.05.
     *
     * <p>If rounding to a power of ten, use the more efficient {@link #roundToMagnitude} instead.
     *
     * @param increment The increment to which to round.
     * @param magnitude The power of 10 to which to round.
     * @param roundingMode The {@link RoundingMode} to use if rounding is necessary.
     */
    void roundToIncrement(
        uint64_t increment,
        digits_t magnitude,
        RoundingMode roundingMode,
        UErrorCode& status);

    /** Removes all fraction digits. */
    void truncate();

    /**
     * Rounds the number to the nearest multiple of 5 at the specified magnitude.
     * For example, when magnitude == -2, this performs rounding to the nearest 0.05.
     *
     * @param magnitude The magnitude at which the digit should become either 0 or 5.
     * @param roundingMode Rounding strategy.
     */
    void roundToNickel(int32_t magnitude, RoundingMode roundingMode, UErrorCode& status);

    /**
     * Rounds the number to a specified magnitude (power of ten).
     *
     * @param roundingMagnitude The power of ten to which to round. For example, a value of -2 will
     *     round to 2 decimal places.
     * @param roundingMode The {@link RoundingMode} to use if rounding is necessary.
     */
    void roundToMagnitude(int32_t magnitude, RoundingMode roundingMode, UErrorCode& status);

    /**
     * Rounds the number to an infinite number of decimal points. This has no effect except for
     * forcing the double in {@link DecimalQuantity_AbstractBCD} to adopt its exact representation.
     */
    void roundToInfinity();

    /**
     * Multiply the internal value. Uses decNumber.
     *
     * @param multiplicand The value by which to multiply.
     */
    void multiplyBy(const DecNum& multiplicand, UErrorCode& status);

    /**
     * Divide the internal value. Uses decNumber.
     *
     * @param multiplicand The value by which to multiply.
     */
    void divideBy(const DecNum& divisor, UErrorCode& status);

    /** Flips the sign from positive to negative and back. */
    void negate();

    /**
     * Scales the number by a power of ten. For example, if the value is currently "1234.56", calling
     * this method with delta=-3 will change the value to "1.23456".
     *
     * @param delta The number of magnitudes of ten to change by.
     * @return true if integer overflow occurred; false otherwise.
     */
    bool adjustMagnitude(int32_t delta);

    /**
     * Scales the number such that the least significant nonzero digit is at magnitude 0.
     *
     * @return The previous magnitude of the least significant digit.
     */
    int32_t adjustToZeroScale();

    /**
     * @return The power of ten corresponding to the most significant nonzero digit.
     * The number must not be zero.
     */
    int32_t getMagnitude() const;

    /**
     * @return The value of the (suppressed) exponent after the number has been
     * put into a notation with exponents (ex: compact, scientific).  Ex: given
     * the number 1000 as "1K" / "1E3", the return value will be 3 (positive).
     */
    int32_t getExponent() const;

    /**
     * Adjusts the value for the (suppressed) exponent stored when using
     * notation with exponents (ex: compact, scientific).
     *
     * <p>Adjusting the exponent is decoupled from {@link #adjustMagnitude} in
     * order to allow flexibility for {@link StandardPlural} to be selected in
     * formatting (ex: for compact notation) either with or without the exponent
     * applied in the value of the number.
     * @param delta
     *             The value to adjust the exponent by.
     */
    void adjustExponent(int32_t delta);

    /**
     * Resets the DecimalQuantity to the value before adjustMagnitude and adjustExponent.
     */
    void resetExponent();

    /**
     * @return Whether the value represented by this {@link DecimalQuantity} is
     * zero, infinity, or NaN.
     */
    bool isZeroish() const;

    /** @return Whether the value represented by this {@link DecimalQuantity} is less than zero. */
    bool isNegative() const;

    /** @return The appropriate value from the Signum enum. */
    Signum signum() const;

    /** @return Whether the value represented by this {@link DecimalQuantity} is infinite. */
    bool isInfinite() const override;

    /** @return Whether the value represented by this {@link DecimalQuantity} is not a number. */
    bool isNaN() const override;

    /**  
     * Note: this method incorporates the value of {@code exponent}
     * (for cases such as compact notation) to return the proper long value
     * represented by the result.
     * @param truncateIfOverflow if false and the number does NOT fit, fails with an assertion error. 
     */
    int64_t toLong(bool truncateIfOverflow = false) const;

    /**
     * Note: this method incorporates the value of {@code exponent}
     * (for cases such as compact notation) to return the proper long value
     * represented by the result.
     */
    uint64_t toFractionLong(bool includeTrailingZeros) const;

    /**
     * Returns whether or not a Long can fully represent the value stored in this DecimalQuantity.
     * @param ignoreFraction if true, silently ignore digits after the decimal place.
     */
    bool fitsInLong(bool ignoreFraction = false) const;

    /** @return The value contained in this {@link DecimalQuantity} approximated as a double. */
    double toDouble() const;

    /** Computes a DecNum representation of this DecimalQuantity, saving it to the output parameter. */
    DecNum& toDecNum(DecNum& output, UErrorCode& status) const;

    DecimalQuantity &setToInt(int32_t n);

    DecimalQuantity &setToLong(int64_t n);

    DecimalQuantity &setToDouble(double n);

    /**
     * Produces a DecimalQuantity that was parsed from a string by the decNumber
     * C Library.
     *
     * decNumber is similar to BigDecimal in Java, and supports parsing strings
     * such as "123.456621E+40".
     */
    DecimalQuantity &setToDecNumber(StringPiece n, UErrorCode& status);

    /** Internal method if the caller already has a DecNum. */
    DecimalQuantity &setToDecNum(const DecNum& n, UErrorCode& status);

    /** Returns a DecimalQuantity after parsing the input string. */
    static DecimalQuantity fromExponentString(UnicodeString n, UErrorCode& status);

    /**
     * Appends a digit, optionally with one or more leading zeros, to the end of the value represented
     * by this DecimalQuantity.
     *
     * <p>The primary use of this method is to construct numbers during a parsing loop. It allows
     * parsing to take advantage of the digit list infrastructure primarily designed for formatting.
     *
     * @param value The digit to append.
     * @param leadingZeros The number of zeros to append before the digit. For example, if the value
     *     in this instance starts as 12.3, and you append a 4 with 1 leading zero, the value becomes
     *     12.304.
     * @param appendAsInteger If true, increase the magnitude of existing digits to make room for the
     *     new digit. If false, append to the end like a fraction digit. If true, there must not be
     *     any fraction digits already in the number.
     * @internal
     * @deprecated This API is ICU internal only.
     */
    void appendDigit(int8_t value, int32_t leadingZeros, bool appendAsInteger);

    double getPluralOperand(PluralOperand operand) const override;

    bool hasIntegerValue() const override;

    /**
     * Gets the digit at the specified magnitude. For example, if the represented number is 12.3,
     * getDigit(-1) returns 3, since 3 is the digit corresponding to 10^-1.
     *
     * @param magnitude The magnitude of the digit.
     * @return The digit at the specified magnitude.
     */
    int8_t getDigit(int32_t magnitude) const;

    /**
     * Gets the largest power of ten that needs to be displayed. The value returned by this function
     * will be bounded between minInt and maxInt.
     *
     * @return The highest-magnitude digit to be displayed.
     */
    int32_t getUpperDisplayMagnitude() const;

    /**
     * Gets the smallest power of ten that needs to be displayed. The value returned by this function
     * will be bounded between -minFrac and -maxFrac.
     *
     * @return The lowest-magnitude digit to be displayed.
     */
    int32_t getLowerDisplayMagnitude() const;

    int32_t fractionCount() const;

    int32_t fractionCountWithoutTrailingZeros() const;

    void clear();

    /** This method is for internal testing only. */
    uint64_t getPositionFingerprint() const;

//    /**
//     * If the given {@link FieldPosition} is a {@link UFieldPosition}, populates it with the fraction
//     * length and fraction long value. If the argument is not a {@link UFieldPosition}, nothing
//     * happens.
//     *
//     * @param fp The {@link UFieldPosition} to populate.
//     */
//    void populateUFieldPosition(FieldPosition fp);

    /**
     * Checks whether the bytes stored in this instance are all valid. For internal unit testing only.
     *
     * @return An error message if this instance is invalid, or null if this instance is healthy.
     */
    const char16_t* checkHealth() const;

    UnicodeString toString() const;

    /** Returns the string in standard exponential notation. */
    UnicodeString toScientificString() const;

    /** Returns the string without exponential notation. Slightly slower than toScientificString(). */
    UnicodeString toPlainString() const;

    /** Returns the string using ASCII digits and using exponential notation for non-zero
    exponents, following the UTS 35 specification for plural rule samples. */
    UnicodeString toExponentString() const;

    /** Visible for testing */
    inline bool isUsingBytes() { return usingBytes; }

    /** Visible for testing */
    inline bool isExplicitExactDouble() { return explicitExactDouble; }

    bool operator==(const DecimalQuantity& other) const;

    inline bool operator!=(const DecimalQuantity& other) const {
        return !(*this == other);
    }

    /**
     * Bogus flag for when a DecimalQuantity is stored on the stack.
     */
    bool bogus = false;

  private:
    /**
     * The power of ten corresponding to the least significant digit in the BCD. For example, if this
     * object represents the number "3.14", the BCD will be "0x314" and the scale will be -2.
     *
     * <p>Note that in {@link java.math.BigDecimal}, the scale is defined differently: the number of
     * digits after the decimal place, which is the negative of our definition of scale.
     */
    int32_t scale;

    /**
     * The number of digits in the BCD. For example, "1007" has BCD "0x1007" and precision 4. The
     * maximum precision is 16 since a long can hold only 16 digits.
     *
     * <p>This value must be re-calculated whenever the value in bcd changes by using {@link
     * #computePrecisionAndCompact()}.
     */
    int32_t precision;

    /**
     * A bitmask of properties relating to the number represented by this object.
     *
     * @see #NEGATIVE_FLAG
     * @see #INFINITY_FLAG
     * @see #NAN_FLAG
     */
    int8_t flags;

    // The following three fields relate to the double-to-ascii fast path algorithm.
    // When a double is given to DecimalQuantityBCD, it is converted to using a fast algorithm. The
    // fast algorithm guarantees correctness to only the first ~12 digits of the double. The process
    // of rounding the number ensures that the converted digits are correct, falling back to a slow-
    // path algorithm if required.  Therefore, if a DecimalQuantity is constructed from a double, it
    // is *required* that roundToMagnitude(), roundToIncrement(), or roundToInfinity() is called. If
    // you don't round, assertions will fail in certain other methods if you try calling them.

    /**
     * Whether the value in the BCD comes from the double fast path without having been rounded to
     * ensure correctness
     */
    UBool isApproximate;

    /**
     * The original number provided by the user and which is represented in BCD. Used when we need to
     * re-compute the BCD for an exact double representation.
     */
    double origDouble;

    /**
     * The change in magnitude relative to the original double. Used when we need to re-compute the
     * BCD for an exact double representation.
     */
    int32_t origDelta;

    // Positions to keep track of leading and trailing zeros.
    // lReqPos is the magnitude of the first required leading zero.
    // rReqPos is the magnitude of the last required trailing zero.
    int32_t lReqPos = 0;
    int32_t rReqPos = 0;

    // The value of the (suppressed) exponent after the number has been put into
    // a notation with exponents (ex: compact, scientific).
    int32_t exponent = 0;

    /**
     * The BCD of the 16 digits of the number represented by this object. Every 4 bits of the long map
     * to one digit. For example, the number "12345" in BCD is "0x12345".
     *
     * <p>Whenever bcd changes internally, {@link #compact()} must be called, except in special cases
     * like setting the digit to zero.
     */
    union {
        struct {
            int8_t *ptr;
            int32_t len;
        } bcdBytes;
        uint64_t bcdLong;
    } fBCD;

    bool usingBytes = false;

    /**
     * Whether this {@link DecimalQuantity} has been explicitly converted to an exact double. true if
     * backed by a double that was explicitly converted via convertToAccurateDouble; false otherwise.
     * Used for testing.
     */
    bool explicitExactDouble = false;

    void roundToMagnitude(int32_t magnitude, RoundingMode roundingMode, bool nickel, UErrorCode& status);

    /**
     * Returns a single digit from the BCD list. No internal state is changed by calling this method.
     *
     * @param position The position of the digit to pop, counted in BCD units from the least
     *     significant digit. If outside the range supported by the implementation, zero is returned.
     * @return The digit at the specified location.
     */
    int8_t getDigitPos(int32_t position) const;

    /**
     * Sets the digit in the BCD list. This method only sets the digit; it is the caller's
     * responsibility to call {@link #compact} after setting the digit, and to ensure
     * that the precision field is updated to reflect the correct number of digits if a
     * nonzero digit is added to the decimal.
     *
     * @param position The position of the digit to pop, counted in BCD units from the least
     *     significant digit. If outside the range supported by the implementation, an AssertionError
     *     is thrown.
     * @param value The digit to set at the specified location.
     */
    void setDigitPos(int32_t position, int8_t value);

    /**
     * Adds zeros to the end of the BCD list. This will result in an invalid BCD representation; it is
     * the caller's responsibility to do further manipulation and then call {@link #compact}.
     *
     * @param numDigits The number of zeros to add.
     */
    void shiftLeft(int32_t numDigits);

    /**
     * Directly removes digits from the end of the BCD list.
     * Updates the scale and precision.
     *
     * CAUTION: it is the caller's responsibility to call {@link #compact} after this method.
     */
    void shiftRight(int32_t numDigits);

    /**
     * Directly removes digits from the front of the BCD list.
     * Updates precision.
     *
     * CAUTION: it is the caller's responsibility to call {@link #compact} after this method.
     */
    void popFromLeft(int32_t numDigits);

    /**
     * Sets the internal representation to zero. Clears any values stored in scale, precision,
     * hasDouble, origDouble, origDelta, exponent, and BCD data.
     */
    void setBcdToZero();

    /**
     * Sets the internal BCD state to represent the value in the given int. The int is guaranteed to
     * be either positive. The internal state is guaranteed to be empty when this method is called.
     *
     * @param n The value to consume.
     */
    void readIntToBcd(int32_t n);

    /**
     * Sets the internal BCD state to represent the value in the given long. The long is guaranteed to
     * be either positive. The internal state is guaranteed to be empty when this method is called.
     *
     * @param n The value to consume.
     */
    void readLongToBcd(int64_t n);

    void readDecNumberToBcd(const DecNum& dn);

    void readDoubleConversionToBcd(const char* buffer, int32_t length, int32_t point);

    void copyFieldsFrom(const DecimalQuantity& other);

    void copyBcdFrom(const DecimalQuantity &other);

    void moveBcdFrom(DecimalQuantity& src);

    /**
     * Removes trailing zeros from the BCD (adjusting the scale as required) and then computes the
     * precision. The precision is the number of digits in the number up through the greatest nonzero
     * digit.
     *
     * <p>This method must always be called when bcd changes in order for assumptions to be correct in
     * methods like {@link #fractionCount()}.
     */
    void compact();

    void _setToInt(int32_t n);

    void _setToLong(int64_t n);

    void _setToDoubleFast(double n);

    void _setToDecNum(const DecNum& dn, UErrorCode& status);

    static int32_t getVisibleFractionCount(UnicodeString value);

    void convertToAccurateDouble();

    /** Ensure that a byte array of at least 40 digits is allocated. */
    void ensureCapacity();

    void ensureCapacity(int32_t capacity);

    /** Switches the internal storage mechanism between the 64-bit long and the byte array. */
    void switchStorage();
};

} // namespace number::impl
U_NAMESPACE_END


#endif //__NUMBER_DECIMALQUANTITY_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
