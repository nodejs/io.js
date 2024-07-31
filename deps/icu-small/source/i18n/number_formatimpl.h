// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_FORMATIMPL_H__
#define __NUMBER_FORMATIMPL_H__

#include "number_types.h"
#include "formatted_string_builder.h"
#include "number_patternstring.h"
#include "number_usageprefs.h"
#include "number_utils.h"
#include "number_patternmodifier.h"
#include "number_longnames.h"
#include "number_compact.h"
#include "number_microprops.h"
#include "number_utypes.h"

U_NAMESPACE_BEGIN
namespace number::impl {

/**
 * This is the "brain" of the number formatting pipeline. It ties all the pieces together, taking in a MacroProps and a
 * DecimalQuantity and outputting a properly formatted number string.
 */
class NumberFormatterImpl : public UMemory {
  public:
    /**
     * Builds a "safe" MicroPropsGenerator, which is thread-safe and can be used repeatedly.
     * The caller owns the returned NumberFormatterImpl.
     */
    NumberFormatterImpl(const MacroProps &macros, UErrorCode &status);

    /**
     * Default constructor; leaves the NumberFormatterImpl in an undefined state.
     * Takes an error code to prevent the method from being called accidentally.
     */
    NumberFormatterImpl(UErrorCode &) {}

    /**
     * Builds and evaluates an "unsafe" MicroPropsGenerator, which is cheaper but can be used only once.
     */
    static int32_t formatStatic(const MacroProps &macros, UFormattedNumberData *results,
                                UErrorCode &status);

    /**
     * Prints only the prefix and suffix; used for DecimalFormat getters.
     *
     * @return The index into the output at which the prefix ends and the suffix starts; in other words,
     *         the prefix length.
     */
    static int32_t getPrefixSuffixStatic(const MacroProps& macros, Signum signum,
                                         StandardPlural::Form plural, FormattedStringBuilder& outString,
                                         UErrorCode& status);

    /**
     * Evaluates the "safe" MicroPropsGenerator created by "fromMacros".
     */
    int32_t format(UFormattedNumberData *results, UErrorCode &status) const;

    /**
     * Like format(), but saves the result into an output MicroProps without additional processing.
     */
    void preProcess(DecimalQuantity& inValue, MicroProps& microsOut, UErrorCode& status) const;

    /**
     * Like getPrefixSuffixStatic() but uses the safe compiled object.
     */
    int32_t getPrefixSuffix(Signum signum, StandardPlural::Form plural, FormattedStringBuilder& outString,
                            UErrorCode& status) const;

    const MicroProps& getRawMicroProps() const {
        return fMicros;
    }

    /**
     * Synthesizes the output string from a MicroProps and DecimalQuantity.
     * This method formats only the main number, not affixes.
     */
    static int32_t writeNumber(
        const SimpleMicroProps& micros,
        DecimalQuantity& quantity,
        FormattedStringBuilder& string,
        int32_t index,
        UErrorCode& status);

    /**
     * Adds the affixes.  Intended to be called immediately after formatNumber.
     */
    static int32_t writeAffixes(
        const MicroProps& micros,
        FormattedStringBuilder& string,
        int32_t start,
        int32_t end,
        UErrorCode& status);

  private:
    // Head of the MicroPropsGenerator linked list. Subclasses' processQuantity
    // methods process this list in a parent-first order, such that the last
    // item added, which this points to, typically has its logic executed last.
    const MicroPropsGenerator *fMicroPropsGenerator = nullptr;

    // Tail of the list:
    MicroProps fMicros;

    // Other fields possibly used by the number formatting pipeline:
    // TODO: Convert more of these LocalPointers to value objects to reduce the number of news?
    LocalPointer<const UsagePrefsHandler> fUsagePrefsHandler;
    LocalPointer<const UnitConversionHandler> fUnitConversionHandler;
    LocalPointer<const DecimalFormatSymbols> fSymbols;
    LocalPointer<const PluralRules> fRules;
    LocalPointer<const ParsedPatternInfo> fPatternInfo;
    LocalPointer<const ScientificHandler> fScientificHandler;
    LocalPointer<MutablePatternModifier> fPatternModifier;
    LocalPointer<ImmutablePatternModifier> fImmutablePatternModifier;
    LocalPointer<LongNameHandler> fLongNameHandler;
    // TODO: use a common base class that enables fLongNameHandler,
    // fLongNameMultiplexer, and fMixedUnitLongNameHandler to be merged into one
    // member?
    LocalPointer<MixedUnitLongNameHandler> fMixedUnitLongNameHandler;
    LocalPointer<const LongNameMultiplexer> fLongNameMultiplexer;
    LocalPointer<const CompactHandler> fCompactHandler;

    NumberFormatterImpl(const MacroProps &macros, bool safe, UErrorCode &status);

    MicroProps& preProcessUnsafe(DecimalQuantity &inValue, UErrorCode &status);

    int32_t getPrefixSuffixUnsafe(Signum signum, StandardPlural::Form plural,
                                  FormattedStringBuilder& outString, UErrorCode& status);

    /**
     * If rulesPtr is non-null, return it.  Otherwise, return a PluralRules owned by this object for the
     * specified locale, creating it if necessary.
     */
    const PluralRules *
    resolvePluralRules(const PluralRules *rulesPtr, const Locale &locale, UErrorCode &status);

    /**
     * Synthesizes the MacroProps into a MicroPropsGenerator. All information, including the locale, is encoded into the
     * MicroPropsGenerator, except for the quantity itself, which is left abstract and must be provided to the returned
     * MicroPropsGenerator instance.
     *
     * @see MicroPropsGenerator
     * @param macros
     *            The {@link MacroProps} to consume. This method does not mutate the MacroProps instance.
     * @param safe
     *            If true, the returned MicroPropsGenerator will be thread-safe. If false, the returned value will
     *            <em>not</em> be thread-safe, intended for a single "one-shot" use only. Building the thread-safe
     *            object is more expensive.
     */
    const MicroPropsGenerator *
    macrosToMicroGenerator(const MacroProps &macros, bool safe, UErrorCode &status);

    static int32_t
    writeIntegerDigits(
        const SimpleMicroProps& micros,
        DecimalQuantity &quantity,
        FormattedStringBuilder &string,
        int32_t index,
        UErrorCode &status);

    static int32_t
    writeFractionDigits(
        const SimpleMicroProps& micros,
        DecimalQuantity &quantity,
        FormattedStringBuilder &string,
        int32_t index,
        UErrorCode &status);
};

} // namespace number::impl
U_NAMESPACE_END


#endif //__NUMBER_FORMATIMPL_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
