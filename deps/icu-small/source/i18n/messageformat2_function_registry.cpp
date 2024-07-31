// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "unicode/dtptngen.h"
#include "unicode/messageformat2_data_model_names.h"
#include "unicode/messageformat2_function_registry.h"
#include "unicode/smpdtfmt.h"
#include "messageformat2_allocation.h"
#include "messageformat2_function_registry_internal.h"
#include "messageformat2_macros.h"
#include "hash.h"
#include "number_types.h"
#include "uvector.h" // U_ASSERT

#include <inttypes.h>
#include <math.h>

U_NAMESPACE_BEGIN

namespace message2 {

// Function registry implementation

Formatter::~Formatter() {}
Selector::~Selector() {}
FormatterFactory::~FormatterFactory() {}
SelectorFactory::~SelectorFactory() {}

MFFunctionRegistry MFFunctionRegistry::Builder::build() {
    U_ASSERT(formatters != nullptr && selectors != nullptr && formattersByType != nullptr);
    MFFunctionRegistry result = MFFunctionRegistry(formatters, selectors, formattersByType);
    formatters = nullptr;
    selectors = nullptr;
    formattersByType = nullptr;
    return result;
}

MFFunctionRegistry::Builder& MFFunctionRegistry::Builder::adoptSelector(const FunctionName& selectorName, SelectorFactory* selectorFactory, UErrorCode& errorCode) {
    if (U_SUCCESS(errorCode)) {
        U_ASSERT(selectors != nullptr);
        selectors->put(selectorName, selectorFactory, errorCode);
    }
    return *this;
}

MFFunctionRegistry::Builder& MFFunctionRegistry::Builder::adoptFormatter(const FunctionName& formatterName, FormatterFactory* formatterFactory, UErrorCode& errorCode) {
    if (U_SUCCESS(errorCode)) {
        U_ASSERT(formatters != nullptr);
        formatters->put(formatterName, formatterFactory, errorCode);
    }
    return *this;
}

MFFunctionRegistry::Builder& MFFunctionRegistry::Builder::setDefaultFormatterNameByType(const UnicodeString& type, const FunctionName& functionName, UErrorCode& errorCode) {
    if (U_SUCCESS(errorCode)) {
        U_ASSERT(formattersByType != nullptr);
        FunctionName* f = create<FunctionName>(FunctionName(functionName), errorCode);
        formattersByType->put(type, f, errorCode);
    }
    return *this;
}

MFFunctionRegistry::Builder::Builder(UErrorCode& errorCode) {
    CHECK_ERROR(errorCode);

    formatters = new Hashtable();
    selectors = new Hashtable();
    formattersByType = new Hashtable();
    if (!(formatters != nullptr && selectors != nullptr && formattersByType != nullptr)) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
    }
    formatters->setValueDeleter(uprv_deleteUObject);
    selectors->setValueDeleter(uprv_deleteUObject);
    formattersByType->setValueDeleter(uprv_deleteUObject);
}

MFFunctionRegistry::Builder::~Builder() {
    if (formatters != nullptr) {
        delete formatters;
    }
    if (selectors != nullptr) {
        delete selectors;
    }
    if (formattersByType != nullptr) {
        delete formattersByType;
    }
}

// Returns non-owned pointer. Returns pointer rather than reference because it can fail.
// Returns non-const because FormatterFactory is mutable.
// TODO: This is unsafe because of the cached-formatters map
// (the caller could delete the resulting pointer)
FormatterFactory* MFFunctionRegistry::getFormatter(const FunctionName& formatterName) const {
    U_ASSERT(formatters != nullptr);
    return static_cast<FormatterFactory*>(formatters->get(formatterName));
}

UBool MFFunctionRegistry::getDefaultFormatterNameByType(const UnicodeString& type, FunctionName& name) const {
    U_ASSERT(formatters != nullptr);
    const FunctionName* f = static_cast<FunctionName*>(formattersByType->get(type));
    if (f != nullptr) {
        name = *f;
        return true;
    }
    return false;
}

const SelectorFactory* MFFunctionRegistry::getSelector(const FunctionName& selectorName) const {
    U_ASSERT(selectors != nullptr);
    return static_cast<const SelectorFactory*>(selectors->get(selectorName));
}

bool MFFunctionRegistry::hasFormatter(const FunctionName& f) const {
    return getFormatter(f) != nullptr;
}

bool MFFunctionRegistry::hasSelector(const FunctionName& s) const {
    return getSelector(s) != nullptr;
}

void MFFunctionRegistry::checkFormatter(const char* s) const {
#if U_DEBUG
    U_ASSERT(hasFormatter(FunctionName(UnicodeString(s))));
#else
   (void) s;
#endif
}

void MFFunctionRegistry::checkSelector(const char* s) const {
#if U_DEBUG
    U_ASSERT(hasSelector(FunctionName(UnicodeString(s))));
#else
    (void) s;
#endif
}

// Debugging
void MFFunctionRegistry::checkStandard() const {
    checkFormatter("datetime");
    checkFormatter("date");
    checkFormatter("time");
    checkFormatter("number");
    checkFormatter("integer");
    checkSelector("number");
    checkSelector("integer");
    checkSelector("string");
}

// Formatter/selector helpers

// Converts `s` to a double, indicating failure via `errorCode`
static void strToDouble(const UnicodeString& s, double& result, UErrorCode& errorCode) {
    CHECK_ERROR(errorCode);

    // Using en-US locale because it happens to correspond to the spec:
    // https://github.com/unicode-org/message-format-wg/blob/main/spec/registry.md#number-operands
    // Ideally, this should re-use the code for parsing number literals (Parser::parseUnquotedLiteral())
    // It's hard to reuse the same code because of how parse errors work.
    // TODO: Refactor
    LocalPointer<NumberFormat> numberFormat(NumberFormat::createInstance(Locale("en-US"), errorCode));
    CHECK_ERROR(errorCode);
    icu::Formattable asNumber;
    numberFormat->parse(s, asNumber, errorCode);
    CHECK_ERROR(errorCode);
    result = asNumber.getDouble(errorCode);
}

static double tryStringAsNumber(const Locale& locale, const Formattable& val, UErrorCode& errorCode) {
    // Check for a string option, try to parse it as a number if present
    UnicodeString tempString = val.getString(errorCode);
    LocalPointer<NumberFormat> numberFormat(NumberFormat::createInstance(locale, errorCode));
    if (U_SUCCESS(errorCode)) {
        icu::Formattable asNumber;
        numberFormat->parse(tempString, asNumber, errorCode);
        if (U_SUCCESS(errorCode)) {
            return asNumber.getDouble(errorCode);
        }
    }
    return 0;
}

static int64_t getInt64Value(const Locale& locale, const Formattable& value, UErrorCode& errorCode) {
    if (U_SUCCESS(errorCode)) {
        if (!value.isNumeric()) {
            double doubleResult = tryStringAsNumber(locale, value, errorCode);
            if (U_SUCCESS(errorCode)) {
                return static_cast<int64_t>(doubleResult);
            }
        }
        else {
            int64_t result = value.getInt64(errorCode);
            if (U_SUCCESS(errorCode)) {
                return result;
            }
        }
    }
    // Option was numeric but couldn't be converted to int64_t -- could be overflow
    return 0;
}

// Adopts its arguments
MFFunctionRegistry::MFFunctionRegistry(FormatterMap* f, SelectorMap* s, Hashtable* byType) : formatters(f), selectors(s), formattersByType(byType) {
    U_ASSERT(f != nullptr && s != nullptr && byType != nullptr);
}

MFFunctionRegistry& MFFunctionRegistry::operator=(MFFunctionRegistry&& other) noexcept {
    cleanup();

    formatters = other.formatters;
    selectors = other.selectors;
    formattersByType = other.formattersByType;
    other.formatters = nullptr;
    other.selectors = nullptr;
    other.formattersByType = nullptr;

    return *this;
}

void MFFunctionRegistry::cleanup() noexcept {
    if (formatters != nullptr) {
        delete formatters;
    }
    if (selectors != nullptr) {
        delete selectors;
    }
    if (formattersByType != nullptr) {
        delete formattersByType;
    }
}


MFFunctionRegistry::~MFFunctionRegistry() {
    cleanup();
}

// Specific formatter implementations

// --------- Number

/* static */ number::LocalizedNumberFormatter StandardFunctions::formatterForOptions(const Number& number,
                                                                                     const FunctionOptions& opts,
                                                                                     UErrorCode& status) {
    number::UnlocalizedNumberFormatter nf;

    using namespace number;

    if (U_SUCCESS(status)) {
        Formattable opt;
        nf = NumberFormatter::with();
        bool isInteger = number.isInteger;

        if (isInteger) {
            nf = nf.precision(Precision::integer());
        }

        // Notation options
        if (!isInteger) {
            // These options only apply to `:number`

            // Default notation is simple
            Notation notation = Notation::simple();
            UnicodeString notationOpt = opts.getStringFunctionOption(UnicodeString("notation"));
            if (notationOpt == UnicodeString("scientific")) {
                notation = Notation::scientific();
            } else if (notationOpt == UnicodeString("engineering")) {
                notation = Notation::engineering();
            } else if (notationOpt == UnicodeString("compact")) {
                UnicodeString displayOpt = opts.getStringFunctionOption(UnicodeString("compactDisplay"));
                if (displayOpt == UnicodeString("long")) {
                    notation = Notation::compactLong();
                } else {
                    // Default is short
                    notation = Notation::compactShort();
                }
            } else {
                // Already set to default
            }
            nf = nf.notation(notation);
        }

        // Style options -- specific to `:number`
        if (!isInteger) {
            if (number.usePercent(opts)) {
                nf = nf.unit(NoUnit::percent());
            }
        }

        int32_t maxSignificantDigits = number.maximumSignificantDigits(opts);
        if (!isInteger) {
            int32_t minFractionDigits = number.minimumFractionDigits(opts);
            int32_t maxFractionDigits = number.maximumFractionDigits(opts);
            int32_t minSignificantDigits = number.minimumSignificantDigits(opts);
            Precision p = Precision::minMaxFraction(minFractionDigits, maxFractionDigits);
            if (minSignificantDigits > 0) {
                p = p.minSignificantDigits(minSignificantDigits);
            }
            if (maxSignificantDigits > 0) {
                p = p.maxSignificantDigits(maxSignificantDigits);
            }
            nf = nf.precision(p);
        } else {
            // maxSignificantDigits applies to `:integer`, but the other precision options don't
            Precision p = Precision::integer();
            if (maxSignificantDigits > 0) {
                p = p.maxSignificantDigits(maxSignificantDigits);
            }
            nf = nf.precision(p);
        }

        // All other options apply to both `:number` and `:integer`
        int32_t minIntegerDigits = number.minimumIntegerDigits(opts);
        nf = nf.integerWidth(IntegerWidth::zeroFillTo(minIntegerDigits));

        // signDisplay
        UnicodeString sd = opts.getStringFunctionOption(UnicodeString("signDisplay"));
        UNumberSignDisplay signDisplay;
        if (sd == UnicodeString("always")) {
            signDisplay = UNumberSignDisplay::UNUM_SIGN_ALWAYS;
        } else if (sd == UnicodeString("exceptZero")) {
            signDisplay = UNumberSignDisplay::UNUM_SIGN_EXCEPT_ZERO;
        } else if (sd == UnicodeString("negative")) {
            signDisplay = UNumberSignDisplay::UNUM_SIGN_NEGATIVE;
        } else if (sd == UnicodeString("never")) {
            signDisplay = UNumberSignDisplay::UNUM_SIGN_NEVER;
        } else {
            signDisplay = UNumberSignDisplay::UNUM_SIGN_AUTO;
        }
        nf = nf.sign(signDisplay);

        // useGrouping
        UnicodeString ug = opts.getStringFunctionOption(UnicodeString("useGrouping"));
        UNumberGroupingStrategy grp;
        if (ug == UnicodeString("always")) {
            grp = UNumberGroupingStrategy::UNUM_GROUPING_ON_ALIGNED;
        } else if (ug == UnicodeString("never")) {
            grp = UNumberGroupingStrategy::UNUM_GROUPING_OFF;
        } else if (ug == UnicodeString("min2")) {
            grp = UNumberGroupingStrategy::UNUM_GROUPING_MIN2;
        } else {
            // Default is "auto"
            grp = UNumberGroupingStrategy::UNUM_GROUPING_AUTO;
        }
        nf = nf.grouping(grp);
    }
    return LocalizedNumberFormatter(nf.locale(number.locale));
}

Formatter* StandardFunctions::NumberFactory::createFormatter(const Locale& locale, UErrorCode& errorCode) {
    NULL_ON_ERROR(errorCode);

    Formatter* result = new Number(locale);
    if (result == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

Formatter* StandardFunctions::IntegerFactory::createFormatter(const Locale& locale, UErrorCode& errorCode) {
    NULL_ON_ERROR(errorCode);

    Formatter* result = new Number(Number::integer(locale));
    if (result == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

StandardFunctions::IntegerFactory::~IntegerFactory() {}

static FormattedPlaceholder notANumber(const FormattedPlaceholder& input) {
    return FormattedPlaceholder(input, FormattedValue(UnicodeString("NaN")));
}

static FormattedPlaceholder stringAsNumber(const number::LocalizedNumberFormatter& nf, const FormattedPlaceholder& input, UErrorCode& errorCode) {
    if (U_FAILURE(errorCode)) {
        return {};
    }

    double numberValue;
    // Copying string to avoid GCC dangling-reference warning
    // (although the reference is safe)
    UnicodeString inputStr = input.asFormattable().getString(errorCode);
    // Precondition: `input`'s source Formattable has type string
    if (U_FAILURE(errorCode)) {
        return {};
    }
    UErrorCode localErrorCode = U_ZERO_ERROR;
    strToDouble(inputStr, numberValue, localErrorCode);
    if (U_FAILURE(localErrorCode)) {
        errorCode = U_MF_OPERAND_MISMATCH_ERROR;
        return notANumber(input);
    }
    UErrorCode savedStatus = errorCode;
    number::FormattedNumber result = nf.formatDouble(numberValue, errorCode);
    // Ignore U_USING_DEFAULT_WARNING
    if (errorCode == U_USING_DEFAULT_WARNING) {
        errorCode = savedStatus;
    }
    return FormattedPlaceholder(input, FormattedValue(std::move(result)));
}

int32_t StandardFunctions::Number::maximumFractionDigits(const FunctionOptions& opts) const {
    Formattable opt;

    if (isInteger) {
        return 0;
    }

    if (opts.getFunctionOption(UnicodeString("maximumFractionDigits"), opt)) {
        UErrorCode localErrorCode = U_ZERO_ERROR;
        int64_t val = getInt64Value(locale, opt, localErrorCode);
        if (U_SUCCESS(localErrorCode)) {
            return static_cast<int32_t>(val);
        }
    }
    return number::impl::kMaxIntFracSig;
}

int32_t StandardFunctions::Number::minimumFractionDigits(const FunctionOptions& opts) const {
    Formattable opt;

    if (!isInteger) {
        if (opts.getFunctionOption(UnicodeString("minimumFractionDigits"), opt)) {
            UErrorCode localErrorCode = U_ZERO_ERROR;
            int64_t val = getInt64Value(locale, opt, localErrorCode);
            if (U_SUCCESS(localErrorCode)) {
                return static_cast<int32_t>(val);
            }
        }
    }
    return 0;
}

int32_t StandardFunctions::Number::minimumIntegerDigits(const FunctionOptions& opts) const {
    Formattable opt;

    if (opts.getFunctionOption(UnicodeString("minimumIntegerDigits"), opt)) {
        UErrorCode localErrorCode = U_ZERO_ERROR;
        int64_t val = getInt64Value(locale, opt, localErrorCode);
        if (U_SUCCESS(localErrorCode)) {
            return static_cast<int32_t>(val);
        }
    }
    return 0;
}

int32_t StandardFunctions::Number::minimumSignificantDigits(const FunctionOptions& opts) const {
    Formattable opt;

    if (!isInteger) {
        if (opts.getFunctionOption(UnicodeString("minimumSignificantDigits"), opt)) {
            UErrorCode localErrorCode = U_ZERO_ERROR;
            int64_t val = getInt64Value(locale, opt, localErrorCode);
            if (U_SUCCESS(localErrorCode)) {
                return static_cast<int32_t>(val);
            }
        }
    }
    // Returning 0 indicates that the option wasn't provided or was a non-integer.
    // The caller needs to check for that case, since passing 0 to Precision::minSignificantDigits()
    // is an error.
    return 0;
}

int32_t StandardFunctions::Number::maximumSignificantDigits(const FunctionOptions& opts) const {
    Formattable opt;

    if (opts.getFunctionOption(UnicodeString("maximumSignificantDigits"), opt)) {
        UErrorCode localErrorCode = U_ZERO_ERROR;
        int64_t val = getInt64Value(locale, opt, localErrorCode);
        if (U_SUCCESS(localErrorCode)) {
            return static_cast<int32_t>(val);
        }
    }
    // Returning 0 indicates that the option wasn't provided or was a non-integer.
    // The caller needs to check for that case, since passing 0 to Precision::maxSignificantDigits()
    // is an error.
    return 0; // Not a valid value for Precision; has to be checked
}

bool StandardFunctions::Number::usePercent(const FunctionOptions& opts) const {
    Formattable opt;
    if (isInteger
        || !opts.getFunctionOption(UnicodeString("style"), opt)
        || opt.getType() != UFMT_STRING) {
        return false;
    }
    UErrorCode localErrorCode = U_ZERO_ERROR;
    const UnicodeString& style = opt.getString(localErrorCode);
    U_ASSERT(U_SUCCESS(localErrorCode));
    return (style == UnicodeString("percent"));
}

/* static */ StandardFunctions::Number StandardFunctions::Number::integer(const Locale& loc) {
    return StandardFunctions::Number(loc, true);
}

FormattedPlaceholder StandardFunctions::Number::format(FormattedPlaceholder&& arg, FunctionOptions&& opts, UErrorCode& errorCode) const {
    if (U_FAILURE(errorCode)) {
        return {};
    }

    // No argument => return "NaN"
    if (!arg.canFormat()) {
        errorCode = U_MF_OPERAND_MISMATCH_ERROR;
        return notANumber(arg);
    }

    number::LocalizedNumberFormatter realFormatter;
    realFormatter = formatterForOptions(*this, opts, errorCode);

    number::FormattedNumber numberResult;
    if (U_SUCCESS(errorCode)) {
        // Already checked that contents can be formatted
        const Formattable& toFormat = arg.asFormattable();
        switch (toFormat.getType()) {
        case UFMT_DOUBLE: {
            double d = toFormat.getDouble(errorCode);
            U_ASSERT(U_SUCCESS(errorCode));
            numberResult = realFormatter.formatDouble(d, errorCode);
            break;
        }
        case UFMT_LONG: {
            int32_t l = toFormat.getLong(errorCode);
            U_ASSERT(U_SUCCESS(errorCode));
            numberResult = realFormatter.formatInt(l, errorCode);
            break;
        }
        case UFMT_INT64: {
            int64_t i = toFormat.getInt64(errorCode);
            U_ASSERT(U_SUCCESS(errorCode));
            numberResult = realFormatter.formatInt(i, errorCode);
            break;
        }
        case UFMT_STRING: {
            // Try to parse the string as a number
            return stringAsNumber(realFormatter, arg, errorCode);
        }
        default: {
            // Other types can't be parsed as a number
            errorCode = U_MF_OPERAND_MISMATCH_ERROR;
            return notANumber(arg);
        }
        }
    }

    return FormattedPlaceholder(arg, FormattedValue(std::move(numberResult)));
}

StandardFunctions::Number::~Number() {}
StandardFunctions::NumberFactory::~NumberFactory() {}

// --------- PluralFactory


StandardFunctions::Plural::PluralType StandardFunctions::Plural::pluralType(const FunctionOptions& opts) const {
    Formattable opt;

    if (opts.getFunctionOption(UnicodeString("select"), opt)) {
        UErrorCode localErrorCode = U_ZERO_ERROR;
        UnicodeString val = opt.getString(localErrorCode);
        if (U_SUCCESS(localErrorCode)) {
            if (val == UnicodeString("ordinal")) {
                return PluralType::PLURAL_ORDINAL;
            }
            if (val == UnicodeString("exact")) {
                return PluralType::PLURAL_EXACT;
            }
        }
    }
    return PluralType::PLURAL_CARDINAL;
}

Selector* StandardFunctions::PluralFactory::createSelector(const Locale& locale, UErrorCode& errorCode) const {
    NULL_ON_ERROR(errorCode);

    Selector* result;
    if (isInteger) {
        result = new Plural(Plural::integer(locale));
    } else {
        result = new Plural(locale);
    }
    if (result == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    return result;
}

static double tryAsString(const UnicodeString& s, UErrorCode& errorCode) {
    if (U_FAILURE(errorCode)) {
        return 0;
    }
    // Try parsing the inputString as a double
    double valToCheck;
    strToDouble(s, valToCheck, errorCode);
    return valToCheck;
}

static double tryWithFormattable(const Formattable& value, UErrorCode& errorCode) {
    if (U_FAILURE(errorCode)) {
        return 0;
    }
    double valToCheck;
    switch (value.getType()) {
        case UFMT_DOUBLE: {
            valToCheck = value.getDouble(errorCode);
            break;
        }
        case UFMT_LONG: {
            valToCheck = (double) value.getLong(errorCode);
            break;
        }
        case UFMT_INT64: {
            valToCheck = (double) value.getInt64(errorCode);
            break;
        }
        case UFMT_STRING: {
            const UnicodeString& s = value.getString(errorCode);
            U_ASSERT(U_SUCCESS(errorCode));
            return tryAsString(s, errorCode);
        }
        default: {
            errorCode = U_ILLEGAL_ARGUMENT_ERROR;
            return 0;
        }
    }
    U_ASSERT(U_SUCCESS(errorCode));
    return valToCheck;
}

static UnicodeString toJSONString(double d) {
    // TODO :(
    char buffer[512];
    // "Only integer matching is required in the Technical Preview."
    snprintf(buffer, 512, "%" PRId64, static_cast<int64_t>(d));
    return UnicodeString(buffer);
}

void StandardFunctions::Plural::selectKey(FormattedPlaceholder&& toFormat,
                                          FunctionOptions&& opts,
                                          const UnicodeString* keys,
                                          int32_t keysLen,
                                          UnicodeString* prefs,
                                          int32_t& prefsLen,
					  UErrorCode& errorCode) const {
    CHECK_ERROR(errorCode);

    // No argument => return "NaN"
    if (!toFormat.canFormat()) {
        errorCode = U_MF_SELECTOR_ERROR;
        return;
    }

    // Only doubles and integers can match
    double valToCheck;

    bool isFormattedString = toFormat.isEvaluated() && toFormat.output().isString();
    bool isFormattedNumber = toFormat.isEvaluated() && toFormat.output().isNumber();

    if (isFormattedString) {
        // Formatted string: try parsing it as a number
        valToCheck = tryAsString(toFormat.output().getString(), errorCode);
    } else {
        // Already checked that contents can be formatted
        valToCheck = tryWithFormattable(toFormat.asFormattable(), errorCode);
    }

    if (U_FAILURE(errorCode)) {
        // Non-number => selector error
        errorCode = U_MF_SELECTOR_ERROR;
        return;
    }
    // TODO: This needs to be checked against https://github.com/unicode-org/message-format-wg/blob/main/spec/registry.md#number-selection
    // Determine `exact`, per step 1 under "Number Selection"
    UnicodeString exact = toJSONString(valToCheck);

    // Generate the matches
    // -----------------------

    prefsLen = 0;

    // First, check for an exact match
    double keyAsDouble = 0;
    for (int32_t i = 0; i < keysLen; i++) {
        // Try parsing the key as a double
        UErrorCode localErrorCode = U_ZERO_ERROR;
        strToDouble(keys[i], keyAsDouble, localErrorCode);
        if (U_SUCCESS(localErrorCode)) {
            if (exact == keys[i]) {
		prefs[prefsLen] = keys[i];
                prefsLen++;
                break;
            }
        }
    }

    PluralType type = pluralType(opts);
    // Return immediately if exact matching was requested
    if (prefsLen == keysLen || type == PluralType::PLURAL_EXACT) {
        return;
    }

    UPluralType t = type == PluralType::PLURAL_ORDINAL ? UPLURAL_TYPE_ORDINAL : UPLURAL_TYPE_CARDINAL;
    // Look up plural rules by locale and type
    LocalPointer<PluralRules> rules(PluralRules::forLocale(locale, t, errorCode));
    CHECK_ERROR(errorCode);


    // Check for a match based on the plural category
    UnicodeString match;
    if (isFormattedNumber) {
        match = rules->select(toFormat.output().getNumber(), errorCode);
    } else {
        if (isInteger) {
            match = rules->select(static_cast<int32_t>(trunc(valToCheck)));
        } else {
            match = rules->select(valToCheck);
        }
    }
    CHECK_ERROR(errorCode);

    for (int32_t i = 0; i < keysLen; i ++) {
        if (prefsLen >= keysLen) {
            break;
        }
        if (match == keys[i]) {
            prefs[prefsLen] = keys[i];
            prefsLen++;
        }
    }
}

StandardFunctions::Plural::~Plural() {}
StandardFunctions::PluralFactory::~PluralFactory() {}

// --------- DateTimeFactory

/* static */ UnicodeString StandardFunctions::getStringOption(const FunctionOptions& opts,
                                                              const UnicodeString& optionName,
                                                              UErrorCode& errorCode) {
    if (U_SUCCESS(errorCode)) {
        Formattable opt;
        if (opts.getFunctionOption(optionName, opt)) {
            return opt.getString(errorCode); // In case it's not a string, error code will be set
        } else {
            errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        }
    }
    // Default is empty string
    return {};
}

// Date/time options only
static UnicodeString defaultForOption(const UnicodeString& optionName) {
    if (optionName == UnicodeString("dateStyle")
        || optionName == UnicodeString("timeStyle")
        || optionName == UnicodeString("style")) {
        return UnicodeString("short");
    }
    return {}; // Empty string is default
}

// TODO
// Only DateTime currently uses the function options stored in the placeholder.
// It also doesn't use them very consistently (it looks at the previous set of options,
// and others aren't preserved). This needs to be generalized,
// but that depends on https://github.com/unicode-org/message-format-wg/issues/515
// Finally, the option value is assumed to be a string,
// which works for datetime options but not necessarily in general.
UnicodeString StandardFunctions::DateTime::getFunctionOption(const FormattedPlaceholder& toFormat,
                                                             const FunctionOptions& opts,
                                                             const UnicodeString& optionName) const {
    // Options passed to the current function invocation take priority
    Formattable opt;
    UnicodeString s;
    UErrorCode localErrorCode = U_ZERO_ERROR;
    s = getStringOption(opts, optionName, localErrorCode);
    if (U_SUCCESS(localErrorCode)) {
        return s;
    }
    // Next try the set of options used to construct `toFormat`
    localErrorCode = U_ZERO_ERROR;
    s = getStringOption(toFormat.options(), optionName, localErrorCode);
    if (U_SUCCESS(localErrorCode)) {
        return s;
    }
    // Finally, use default
    return defaultForOption(optionName);
}

// Used for options that don't have defaults
UnicodeString StandardFunctions::DateTime::getFunctionOption(const FormattedPlaceholder& toFormat,
                                                             const FunctionOptions& opts,
                                                             const UnicodeString& optionName,
                                                             UErrorCode& errorCode) const {
    if (U_SUCCESS(errorCode)) {
        // Options passed to the current function invocation take priority
        Formattable opt;
        UnicodeString s;
        UErrorCode localErrorCode = U_ZERO_ERROR;
        s = getStringOption(opts, optionName, localErrorCode);
        if (U_SUCCESS(localErrorCode)) {
            return s;
        }
        // Next try the set of options used to construct `toFormat`
        localErrorCode = U_ZERO_ERROR;
        s = getStringOption(toFormat.options(), optionName, localErrorCode);
        if (U_SUCCESS(localErrorCode)) {
            return s;
        }
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
    }
    return {};
}

static DateFormat::EStyle stringToStyle(UnicodeString option, UErrorCode& errorCode) {
    if (U_SUCCESS(errorCode)) {
        UnicodeString upper = option.toUpper();
        if (upper == UnicodeString("FULL")) {
            return DateFormat::EStyle::kFull;
        }
        if (upper == UnicodeString("LONG")) {
            return DateFormat::EStyle::kLong;
        }
        if (upper == UnicodeString("MEDIUM")) {
            return DateFormat::EStyle::kMedium;
        }
        if (upper == UnicodeString("SHORT")) {
            return DateFormat::EStyle::kShort;
        }
        if (upper.isEmpty() || upper == UnicodeString("DEFAULT")) {
            return DateFormat::EStyle::kDefault;
        }
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
    }
    return DateFormat::EStyle::kNone;
}

/* static */ StandardFunctions::DateTimeFactory* StandardFunctions::DateTimeFactory::dateTime(UErrorCode& errorCode) {
    NULL_ON_ERROR(errorCode);

    DateTimeFactory* result = new StandardFunctions::DateTimeFactory(DateTimeType::DateTime);
    if (result == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

/* static */ StandardFunctions::DateTimeFactory* StandardFunctions::DateTimeFactory::date(UErrorCode& errorCode) {
    NULL_ON_ERROR(errorCode);

    DateTimeFactory* result = new DateTimeFactory(DateTimeType::Date);
    if (result == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

/* static */ StandardFunctions::DateTimeFactory* StandardFunctions::DateTimeFactory::time(UErrorCode& errorCode) {
    NULL_ON_ERROR(errorCode);

    DateTimeFactory* result = new DateTimeFactory(DateTimeType::Time);
    if (result == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

Formatter* StandardFunctions::DateTimeFactory::createFormatter(const Locale& locale, UErrorCode& errorCode) {
    NULL_ON_ERROR(errorCode);

    Formatter* result = new StandardFunctions::DateTime(locale, type);
    if (result == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

FormattedPlaceholder StandardFunctions::DateTime::format(FormattedPlaceholder&& toFormat,
                                                   FunctionOptions&& opts,
                                                   UErrorCode& errorCode) const {
    if (U_FAILURE(errorCode)) {
        return {};
    }

    // Argument must be present
    if (!toFormat.canFormat()) {
        errorCode = U_MF_OPERAND_MISMATCH_ERROR;
        return std::move(toFormat);
    }

    LocalPointer<DateFormat> df;
    Formattable opt;

    DateFormat::EStyle dateStyle = DateFormat::kShort;
    DateFormat::EStyle timeStyle = DateFormat::kShort;

    UnicodeString dateStyleName("dateStyle");
    UnicodeString timeStyleName("timeStyle");
    UnicodeString styleName("style");

    bool hasDateStyleOption = opts.getFunctionOption(dateStyleName, opt);
    bool hasTimeStyleOption = opts.getFunctionOption(timeStyleName, opt);
    bool noOptions = opts.optionsCount() == 0;

    bool useStyle = (type == DateTimeFactory::DateTimeType::DateTime
                     && (hasDateStyleOption || hasTimeStyleOption
                         || noOptions))
        || (type != DateTimeFactory::DateTimeType::DateTime);

    bool useDate = type == DateTimeFactory::DateTimeType::Date
        || (type == DateTimeFactory::DateTimeType::DateTime
            && hasDateStyleOption);
    bool useTime = type == DateTimeFactory::DateTimeType::Time
        || (type == DateTimeFactory::DateTimeType::DateTime
            && hasTimeStyleOption);

    if (useStyle) {
        // Extract style options
        if (type == DateTimeFactory::DateTimeType::DateTime) {
            // Note that the options-getting has to be repeated across the three cases,
            // since `:datetime` uses "dateStyle"/"timeStyle" and `:date` and `:time`
            // use "style"
            dateStyle = stringToStyle(getFunctionOption(toFormat, opts, dateStyleName), errorCode);
            timeStyle = stringToStyle(getFunctionOption(toFormat, opts, timeStyleName), errorCode);

            if (useDate && !useTime) {
                df.adoptInstead(DateFormat::createDateInstance(dateStyle, locale));
            } else if (useTime && !useDate) {
                df.adoptInstead(DateFormat::createTimeInstance(timeStyle, locale));
            } else {
                df.adoptInstead(DateFormat::createDateTimeInstance(dateStyle, timeStyle, locale));
            }
        } else if (type == DateTimeFactory::DateTimeType::Date) {
            dateStyle = stringToStyle(getFunctionOption(toFormat, opts, styleName), errorCode);
            df.adoptInstead(DateFormat::createDateInstance(dateStyle, locale));
        } else {
            // :time
            timeStyle = stringToStyle(getFunctionOption(toFormat, opts, styleName), errorCode);
            df.adoptInstead(DateFormat::createTimeInstance(timeStyle, locale));
        }
    } else {
        // Build up a skeleton based on the field options, then use that to
        // create the date formatter

        UnicodeString skeleton;
        #define ADD_PATTERN(s) skeleton += UnicodeString(s)
        if (U_SUCCESS(errorCode)) {
            // Year
            UnicodeString year = getFunctionOption(toFormat, opts, UnicodeString("year"), errorCode);
            if (U_FAILURE(errorCode)) {
                errorCode = U_ZERO_ERROR;
            } else {
                useDate = true;
                if (year == UnicodeString("2-digit")) {
                    ADD_PATTERN("YY");
                } else if (year == UnicodeString("numeric")) {
                    ADD_PATTERN("YYYY");
                }
            }
            // Month
            UnicodeString month = getFunctionOption(toFormat, opts, UnicodeString("month"), errorCode);
            if (U_FAILURE(errorCode)) {
                errorCode = U_ZERO_ERROR;
            } else {
                useDate = true;
                /* numeric, 2-digit, long, short, narrow */
                if (month == UnicodeString("long")) {
                    ADD_PATTERN("MMMM");
                } else if (month == UnicodeString("short")) {
                    ADD_PATTERN("MMM");
                } else if (month == UnicodeString("narrow")) {
                    ADD_PATTERN("MMMMM");
                } else if (month == UnicodeString("numeric")) {
                    ADD_PATTERN("M");
                } else if (month == UnicodeString("2-digit")) {
                    ADD_PATTERN("MM");
                }
            }
            // Weekday
            UnicodeString weekday = getFunctionOption(toFormat, opts, UnicodeString("weekday"), errorCode);
            if (U_FAILURE(errorCode)) {
                errorCode = U_ZERO_ERROR;
            } else {
                useDate = true;
                if (weekday == UnicodeString("long")) {
                    ADD_PATTERN("EEEE");
                } else if (weekday == UnicodeString("short")) {
                    ADD_PATTERN("EEEEE");
                } else if (weekday == UnicodeString("narrow")) {
                    ADD_PATTERN("EEEEE");
                }
            }
            // Day
            UnicodeString day = getFunctionOption(toFormat, opts, UnicodeString("day"), errorCode);
            if (U_FAILURE(errorCode)) {
                errorCode = U_ZERO_ERROR;
            } else {
                useDate = true;
                if (day == UnicodeString("numeric")) {
                    ADD_PATTERN("d");
                } else if (day == UnicodeString("2-digit")) {
                    ADD_PATTERN("dd");
                }
            }
            // Hour
            UnicodeString hour = getFunctionOption(toFormat, opts, UnicodeString("hour"), errorCode);
            if (U_FAILURE(errorCode)) {
                errorCode = U_ZERO_ERROR;
            } else {
                useTime = true;
                if (hour == UnicodeString("numeric")) {
                    ADD_PATTERN("h");
                } else if (hour == UnicodeString("2-digit")) {
                    ADD_PATTERN("hh");
                }
            }
            // Minute
            UnicodeString minute = getFunctionOption(toFormat, opts, UnicodeString("minute"), errorCode);
            if (U_FAILURE(errorCode)) {
                errorCode = U_ZERO_ERROR;
            } else {
                useTime = true;
                if (minute == UnicodeString("numeric")) {
                    ADD_PATTERN("m");
                } else if (minute == UnicodeString("2-digit")) {
                    ADD_PATTERN("mm");
                }
            }
            // Second
            UnicodeString second = getFunctionOption(toFormat, opts, UnicodeString("second"), errorCode);
            if (U_FAILURE(errorCode)) {
                errorCode = U_ZERO_ERROR;
            } else {
                useTime = true;
                if (second == UnicodeString("numeric")) {
                    ADD_PATTERN("s");
                } else if (second == UnicodeString("2-digit")) {
                    ADD_PATTERN("ss");
                }
            }
        }
        /*
          TODO
          fractionalSecondDigits
          hourCycle
          timeZoneName
          era
         */
        df.adoptInstead(DateFormat::createInstanceForSkeleton(skeleton, errorCode));
    }

    if (U_FAILURE(errorCode)) {
        return {};
    }
    if (!df.isValid()) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return {};
    }

    UnicodeString result;
    const Formattable& source = toFormat.asFormattable();
    switch (source.getType()) {
    case UFMT_STRING: {
        const UnicodeString& sourceStr = source.getString(errorCode);
        U_ASSERT(U_SUCCESS(errorCode));
        // Pattern for ISO 8601 format - datetime
        UnicodeString pattern("YYYY-MM-dd'T'HH:mm:ss");
        LocalPointer<DateFormat> dateParser(new SimpleDateFormat(pattern, errorCode));
        if (U_FAILURE(errorCode)) {
            errorCode = U_MF_FORMATTING_ERROR;
        } else {
            // Parse the date
            UDate d = dateParser->parse(sourceStr, errorCode);
            if (U_FAILURE(errorCode)) {
                // Pattern for ISO 8601 format - date
                UnicodeString pattern("YYYY-MM-dd");
                errorCode = U_ZERO_ERROR;
                dateParser.adoptInstead(new SimpleDateFormat(pattern, errorCode));
                if (U_FAILURE(errorCode)) {
                    errorCode = U_MF_FORMATTING_ERROR;
                } else {
                    d = dateParser->parse(sourceStr, errorCode);
                    if (U_FAILURE(errorCode)) {
                        errorCode = U_MF_OPERAND_MISMATCH_ERROR;
                    }
                }
            }
            // Use the parsed date as the source value
            // in the returned FormattedPlaceholder; this is necessary
            // so the date can be re-formatted
            toFormat = FormattedPlaceholder(message2::Formattable::forDate(d),
                                            toFormat.getFallback());
            df->format(d, result, 0, errorCode);
        }
        break;
    }
    case UFMT_DATE: {
        df->format(source.asICUFormattable(errorCode), result, 0, errorCode);
        if (U_FAILURE(errorCode)) {
            if (errorCode == U_ILLEGAL_ARGUMENT_ERROR) {
                errorCode = U_MF_OPERAND_MISMATCH_ERROR;
            }
        }
        break;
    }
    // Any other cases are an error
    default: {
        errorCode = U_MF_OPERAND_MISMATCH_ERROR;
        break;
    }
    }
    if (U_FAILURE(errorCode)) {
        return {};
    }
    return FormattedPlaceholder(toFormat, std::move(opts), FormattedValue(std::move(result)));
}

StandardFunctions::DateTimeFactory::~DateTimeFactory() {}
StandardFunctions::DateTime::~DateTime() {}

// --------- TextFactory

Selector* StandardFunctions::TextFactory::createSelector(const Locale& locale, UErrorCode& errorCode) const {
    Selector* result = new TextSelector(locale);
    if (result == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    return result;
}

void StandardFunctions::TextSelector::selectKey(FormattedPlaceholder&& toFormat,
                                                FunctionOptions&& opts,
                                                const UnicodeString* keys,
                                                int32_t keysLen,
                                                UnicodeString* prefs,
                                                int32_t& prefsLen,
						UErrorCode& errorCode) const {
    // No options
    (void) opts;

    CHECK_ERROR(errorCode);

    // Just compares the key and value as strings

    // Argument must be present
    if (!toFormat.canFormat()) {
        errorCode = U_MF_SELECTOR_ERROR;
        return;
    }

    prefsLen = 0;

    // Convert to string
    const UnicodeString& formattedValue = toFormat.formatToString(locale, errorCode);
    if (U_FAILURE(errorCode)) {
        return;
    }

    for (int32_t i = 0; i < keysLen; i++) {
        if (keys[i] == formattedValue) {
	    prefs[0] = keys[i];
            prefsLen = 1;
            break;
        }
    }
}

StandardFunctions::TextFactory::~TextFactory() {}
StandardFunctions::TextSelector::~TextSelector() {}

} // namespace message2
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */

