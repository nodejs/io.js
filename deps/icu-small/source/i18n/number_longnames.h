// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_LONGNAMES_H__
#define __NUMBER_LONGNAMES_H__

#include "cmemory.h"
#include "unicode/listformatter.h"
#include "unicode/uversion.h"
#include "number_utils.h"
#include "number_modifiers.h"

U_NAMESPACE_BEGIN
namespace number::impl {

// LongNameHandler takes care of formatting currency and measurement unit names,
// as well as populating the gender of measure units.
class LongNameHandler : public MicroPropsGenerator, public ModifierStore, public UMemory {
  public:
    static UnicodeString getUnitDisplayName(
        const Locale& loc,
        const MeasureUnit& unit,
        UNumberUnitWidth width,
        UErrorCode& status);

    // This function does not support inflections or other newer NumberFormatter
    // features: it exists to support the older not-recommended MeasureFormat.
    static UnicodeString getUnitPattern(
        const Locale& loc,
        const MeasureUnit& unit,
        UNumberUnitWidth width,
        StandardPlural::Form pluralForm,
        UErrorCode& status);

    static LongNameHandler*
    forCurrencyLongNames(const Locale &loc, const CurrencyUnit &currency, const PluralRules *rules,
                         const MicroPropsGenerator *parent, UErrorCode &status);

    /**
     * Construct a localized LongNameHandler for the specified MeasureUnit.
     *
     * Mixed units are not supported, use MixedUnitLongNameHandler::forMeasureUnit.
     *
     * This function uses a fillIn instead of returning a pointer, because we
     * want to fill in instances in a MemoryPool (which cannot adopt pointers it
     * didn't create itself).
     *
     * @param loc The desired locale.
     * @param unitRef The measure unit to construct a LongNameHandler for.
     * @param width Specifies the desired unit rendering.
     * @param unitDisplayCase Specifies the desired grammatical case. If the
     *     specified case is not found, we fall back to nominative or no-case.
     * @param rules Does not take ownership.
     * @param parent Does not take ownership.
     * @param fillIn Required.
     */
    static void forMeasureUnit(const Locale &loc,
                               const MeasureUnit &unitRef,
                               const UNumberUnitWidth &width,
                               const char *unitDisplayCase,
                               const PluralRules *rules,
                               const MicroPropsGenerator *parent,
                               LongNameHandler *fillIn,
                               UErrorCode &status);

    /**
     * Selects the plural-appropriate Modifier from the set of fModifiers based
     * on the plural form.
     */
    void
    processQuantity(DecimalQuantity &quantity, MicroProps &micros, UErrorCode &status) const override;

    const Modifier* getModifier(Signum signum, StandardPlural::Form plural) const override;

  private:
    // A set of pre-computed modifiers, one for each plural form.
    SimpleModifier fModifiers[StandardPlural::Form::COUNT];
    // Not owned
    const PluralRules *rules;
    // Not owned
    const MicroPropsGenerator *parent;
    // Grammatical gender of the formatted result. Not owned: must point at
    // static or global strings.
    const char *gender = "";

    LongNameHandler(const PluralRules *rules, const MicroPropsGenerator *parent)
        : rules(rules), parent(parent) {
    }

    LongNameHandler() : rules(nullptr), parent(nullptr) {
    }

    // Enables MemoryPool<LongNameHandler>::emplaceBack(): requires access to
    // the private constructors.
    friend class MemoryPool<LongNameHandler>;

    // Allow macrosToMicroGenerator to call the private default constructor.
    friend class NumberFormatterImpl;

    // Fills in LongNameHandler fields for formatting units identified `unit`.
    static void forArbitraryUnit(const Locale &loc,
                                 const MeasureUnit &unit,
                                 const UNumberUnitWidth &width,
                                 const char *unitDisplayCase,
                                 LongNameHandler *fillIn,
                                 UErrorCode &status);

    // Roughly corresponds to patternTimes(...) in the spec:
    // https://unicode.org/reports/tr35/tr35-general.html#compound-units
    //
    // productUnit is an rvalue reference to indicate this function consumes it,
    // leaving it in a not-useful / undefined state.
    static void processPatternTimes(MeasureUnitImpl &&productUnit,
                                    Locale loc,
                                    const UNumberUnitWidth &width,
                                    const char *caseVariant,
                                    UnicodeString *outArray,
                                    UErrorCode &status);

    // Sets fModifiers to use the patterns from `simpleFormats`.
    void simpleFormatsToModifiers(const UnicodeString *simpleFormats, Field field, UErrorCode &status);

    // Sets fModifiers to a combination of `leadFormats` (one per plural form)
    // and `trailFormat` appended to each.
    //
    // With a leadFormat of "{0}m" and a trailFormat of "{0}/s", it produces a
    // pattern of "{0}m/s" by inserting each leadFormat pattern into trailFormat.
    void multiSimpleFormatsToModifiers(const UnicodeString *leadFormats, UnicodeString trailFormat,
                                       Field field, UErrorCode &status);
};

// Similar to LongNameHandler, but only for MIXED units.
class MixedUnitLongNameHandler : public MicroPropsGenerator, public ModifierStore, public UMemory {
  public:
    /**
     * Construct a localized MixedUnitLongNameHandler for the specified
     * MeasureUnit. It must be a MIXED unit.
     *
     * This function uses a fillIn instead of returning a pointer, because we
     * want to fill in instances in a MemoryPool (which cannot adopt pointers it
     * didn't create itself).
     *
     * @param loc The desired locale.
     * @param mixedUnit The mixed measure unit to construct a
     *     MixedUnitLongNameHandler for.
     * @param width Specifies the desired unit rendering.
     * @param unitDisplayCase Specifies the desired grammatical case. If the
     *     specified case is not found, we fall back to nominative or no-case.
     * @param rules Does not take ownership.
     * @param parent Does not take ownership.
     * @param fillIn Required.
     */
    static void forMeasureUnit(const Locale &loc,
                               const MeasureUnit &mixedUnit,
                               const UNumberUnitWidth &width,
                               const char *unitDisplayCase,
                               const PluralRules *rules,
                               const MicroPropsGenerator *parent,
                               MixedUnitLongNameHandler *fillIn,
                               UErrorCode &status);

    /**
     * Produces a plural-appropriate Modifier for a mixed unit: `quantity` is
     * taken as the final smallest unit, while the larger unit values must be
     * provided via `micros.mixedMeasures`.
     */
    void processQuantity(DecimalQuantity &quantity, MicroProps &micros,
                         UErrorCode &status) const override;

    // Required for ModifierStore. And ModifierStore is required by
    // SimpleModifier constructor's last parameter. We assert his will never get
    // called though.
    const Modifier *getModifier(Signum signum, StandardPlural::Form plural) const override;

  private:
    // Not owned
    const PluralRules *rules;

    // Not owned
    const MicroPropsGenerator *parent;

    // Total number of units in the MeasureUnit this handler was configured for:
    // for "foot-and-inch", this will be 2.
    int32_t fMixedUnitCount = 1;

    // Stores unit data for each of the individual units. For each unit, it
    // stores ARRAY_LENGTH strings, as returned by getMeasureData. (Each unit
    // with index `i` has ARRAY_LENGTH strings starting at index
    // `i*ARRAY_LENGTH` in this array.)
    LocalArray<UnicodeString> fMixedUnitData;

    // Formats the larger units of Mixed Unit measurements.
    LocalizedNumberFormatter fNumberFormatter;

    // Joins mixed units together.
    LocalPointer<ListFormatter> fListFormatter;

    MixedUnitLongNameHandler(const PluralRules *rules, const MicroPropsGenerator *parent)
        : rules(rules), parent(parent) {
    }

    MixedUnitLongNameHandler() : rules(nullptr), parent(nullptr) {
    }

    // Allow macrosToMicroGenerator to call the private default constructor.
    friend class NumberFormatterImpl;

    // Enables MemoryPool<LongNameHandler>::emplaceBack(): requires access to
    // the private constructors.
    friend class MemoryPool<MixedUnitLongNameHandler>;

    // For a mixed unit, returns a Modifier that takes only one parameter: the
    // smallest and final unit of the set. The bigger units' values and labels
    // get baked into this Modifier, together with the unit label of the final
    // unit.
    const Modifier *getMixedUnitModifier(DecimalQuantity &quantity, MicroProps &micros,
                                         UErrorCode &status) const;
};

/**
 * A MicroPropsGenerator that multiplexes between different LongNameHandlers,
 * depending on the outputUnit.
 *
 * See processQuantity() for the input requirements.
 */
class LongNameMultiplexer : public MicroPropsGenerator, public UMemory {
  public:
    // Produces a multiplexer for LongNameHandlers, one for each unit in
    // `units`. An individual unit might be a mixed unit.
    static LongNameMultiplexer *forMeasureUnits(const Locale &loc,
                                                const MaybeStackVector<MeasureUnit> &units,
                                                const UNumberUnitWidth &width,
                                                const char *unitDisplayCase,
                                                const PluralRules *rules,
                                                const MicroPropsGenerator *parent,
                                                UErrorCode &status);

    // The output unit must be provided via `micros.outputUnit`, it must match
    // one of the units provided to the factory function.
    void processQuantity(DecimalQuantity &quantity, MicroProps &micros,
                         UErrorCode &status) const override;

  private:
    /**
     * Because we only know which LongNameHandler we wish to call after calling
     * earlier MicroPropsGenerators in the chain, LongNameMultiplexer keeps the
     * parent link, while the LongNameHandlers are given no parents.
     */
    MemoryPool<LongNameHandler> fLongNameHandlers;
    MemoryPool<MixedUnitLongNameHandler> fMixedUnitHandlers;
    // Unowned pointers to instances owned by MaybeStackVectors.
    MaybeStackArray<MicroPropsGenerator *, 8> fHandlers;
    // Each MeasureUnit corresponds to the same-index MicroPropsGenerator
    // pointed to in fHandlers.
    LocalArray<MeasureUnit> fMeasureUnits;

    const MicroPropsGenerator *fParent;

    LongNameMultiplexer(const MicroPropsGenerator *parent) : fParent(parent) {
    }
};

} // namespace number::impl
U_NAMESPACE_END

#endif //__NUMBER_LONGNAMES_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
