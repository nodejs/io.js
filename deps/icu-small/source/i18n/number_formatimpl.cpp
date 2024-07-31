// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "cstring.h"
#include "unicode/ures.h"
#include "uresimp.h"
#include "charstr.h"
#include "number_formatimpl.h"
#include "unicode/numfmt.h"
#include "number_patternstring.h"
#include "number_utils.h"
#include "unicode/numberformatter.h"
#include "unicode/dcfmtsym.h"
#include "number_scientific.h"
#include "number_compact.h"
#include "uresimp.h"
#include "ureslocs.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;


NumberFormatterImpl::NumberFormatterImpl(const MacroProps& macros, UErrorCode& status)
    : NumberFormatterImpl(macros, true, status) {
}

int32_t NumberFormatterImpl::formatStatic(const MacroProps &macros, UFormattedNumberData *results,
                                          UErrorCode &status) {
    DecimalQuantity &inValue = results->quantity;
    FormattedStringBuilder &outString = results->getStringRef();
    NumberFormatterImpl impl(macros, false, status);
    MicroProps& micros = impl.preProcessUnsafe(inValue, status);
    if (U_FAILURE(status)) { return 0; }
    int32_t length = writeNumber(micros.simple, inValue, outString, 0, status);
    length += writeAffixes(micros, outString, 0, length, status);
    results->outputUnit = std::move(micros.outputUnit);
    results->gender = micros.gender;
    return length;
}

int32_t NumberFormatterImpl::getPrefixSuffixStatic(const MacroProps& macros, Signum signum,
                                                   StandardPlural::Form plural,
                                                   FormattedStringBuilder& outString, UErrorCode& status) {
    NumberFormatterImpl impl(macros, false, status);
    return impl.getPrefixSuffixUnsafe(signum, plural, outString, status);
}

// NOTE: C++ SPECIFIC DIFFERENCE FROM JAVA:
// The "safe" apply method uses a new MicroProps. In the MicroPropsGenerator, fMicros is copied into the new instance.
// The "unsafe" method simply re-uses fMicros, eliminating the extra copy operation.
// See MicroProps::processQuantity() for details.

int32_t NumberFormatterImpl::format(UFormattedNumberData *results, UErrorCode &status) const {
    DecimalQuantity &inValue = results->quantity;
    FormattedStringBuilder &outString = results->getStringRef();
    MicroProps micros;
    preProcess(inValue, micros, status);
    if (U_FAILURE(status)) { return 0; }
    int32_t length = writeNumber(micros.simple, inValue, outString, 0, status);
    length += writeAffixes(micros, outString, 0, length, status);
    results->outputUnit = std::move(micros.outputUnit);
    results->gender = micros.gender;
    return length;
}

void NumberFormatterImpl::preProcess(DecimalQuantity& inValue, MicroProps& microsOut,
                                     UErrorCode& status) const {
    if (U_FAILURE(status)) { return; }
    if (fMicroPropsGenerator == nullptr) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return;
    }
    fMicroPropsGenerator->processQuantity(inValue, microsOut, status);
    microsOut.integerWidth.apply(inValue, status);
}

MicroProps& NumberFormatterImpl::preProcessUnsafe(DecimalQuantity& inValue, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return fMicros; // must always return a value
    }
    if (fMicroPropsGenerator == nullptr) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return fMicros; // must always return a value
    }
    fMicroPropsGenerator->processQuantity(inValue, fMicros, status);
    fMicros.integerWidth.apply(inValue, status);
    return fMicros;
}

int32_t NumberFormatterImpl::getPrefixSuffix(Signum signum, StandardPlural::Form plural,
                                             FormattedStringBuilder& outString, UErrorCode& status) const {
    if (U_FAILURE(status)) { return 0; }
    // #13453: DecimalFormat wants the affixes from the pattern only (modMiddle, aka pattern modifier).
    // Safe path: use fImmutablePatternModifier.
    const Modifier* modifier = fImmutablePatternModifier->getModifier(signum, plural);
    modifier->apply(outString, 0, 0, status);
    if (U_FAILURE(status)) { return 0; }
    return modifier->getPrefixLength();
}

int32_t NumberFormatterImpl::getPrefixSuffixUnsafe(Signum signum, StandardPlural::Form plural,
                                                   FormattedStringBuilder& outString, UErrorCode& status) {
    if (U_FAILURE(status)) { return 0; }
    // #13453: DecimalFormat wants the affixes from the pattern only (modMiddle, aka pattern modifier).
    // Unsafe path: use fPatternModifier.
    fPatternModifier->setNumberProperties(signum, plural);
    fPatternModifier->apply(outString, 0, 0, status);
    if (U_FAILURE(status)) { return 0; }
    return fPatternModifier->getPrefixLength();
}

NumberFormatterImpl::NumberFormatterImpl(const MacroProps& macros, bool safe, UErrorCode& status) {
    fMicroPropsGenerator = macrosToMicroGenerator(macros, safe, status);
}

//////////

const MicroPropsGenerator*
NumberFormatterImpl::macrosToMicroGenerator(const MacroProps& macros, bool safe, UErrorCode& status) {
    if (U_FAILURE(status)) { return nullptr; }
    const MicroPropsGenerator* chain = &fMicros;

    // Check that macros is error-free before continuing.
    if (macros.copyErrorTo(status)) {
        return nullptr;
    }

    // TODO: Accept currency symbols from DecimalFormatSymbols?

    // Pre-compute a few values for efficiency.
    bool isCurrency = utils::unitIsCurrency(macros.unit);
    bool isBaseUnit = utils::unitIsBaseUnit(macros.unit);
    bool isPercent = utils::unitIsPercent(macros.unit);
    bool isPermille = utils::unitIsPermille(macros.unit);
    bool isCompactNotation = macros.notation.fType == Notation::NTN_COMPACT;
    bool isAccounting =
            macros.sign == UNUM_SIGN_ACCOUNTING ||
            macros.sign == UNUM_SIGN_ACCOUNTING_ALWAYS ||
            macros.sign == UNUM_SIGN_ACCOUNTING_EXCEPT_ZERO ||
            macros.sign == UNUM_SIGN_ACCOUNTING_NEGATIVE;
    CurrencyUnit currency(u"", status);
    if (isCurrency) {
        currency = CurrencyUnit(macros.unit, status); // Restore CurrencyUnit from MeasureUnit
    }
    UNumberUnitWidth unitWidth = UNUM_UNIT_WIDTH_SHORT;
    if (macros.unitWidth != UNUM_UNIT_WIDTH_COUNT) {
        unitWidth = macros.unitWidth;
    }
    // Use CLDR unit data for all MeasureUnits (not currency and not
    // no-unit), except use the dedicated percent pattern for percent and
    // permille. However, use the CLDR unit data for percent/permille if a
    // long name was requested OR if compact notation is being used, since
    // compact notation overrides the middle modifier (micros.modMiddle)
    // normally used for the percent pattern.
    bool isCldrUnit = !isCurrency
        && !isBaseUnit
        && (unitWidth == UNUM_UNIT_WIDTH_FULL_NAME
            || !(isPercent || isPermille)
            || isCompactNotation
        );
    bool isMixedUnit = isCldrUnit && (uprv_strcmp(macros.unit.getType(), "") == 0) &&
                       macros.unit.getComplexity(status) == UMEASURE_UNIT_MIXED;

    // Select the numbering system.
    LocalPointer<const NumberingSystem> nsLocal;
    const NumberingSystem* ns;
    if (macros.symbols.isNumberingSystem()) {
        ns = macros.symbols.getNumberingSystem();
    } else {
        // TODO: Is there a way to avoid creating the NumberingSystem object?
        ns = NumberingSystem::createInstance(macros.locale, status);
        // Give ownership to the function scope.
        nsLocal.adoptInstead(ns);
    }
    const char* nsName = U_SUCCESS(status) ? ns->getName() : "latn";
    uprv_strncpy(fMicros.nsName, nsName, 8);
    fMicros.nsName[8] = 0; // guarantee NUL-terminated

    // Default gender: none.
    fMicros.gender = "";

    // Resolve the symbols. Do this here because currency may need to customize them.
    if (macros.symbols.isDecimalFormatSymbols()) {
        fMicros.simple.symbols = macros.symbols.getDecimalFormatSymbols();
    } else {
        LocalPointer<DecimalFormatSymbols> newSymbols(
            new DecimalFormatSymbols(macros.locale, *ns, status), status);
        if (U_FAILURE(status)) {
            return nullptr;
        }
        if (isCurrency) {
            newSymbols->setCurrency(currency.getISOCurrency(), status);
            if (U_FAILURE(status)) {
                return nullptr;
            }
        }
        fMicros.simple.symbols = newSymbols.getAlias();
        fSymbols.adoptInstead(newSymbols.orphan());
    }

    // Load and parse the pattern string. It is used for grouping sizes and affixes only.
    // If we are formatting currency, check for a currency-specific pattern.
    const char16_t* pattern = nullptr;
    if (isCurrency && fMicros.simple.symbols->getCurrencyPattern() != nullptr) {
        pattern = fMicros.simple.symbols->getCurrencyPattern();
    }
    if (pattern == nullptr) {
        CldrPatternStyle patternStyle;
        if (isCldrUnit) {
            patternStyle = CLDR_PATTERN_STYLE_DECIMAL;
        } else if (isPercent || isPermille) {
            patternStyle = CLDR_PATTERN_STYLE_PERCENT;
        } else if (!isCurrency || unitWidth == UNUM_UNIT_WIDTH_FULL_NAME) {
            patternStyle = CLDR_PATTERN_STYLE_DECIMAL;
        } else if (isAccounting) {
            // NOTE: Although ACCOUNTING and ACCOUNTING_ALWAYS are only supported in currencies right now,
            // the API contract allows us to add support to other units in the future.
            patternStyle = CLDR_PATTERN_STYLE_ACCOUNTING;
        } else {
            patternStyle = CLDR_PATTERN_STYLE_CURRENCY;
        }
        pattern = utils::getPatternForStyle(macros.locale, nsName, patternStyle, status);
        if (U_FAILURE(status)) {
            return nullptr;
        }
    }
    auto* patternInfo = new ParsedPatternInfo();
    if (patternInfo == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    fPatternInfo.adoptInstead(patternInfo);
    PatternParser::parseToPatternInfo(UnicodeString(pattern), *patternInfo, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    /////////////////////////////////////////////////////////////////////////////////////
    /// START POPULATING THE DEFAULT MICROPROPS AND BUILDING THE MICROPROPS GENERATOR ///
    /////////////////////////////////////////////////////////////////////////////////////

    // Unit Preferences and Conversions as our first step
    if (macros.usage.isSet()) {
        if (!isCldrUnit) {
            // We only support "usage" when the input unit is specified, and is
            // a CLDR Unit.
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return nullptr;
        }
        auto* usagePrefsHandler =
            new UsagePrefsHandler(macros.locale, macros.unit, macros.usage.fValue, chain, status);
        fUsagePrefsHandler.adoptInsteadAndCheckErrorCode(usagePrefsHandler, status);
        chain = fUsagePrefsHandler.getAlias();
    } else if (isMixedUnit) {
        auto* unitConversionHandler = new UnitConversionHandler(macros.unit, chain, status);
        fUnitConversionHandler.adoptInsteadAndCheckErrorCode(unitConversionHandler, status);
        chain = fUnitConversionHandler.getAlias();
    }

    // Multiplier
    if (macros.scale.isValid()) {
        fMicros.helpers.multiplier.setAndChain(macros.scale, chain);
        chain = &fMicros.helpers.multiplier;
    }

    // Rounding strategy
    Precision precision;
    if (!macros.precision.isBogus()) {
        precision = macros.precision;
    } else if (isCompactNotation) {
        precision = Precision::integer().withMinDigits(2);
    } else if (isCurrency) {
        precision = Precision::currency(UCURR_USAGE_STANDARD);
    } else if (macros.usage.isSet()) {
        // Bogus Precision - it will get set in the UsagePrefsHandler instead
        precision = Precision();
    } else {
        precision = Precision::maxFraction(6);
    }
    UNumberFormatRoundingMode roundingMode;
    roundingMode = macros.roundingMode;
    fMicros.rounder = {precision, roundingMode, currency, status};
    if (U_FAILURE(status)) {
        return nullptr;
    }

    // Grouping strategy
    if (!macros.grouper.isBogus()) {
        fMicros.simple.grouping = macros.grouper;
    } else if (isCompactNotation) {
        // Compact notation uses minGrouping by default since ICU 59
        fMicros.simple.grouping = Grouper::forStrategy(UNUM_GROUPING_MIN2);
    } else {
        fMicros.simple.grouping = Grouper::forStrategy(UNUM_GROUPING_AUTO);
    }
    fMicros.simple.grouping.setLocaleData(*fPatternInfo, macros.locale);

    // Padding strategy
    if (!macros.padder.isBogus()) {
        fMicros.padding = macros.padder;
    } else {
        fMicros.padding = Padder::none();
    }

    // Integer width
    if (!macros.integerWidth.isBogus()) {
        fMicros.integerWidth = macros.integerWidth;
    } else {
        fMicros.integerWidth = IntegerWidth::standard();
    }

    // Sign display
    if (macros.sign != UNUM_SIGN_COUNT) {
        fMicros.sign = macros.sign;
    } else {
        fMicros.sign = UNUM_SIGN_AUTO;
    }

    // Decimal mark display
    if (macros.decimal != UNUM_DECIMAL_SEPARATOR_COUNT) {
        fMicros.simple.decimal = macros.decimal;
    } else {
        fMicros.simple.decimal = UNUM_DECIMAL_SEPARATOR_AUTO;
    }

    // Use monetary separator symbols
    fMicros.simple.useCurrency = isCurrency;

    // Inner modifier (scientific notation)
    if (macros.notation.fType == Notation::NTN_SCIENTIFIC) {
        auto* newScientificHandler =
            new ScientificHandler(&macros.notation, fMicros.simple.symbols, chain);
        if (newScientificHandler == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        fScientificHandler.adoptInstead(newScientificHandler);
        chain = fScientificHandler.getAlias();
    } else {
        // No inner modifier required
        fMicros.modInner = &fMicros.helpers.emptyStrongModifier;
    }

    // Middle modifier (patterns, positive/negative, currency symbols, percent)
    auto* patternModifier = new MutablePatternModifier(false);
    if (patternModifier == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    fPatternModifier.adoptInstead(patternModifier);
    const AffixPatternProvider* affixProvider =
        macros.affixProvider != nullptr && (
                // For more information on this condition, see ICU-22073
                !isCompactNotation || isCurrency == macros.affixProvider->hasCurrencySign())
            ? macros.affixProvider
            : static_cast<const AffixPatternProvider*>(fPatternInfo.getAlias());
    patternModifier->setPatternInfo(affixProvider, kUndefinedField);
    patternModifier->setPatternAttributes(fMicros.sign, isPermille, macros.approximately);
    if (patternModifier->needsPlurals()) {
        patternModifier->setSymbols(
                fMicros.simple.symbols,
                currency,
                unitWidth,
                resolvePluralRules(macros.rules, macros.locale, status),
                status);
    } else {
        patternModifier->setSymbols(fMicros.simple.symbols, currency, unitWidth, nullptr, status);
    }
    if (safe) {
        fImmutablePatternModifier.adoptInsteadAndCheckErrorCode(patternModifier->createImmutable(status),
                                                                status);
    }
    if (U_FAILURE(status)) {
        return nullptr;
    }

    // currencyAsDecimal
    if (affixProvider->currencyAsDecimal()) {
        fMicros.simple.currencyAsDecimal = patternModifier->getCurrencySymbolForUnitWidth(status);
    }

    // Outer modifier (CLDR units and currency long names)
    if (isCldrUnit) {
        const char *unitDisplayCase = "";
        if (macros.unitDisplayCase.isSet()) {
            unitDisplayCase = macros.unitDisplayCase.fValue;
        }
        if (macros.usage.isSet()) {
            fLongNameMultiplexer.adoptInsteadAndCheckErrorCode(
                LongNameMultiplexer::forMeasureUnits(
                    macros.locale, *fUsagePrefsHandler->getOutputUnits(), unitWidth, unitDisplayCase,
                    resolvePluralRules(macros.rules, macros.locale, status), chain, status),
                status);
            chain = fLongNameMultiplexer.getAlias();
        } else if (isMixedUnit) {
            fMixedUnitLongNameHandler.adoptInsteadAndCheckErrorCode(new MixedUnitLongNameHandler(),
                                                                    status);
            MixedUnitLongNameHandler::forMeasureUnit(
                macros.locale, macros.unit, unitWidth, unitDisplayCase,
                resolvePluralRules(macros.rules, macros.locale, status), chain,
                fMixedUnitLongNameHandler.getAlias(), status);
            chain = fMixedUnitLongNameHandler.getAlias();
        } else {
            MeasureUnit unit = macros.unit;
            if (!utils::unitIsBaseUnit(macros.perUnit)) {
                unit = unit.product(macros.perUnit.reciprocal(status), status);
                // This isn't strictly necessary, but was what we specced out
                // when perUnit became a backward-compatibility thing:
                // unit/perUnit use case is only valid if both units are
                // built-ins, or the product is a built-in.
                if (uprv_strcmp(unit.getType(), "") == 0 &&
                    (uprv_strcmp(macros.unit.getType(), "") == 0 ||
                     uprv_strcmp(macros.perUnit.getType(), "") == 0)) {
                    status = U_UNSUPPORTED_ERROR;
                    return nullptr;
                }
            }
            fLongNameHandler.adoptInsteadAndCheckErrorCode(new LongNameHandler(), status);
            LongNameHandler::forMeasureUnit(macros.locale, unit, unitWidth, unitDisplayCase,
                                            resolvePluralRules(macros.rules, macros.locale, status),
                                            chain, fLongNameHandler.getAlias(), status);
            chain = fLongNameHandler.getAlias();
        }
    } else if (isCurrency && unitWidth == UNUM_UNIT_WIDTH_FULL_NAME) {
        fLongNameHandler.adoptInsteadAndCheckErrorCode(
            LongNameHandler::forCurrencyLongNames(
                macros.locale, currency, resolvePluralRules(macros.rules, macros.locale, status), chain,
                status),
            status);
        chain = fLongNameHandler.getAlias();
    } else {
        // No outer modifier required
        fMicros.modOuter = &fMicros.helpers.emptyWeakModifier;
    }
    if (U_FAILURE(status)) {
        return nullptr;
    }

    // Compact notation
    if (isCompactNotation) {
        CompactType compactType = (isCurrency && unitWidth != UNUM_UNIT_WIDTH_FULL_NAME)
                                  ? CompactType::TYPE_CURRENCY : CompactType::TYPE_DECIMAL;
        auto* newCompactHandler = new CompactHandler(
            macros.notation.fUnion.compactStyle,
            macros.locale,
            nsName,
            compactType,
            resolvePluralRules(macros.rules, macros.locale, status),
            patternModifier,
            safe,
            chain,
            status);
        if (U_FAILURE(status)) {
            return nullptr;
        }
        if (newCompactHandler == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        fCompactHandler.adoptInstead(newCompactHandler);
        chain = fCompactHandler.getAlias();
    }
    if (U_FAILURE(status)) {
        return nullptr;
    }

    // Always add the pattern modifier as the last element of the chain.
    if (safe) {
        fImmutablePatternModifier->addToChain(chain);
        chain = fImmutablePatternModifier.getAlias();
    } else {
        patternModifier->addToChain(chain);
        chain = patternModifier;
    }

    return chain;
}

const PluralRules*
NumberFormatterImpl::resolvePluralRules(
        const PluralRules* rulesPtr,
        const Locale& locale,
        UErrorCode& status) {
    if (rulesPtr != nullptr) {
        return rulesPtr;
    }
    // Lazily create PluralRules
    if (fRules.isNull()) {
        fRules.adoptInstead(PluralRules::forLocale(locale, status));
    }
    return fRules.getAlias();
}

int32_t NumberFormatterImpl::writeAffixes(
        const MicroProps& micros,
        FormattedStringBuilder& string,
        int32_t start,
        int32_t end,
        UErrorCode& status) {
    U_ASSERT(micros.modOuter != nullptr);
    // Always apply the inner modifier (which is "strong").
    int32_t length = micros.modInner->apply(string, start, end, status);
    if (micros.padding.isValid()) {
        length += micros.padding
                .padAndApply(*micros.modMiddle, *micros.modOuter, string, start, length + end, status);
    } else {
        length += micros.modMiddle->apply(string, start, length + end, status);
        length += micros.modOuter->apply(string, start, length + end, status);
    }
    return length;
}

int32_t NumberFormatterImpl::writeNumber(
        const SimpleMicroProps& micros,
        DecimalQuantity& quantity,
        FormattedStringBuilder& string,
        int32_t index,
        UErrorCode& status) {
    int32_t length = 0;
    if (quantity.isInfinite()) {
        length += string.insert(
                length + index,
                micros.symbols->getSymbol(DecimalFormatSymbols::ENumberFormatSymbol::kInfinitySymbol),
                {UFIELD_CATEGORY_NUMBER, UNUM_INTEGER_FIELD},
                status);

    } else if (quantity.isNaN()) {
        length += string.insert(
                length + index,
                micros.symbols->getSymbol(DecimalFormatSymbols::ENumberFormatSymbol::kNaNSymbol),
                {UFIELD_CATEGORY_NUMBER, UNUM_INTEGER_FIELD},
                status);

    } else {
        // Add the integer digits
        length += writeIntegerDigits(
            micros,
            quantity,
            string,
            length + index,
            status);

        // Add the decimal point
        if (quantity.getLowerDisplayMagnitude() < 0 || micros.decimal == UNUM_DECIMAL_SEPARATOR_ALWAYS) {
            if (!micros.currencyAsDecimal.isBogus()) {
                length += string.insert(
                    length + index,
                    micros.currencyAsDecimal,
                    {UFIELD_CATEGORY_NUMBER, UNUM_CURRENCY_FIELD},
                    status);
            } else if (micros.useCurrency) {
                length += string.insert(
                    length + index,
                    micros.symbols->getSymbol(
                        DecimalFormatSymbols::ENumberFormatSymbol::kMonetarySeparatorSymbol),
                    {UFIELD_CATEGORY_NUMBER, UNUM_DECIMAL_SEPARATOR_FIELD},
                    status);
            } else {
                length += string.insert(
                    length + index,
                    micros.symbols->getSymbol(
                        DecimalFormatSymbols::ENumberFormatSymbol::kDecimalSeparatorSymbol),
                    {UFIELD_CATEGORY_NUMBER, UNUM_DECIMAL_SEPARATOR_FIELD},
                    status);
            }
        }

        // Add the fraction digits
        length += writeFractionDigits(micros, quantity, string, length + index, status);

        if (length == 0) {
            // Force output of the digit for value 0
            length += utils::insertDigitFromSymbols(
                    string,
                    index,
                    0,
                    *micros.symbols,
                    {UFIELD_CATEGORY_NUMBER, UNUM_INTEGER_FIELD},
                    status);
        }
    }

    return length;
}

int32_t NumberFormatterImpl::writeIntegerDigits(
        const SimpleMicroProps& micros,
        DecimalQuantity& quantity,
        FormattedStringBuilder& string,
        int32_t index,
        UErrorCode& status) {
    int length = 0;
    int integerCount = quantity.getUpperDisplayMagnitude() + 1;
    for (int i = 0; i < integerCount; i++) {
        // Add grouping separator
        if (micros.grouping.groupAtPosition(i, quantity)) {
            length += string.insert(
                    index,
                    micros.useCurrency ? micros.symbols->getSymbol(
                            DecimalFormatSymbols::ENumberFormatSymbol::kMonetaryGroupingSeparatorSymbol)
                                       : micros.symbols->getSymbol(
                            DecimalFormatSymbols::ENumberFormatSymbol::kGroupingSeparatorSymbol),
                    {UFIELD_CATEGORY_NUMBER, UNUM_GROUPING_SEPARATOR_FIELD},
                    status);
        }

        // Get and append the next digit value
        int8_t nextDigit = quantity.getDigit(i);
        length += utils::insertDigitFromSymbols(
                string,
                index,
                nextDigit,
                *micros.symbols,
                {UFIELD_CATEGORY_NUMBER,
                UNUM_INTEGER_FIELD},
                status);
    }
    return length;
}

int32_t NumberFormatterImpl::writeFractionDigits(
        const SimpleMicroProps& micros,
        DecimalQuantity& quantity,
        FormattedStringBuilder& string,
        int32_t index,
        UErrorCode& status) {
    int length = 0;
    int fractionCount = -quantity.getLowerDisplayMagnitude();
    for (int i = 0; i < fractionCount; i++) {
        // Get and append the next digit value
        int8_t nextDigit = quantity.getDigit(-i - 1);
        length += utils::insertDigitFromSymbols(
                string,
                length + index,
                nextDigit,
                *micros.symbols,
                {UFIELD_CATEGORY_NUMBER, UNUM_FRACTION_FIELD},
                status);
    }
    return length;
}

#endif /* #if !UCONFIG_NO_FORMATTING */
