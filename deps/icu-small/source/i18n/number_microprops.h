// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_MICROPROPS_H__
#define __NUMBER_MICROPROPS_H__

// TODO: minimize includes
#include "unicode/numberformatter.h"
#include "number_types.h"
#include "number_decimalquantity.h"
#include "number_scientific.h"
#include "number_patternstring.h"
#include "number_modifiers.h"
#include "number_multiplier.h"
#include "number_roundingutils.h"
#include "decNumber.h"
#include "charstr.h"
#include "util.h"

U_NAMESPACE_BEGIN
namespace number::impl {

/**
 * A copyable container for the integer values of mixed unit measurements.
 *
 * If memory allocation fails during copying, no values are copied and status is
 * set to U_MEMORY_ALLOCATION_ERROR.
 */
class IntMeasures : public MaybeStackArray<int64_t, 2> {
  public:
    /**
     * Default constructor initializes with internal T[stackCapacity] buffer.
     *
     * Stack Capacity: most mixed units are expected to consist of two or three
     * subunits, so one or two integer measures should be enough.
     */
    IntMeasures() : MaybeStackArray<int64_t, 2>() {}

    /**
     * Copy constructor.
     *
     * If memory allocation fails during copying, no values are copied and
     * status is set to U_MEMORY_ALLOCATION_ERROR.
     */
    IntMeasures(const IntMeasures &other) : MaybeStackArray<int64_t, 2>() {
        this->operator=(other);
    }

    // Assignment operator
    IntMeasures &operator=(const IntMeasures &rhs) {
        if (this == &rhs) {
            return *this;
        }
        copyFrom(rhs, status);
        return *this;
    }

    /** Move constructor */
    IntMeasures(IntMeasures &&src) = default;

    /** Move assignment */
    IntMeasures &operator=(IntMeasures &&src) = default;

    UErrorCode status = U_ZERO_ERROR;
};

struct SimpleMicroProps : public UMemory {
    Grouper grouping;
    bool useCurrency = false;
    UNumberDecimalSeparatorDisplay decimal = UNUM_DECIMAL_SEPARATOR_AUTO;

    // Currency symbol to be used as the decimal separator
    UnicodeString currencyAsDecimal = ICU_Utility::makeBogusString();

    // Note: This struct has no direct ownership of the following pointer.
    const DecimalFormatSymbols* symbols = nullptr;
};

/**
 * MicroProps is the first MicroPropsGenerator that should be should be called,
 * producing an initialized MicroProps instance that will be passed on and
 * modified throughout the rest of the chain of MicroPropsGenerator instances.
 */
struct MicroProps : public MicroPropsGenerator {
    SimpleMicroProps simple;

    // NOTE: All of these fields are properly initialized in NumberFormatterImpl.
    RoundingImpl rounder;
    Padder padding;
    IntegerWidth integerWidth;
    UNumberSignDisplay sign;
    char nsName[9];

    // No ownership: must point at a string which will outlive MicroProps
    // instances, e.g. a string with static storage duration, or just a string
    // that will never be deallocated or modified.
    const char *gender;

    // Note: This struct has no direct ownership of the following pointers.

    // Pointers to Modifiers provided by the number formatting pipeline (when
    // the value is known):

    // A Modifier provided by LongNameHandler, used for currency long names and
    // units. If there is no LongNameHandler needed, this should be an
    // EmptyModifier. (This is typically the third modifier applied.)
    const Modifier* modOuter;
    // A Modifier for short currencies and compact notation. (This is typically
    // the second modifier applied.)
    const Modifier* modMiddle = nullptr;
    // A Modifier provided by ScientificHandler, used for scientific notation.
    // This is typically the first modifier applied.
    const Modifier* modInner;

    // The following "helper" fields may optionally be used during the MicroPropsGenerator.
    // They live here to retain memory.
    struct {
        // The ScientificModifier for which ScientificHandler is responsible.
        // ScientificHandler::processQuantity() modifies this Modifier.
        ScientificModifier scientificModifier;
        // EmptyModifier used for modOuter
        EmptyModifier emptyWeakModifier{false};
        // EmptyModifier used for modInner
        EmptyModifier emptyStrongModifier{true};
        MultiplierFormatHandler multiplier;
        // A Modifier used for Mixed Units. When formatting mixed units,
        // LongNameHandler assigns this Modifier.
        SimpleModifier mixedUnitModifier;
    } helpers;

    // The MeasureUnit with which the output is represented. May also have
    // UMEASURE_UNIT_MIXED complexity, in which case mixedMeasures comes into
    // play.
    MeasureUnit outputUnit;

    // Contains all the values of each unit in mixed units. For quantity (which is the floating value of
    // the smallest unit in the mixed unit), the value stores in `quantity`.
    // NOTE: the value of quantity in `mixedMeasures` will be left unset.
    IntMeasures mixedMeasures;

    // Points to quantity position, -1 if the position is not set yet.
    int32_t indexOfQuantity = -1;

    // Number of mixedMeasures that have been populated
    int32_t mixedMeasuresCount = 0;

    MicroProps() = default;

    MicroProps(const MicroProps& other) = default;

    MicroProps& operator=(const MicroProps& other) = default;

    /**
     * As MicroProps is the "base instance", this implementation of
     * MicroPropsGenerator::processQuantity() just ensures that the output
     * `micros` is correctly initialized.
     *
     * For the "safe" invocation of this function, micros must not be *this,
     * such that a copy of the base instance is made. For the "unsafe" path,
     * this function can be used only once, because the base MicroProps instance
     * will be modified and thus not be available for re-use.
     *
     * @param quantity The quantity for consideration and optional mutation.
     * @param micros The MicroProps instance to populate. If this parameter is
     * not already `*this`, it will be overwritten with a copy of `*this`.
     */
    void processQuantity(DecimalQuantity &quantity, MicroProps &micros,
                         UErrorCode &status) const override {
        (void) quantity;
        (void) status;
        if (this == &micros) {
            // Unsafe path: no need to perform a copy.
            U_ASSERT(!exhausted);
            micros.exhausted = true;
            U_ASSERT(exhausted);
        } else {
            // Safe path: copy self into the output micros.
            U_ASSERT(!exhausted);
            micros = *this;
        }
    }

  private:
    // Internal fields:
    bool exhausted = false;
};

} // namespace number::impl
U_NAMESPACE_END

#endif // __NUMBER_MICROPROPS_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
