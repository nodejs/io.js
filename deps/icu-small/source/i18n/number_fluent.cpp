// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "uassert.h"
#include "unicode/numberformatter.h"
#include "number_decimalquantity.h"
#include "number_formatimpl.h"
#include "umutex.h"
#include "number_asformat.h"
#include "number_utils.h"
#include "number_utypes.h"
#include "number_mapper.h"
#include "util.h"
#include "fphdlimp.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;

#if (U_PF_WINDOWS <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN) && defined(_MSC_VER)
// Ignore MSVC warning 4661. This is generated for NumberFormatterSettings<>::toSkeleton() as this method
// is defined elsewhere (in number_skeletons.cpp). The compiler is warning that the explicit template instantiation
// inside this single translation unit (CPP file) is incomplete, and thus it isn't sure if the template class is
// fully defined. However, since each translation unit explicitly instantiates all the necessary template classes,
// they will all be passed to the linker, and the linker will still find and export all the class members.
#pragma warning(push)
#pragma warning(disable: 4661)
#endif

template<typename Derived>
Derived NumberFormatterSettings<Derived>::notation(const Notation& notation) const& {
    Derived copy(*this);
    // NOTE: Slicing is OK.
    copy.fMacros.notation = notation;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::notation(const Notation& notation)&& {
    Derived move(std::move(*this));
    // NOTE: Slicing is OK.
    move.fMacros.notation = notation;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::unit(const icu::MeasureUnit& unit) const& {
    Derived copy(*this);
    // NOTE: Slicing occurs here. However, CurrencyUnit can be restored from MeasureUnit.
    // TimeUnit may be affected, but TimeUnit is not as relevant to number formatting.
    copy.fMacros.unit = unit;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::unit(const icu::MeasureUnit& unit)&& {
    Derived move(std::move(*this));
    // See comments above about slicing.
    move.fMacros.unit = unit;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::adoptUnit(icu::MeasureUnit* unit) const& {
    Derived copy(*this);
    // Just move the unit into the MacroProps by value, and delete it since we have ownership.
    // NOTE: Slicing occurs here. However, CurrencyUnit can be restored from MeasureUnit.
    // TimeUnit may be affected, but TimeUnit is not as relevant to number formatting.
    if (unit != nullptr) {
        // TODO: On nullptr, reset to default value?
        copy.fMacros.unit = std::move(*unit);
        delete unit;
    }
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::adoptUnit(icu::MeasureUnit* unit)&& {
    Derived move(std::move(*this));
    // See comments above about slicing and ownership.
    if (unit != nullptr) {
        // TODO: On nullptr, reset to default value?
        move.fMacros.unit = std::move(*unit);
        delete unit;
    }
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::perUnit(const icu::MeasureUnit& perUnit) const& {
    Derived copy(*this);
    // See comments above about slicing.
    copy.fMacros.perUnit = perUnit;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::perUnit(const icu::MeasureUnit& perUnit)&& {
    Derived move(std::move(*this));
    // See comments above about slicing.
    move.fMacros.perUnit = perUnit;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::adoptPerUnit(icu::MeasureUnit* perUnit) const& {
    Derived copy(*this);
    // See comments above about slicing and ownership.
    if (perUnit != nullptr) {
        // TODO: On nullptr, reset to default value?
        copy.fMacros.perUnit = std::move(*perUnit);
        delete perUnit;
    }
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::adoptPerUnit(icu::MeasureUnit* perUnit)&& {
    Derived move(std::move(*this));
    // See comments above about slicing and ownership.
    if (perUnit != nullptr) {
        // TODO: On nullptr, reset to default value?
        move.fMacros.perUnit = std::move(*perUnit);
        delete perUnit;
    }
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::precision(const Precision& precision) const& {
    Derived copy(*this);
    // NOTE: Slicing is OK.
    copy.fMacros.precision = precision;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::precision(const Precision& precision)&& {
    Derived move(std::move(*this));
    // NOTE: Slicing is OK.
    move.fMacros.precision = precision;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::roundingMode(UNumberFormatRoundingMode roundingMode) const& {
    Derived copy(*this);
    copy.fMacros.roundingMode = roundingMode;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::roundingMode(UNumberFormatRoundingMode roundingMode)&& {
    Derived move(std::move(*this));
    move.fMacros.roundingMode = roundingMode;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::grouping(UNumberGroupingStrategy strategy) const& {
    Derived copy(*this);
    // NOTE: This is slightly different than how the setting is stored in Java
    // because we want to put it on the stack.
    copy.fMacros.grouper = Grouper::forStrategy(strategy);
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::grouping(UNumberGroupingStrategy strategy)&& {
    Derived move(std::move(*this));
    move.fMacros.grouper = Grouper::forStrategy(strategy);
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::integerWidth(const IntegerWidth& style) const& {
    Derived copy(*this);
    copy.fMacros.integerWidth = style;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::integerWidth(const IntegerWidth& style)&& {
    Derived move(std::move(*this));
    move.fMacros.integerWidth = style;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::symbols(const DecimalFormatSymbols& symbols) const& {
    Derived copy(*this);
    copy.fMacros.symbols.setTo(symbols);
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::symbols(const DecimalFormatSymbols& symbols)&& {
    Derived move(std::move(*this));
    move.fMacros.symbols.setTo(symbols);
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::adoptSymbols(NumberingSystem* ns) const& {
    Derived copy(*this);
    copy.fMacros.symbols.setTo(ns);
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::adoptSymbols(NumberingSystem* ns)&& {
    Derived move(std::move(*this));
    move.fMacros.symbols.setTo(ns);
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::unitWidth(UNumberUnitWidth width) const& {
    Derived copy(*this);
    copy.fMacros.unitWidth = width;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::unitWidth(UNumberUnitWidth width)&& {
    Derived move(std::move(*this));
    move.fMacros.unitWidth = width;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::sign(UNumberSignDisplay style) const& {
    Derived copy(*this);
    copy.fMacros.sign = style;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::sign(UNumberSignDisplay style)&& {
    Derived move(std::move(*this));
    move.fMacros.sign = style;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::decimal(UNumberDecimalSeparatorDisplay style) const& {
    Derived copy(*this);
    copy.fMacros.decimal = style;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::decimal(UNumberDecimalSeparatorDisplay style)&& {
    Derived move(std::move(*this));
    move.fMacros.decimal = style;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::scale(const Scale& scale) const& {
    Derived copy(*this);
    copy.fMacros.scale = scale;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::scale(const Scale& scale)&& {
    Derived move(std::move(*this));
    move.fMacros.scale = scale;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::usage(const StringPiece usage) const& {
    Derived copy(*this);
    copy.fMacros.usage.set(usage);
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::usage(const StringPiece usage)&& {
    Derived move(std::move(*this));
    move.fMacros.usage.set(usage);
    return move;
}

template <typename Derived>
Derived NumberFormatterSettings<Derived>::displayOptions(const DisplayOptions &displayOptions) const & {
    Derived copy(*this);
    // `displayCase` does not recognise the `undefined`
    if (displayOptions.getGrammaticalCase() == UDISPOPT_GRAMMATICAL_CASE_UNDEFINED) {
        copy.fMacros.unitDisplayCase.set(nullptr);
        return copy;
    }

    copy.fMacros.unitDisplayCase.set(
        udispopt_getGrammaticalCaseIdentifier(displayOptions.getGrammaticalCase()));
    return copy;
}

template <typename Derived>
Derived NumberFormatterSettings<Derived>::displayOptions(const DisplayOptions &displayOptions) && {
    Derived move(std::move(*this));
    // `displayCase` does not recognise the `undefined`
    if (displayOptions.getGrammaticalCase() == UDISPOPT_GRAMMATICAL_CASE_UNDEFINED) {
        move.fMacros.unitDisplayCase.set(nullptr);
        return move;
    }

    move.fMacros.unitDisplayCase.set(
        udispopt_getGrammaticalCaseIdentifier(displayOptions.getGrammaticalCase()));
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::unitDisplayCase(const StringPiece unitDisplayCase) const& {
    Derived copy(*this);
    copy.fMacros.unitDisplayCase.set(unitDisplayCase);
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::unitDisplayCase(const StringPiece unitDisplayCase)&& {
    Derived move(std::move(*this));
    move.fMacros.unitDisplayCase.set(unitDisplayCase);
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::padding(const Padder& padder) const& {
    Derived copy(*this);
    copy.fMacros.padder = padder;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::padding(const Padder& padder)&& {
    Derived move(std::move(*this));
    move.fMacros.padder = padder;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::threshold(int32_t threshold) const& {
    Derived copy(*this);
    copy.fMacros.threshold = threshold;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::threshold(int32_t threshold)&& {
    Derived move(std::move(*this));
    move.fMacros.threshold = threshold;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::macros(const impl::MacroProps& macros) const& {
    Derived copy(*this);
    copy.fMacros = macros;
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::macros(const impl::MacroProps& macros)&& {
    Derived move(std::move(*this));
    move.fMacros = macros;
    return move;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::macros(impl::MacroProps&& macros) const& {
    Derived copy(*this);
    copy.fMacros = std::move(macros);
    return copy;
}

template<typename Derived>
Derived NumberFormatterSettings<Derived>::macros(impl::MacroProps&& macros)&& {
    Derived move(std::move(*this));
    move.fMacros = std::move(macros);
    return move;
}

// Note: toSkeleton defined in number_skeletons.cpp

template<typename Derived>
LocalPointer<Derived> NumberFormatterSettings<Derived>::clone() const & {
    return LocalPointer<Derived>(new Derived(*this));
}

template<typename Derived>
LocalPointer<Derived> NumberFormatterSettings<Derived>::clone() && {
    return LocalPointer<Derived>(new Derived(std::move(*this)));
}

// Declare all classes that implement NumberFormatterSettings
// See https://stackoverflow.com/a/495056/1407170
template
class icu::number::NumberFormatterSettings<icu::number::UnlocalizedNumberFormatter>;
template
class icu::number::NumberFormatterSettings<icu::number::LocalizedNumberFormatter>;


UnlocalizedNumberFormatter NumberFormatter::with() {
    UnlocalizedNumberFormatter result;
    return result;
}

LocalizedNumberFormatter NumberFormatter::withLocale(const Locale& locale) {
    return with().locale(locale);
}

// Note: forSkeleton defined in number_skeletons.cpp


template<typename T> using NFS = NumberFormatterSettings<T>;
using LNF = LocalizedNumberFormatter;
using UNF = UnlocalizedNumberFormatter;

UnlocalizedNumberFormatter::UnlocalizedNumberFormatter(const UNF& other)
        : UNF(static_cast<const NFS<UNF>&>(other)) {}

UnlocalizedNumberFormatter::UnlocalizedNumberFormatter(const NFS<UNF>& other)
        : NFS<UNF>(other) {
    // No additional fields to assign
}

UnlocalizedNumberFormatter::UnlocalizedNumberFormatter(const impl::MacroProps &macros) {
    fMacros = macros;
}

UnlocalizedNumberFormatter::UnlocalizedNumberFormatter(impl::MacroProps &&macros) {
    fMacros = macros;
}

// Make default copy constructor call the NumberFormatterSettings copy constructor.
UnlocalizedNumberFormatter::UnlocalizedNumberFormatter(UNF&& src) noexcept
        : UNF(static_cast<NFS<UNF>&&>(src)) {}

UnlocalizedNumberFormatter::UnlocalizedNumberFormatter(NFS<UNF>&& src) noexcept
        : NFS<UNF>(std::move(src)) {
    // No additional fields to assign
}

UnlocalizedNumberFormatter& UnlocalizedNumberFormatter::operator=(const UNF& other) {
    NFS<UNF>::operator=(static_cast<const NFS<UNF>&>(other));
    // No additional fields to assign
    return *this;
}

UnlocalizedNumberFormatter& UnlocalizedNumberFormatter::operator=(UNF&& src) noexcept {
    NFS<UNF>::operator=(static_cast<NFS<UNF>&&>(src));
    // No additional fields to assign
    return *this;
}

// Make default copy constructor call the NumberFormatterSettings copy constructor.
LocalizedNumberFormatter::LocalizedNumberFormatter(const LNF& other)
        : LNF(static_cast<const NFS<LNF>&>(other)) {}

LocalizedNumberFormatter::LocalizedNumberFormatter(const NFS<LNF>& other)
        : NFS<LNF>(other) {
    UErrorCode localStatus = U_ZERO_ERROR; // Can't bubble up the error
    lnfCopyHelper(static_cast<const LNF&>(other), localStatus);
}

LocalizedNumberFormatter::LocalizedNumberFormatter(LocalizedNumberFormatter&& src) noexcept
        : LNF(static_cast<NFS<LNF>&&>(src)) {}

LocalizedNumberFormatter::LocalizedNumberFormatter(NFS<LNF>&& src) noexcept
        : NFS<LNF>(std::move(src)) {
    lnfMoveHelper(std::move(static_cast<LNF&&>(src)));
}

LocalizedNumberFormatter& LocalizedNumberFormatter::operator=(const LNF& other) {
    if (this == &other) { return *this; }  // self-assignment: no-op
    NFS<LNF>::operator=(static_cast<const NFS<LNF>&>(other));
    UErrorCode localStatus = U_ZERO_ERROR; // Can't bubble up the error
    lnfCopyHelper(other, localStatus);
    return *this;
}

LocalizedNumberFormatter& LocalizedNumberFormatter::operator=(LNF&& src) noexcept {
    NFS<LNF>::operator=(static_cast<NFS<LNF>&&>(src));
    lnfMoveHelper(std::move(src));
    return *this;
}

void LocalizedNumberFormatter::resetCompiled() {
    auto* callCount = reinterpret_cast<u_atomic_int32_t*>(fUnsafeCallCount);
    umtx_storeRelease(*callCount, 0);
    fCompiled = nullptr;
}

void LocalizedNumberFormatter::lnfMoveHelper(LNF&& src) {
    // Copy over the compiled formatter and set call count to INT32_MIN as in computeCompiled().
    // Don't copy the call count directly because doing so requires a loadAcquire/storeRelease.
    // The bits themselves appear to be platform-dependent, so copying them might not be safe.
    delete fCompiled;
    if (src.fCompiled != nullptr) {
        auto* callCount = reinterpret_cast<u_atomic_int32_t*>(fUnsafeCallCount);
        umtx_storeRelease(*callCount, INT32_MIN);
        fCompiled = src.fCompiled;
        // Reset the source object to leave it in a safe state.
        src.resetCompiled();
    } else {
        resetCompiled();
    }

    // Unconditionally move the warehouse
    delete fWarehouse;
    fWarehouse = src.fWarehouse;
    src.fWarehouse = nullptr;
}

void LocalizedNumberFormatter::lnfCopyHelper(const LNF&, UErrorCode& status) {
    // When copying, always reset the compiled formatter.
    delete fCompiled;
    resetCompiled();

    // If MacroProps has a reference to AffixPatternProvider, we need to copy it.
    // If MacroProps has a reference to PluralRules, copy that one, too.
    delete fWarehouse;
    if (fMacros.affixProvider || fMacros.rules) {
        LocalPointer<DecimalFormatWarehouse> warehouse(new DecimalFormatWarehouse(), status);
        if (U_FAILURE(status)) {
            fWarehouse = nullptr;
            return;
        }
        if (fMacros.affixProvider) {
            warehouse->affixProvider.setTo(fMacros.affixProvider, status);
            fMacros.affixProvider = &warehouse->affixProvider.get();
        }
        if (fMacros.rules) {
            warehouse->rules.adoptInsteadAndCheckErrorCode(
                new PluralRules(*fMacros.rules), status);
            fMacros.rules = warehouse->rules.getAlias();
        }
        fWarehouse = warehouse.orphan();
    } else {
        fWarehouse = nullptr;
    }
}


LocalizedNumberFormatter::~LocalizedNumberFormatter() {
    delete fCompiled;
    delete fWarehouse;
}

LocalizedNumberFormatter::LocalizedNumberFormatter(const MacroProps& macros, const Locale& locale) {
    fMacros = macros;
    fMacros.locale = locale;
}

LocalizedNumberFormatter::LocalizedNumberFormatter(MacroProps&& macros, const Locale& locale) {
    fMacros = std::move(macros);
    fMacros.locale = locale;
}

LocalizedNumberFormatter UnlocalizedNumberFormatter::locale(const Locale& locale) const& {
    return LocalizedNumberFormatter(fMacros, locale);
}

LocalizedNumberFormatter UnlocalizedNumberFormatter::locale(const Locale& locale)&& {
    return LocalizedNumberFormatter(std::move(fMacros), locale);
}

FormattedNumber LocalizedNumberFormatter::formatInt(int64_t value, UErrorCode& status) const {
    if (U_FAILURE(status)) { return FormattedNumber(U_ILLEGAL_ARGUMENT_ERROR); }
    auto* results = new UFormattedNumberData();
    if (results == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return FormattedNumber(status);
    }
    results->quantity.setToLong(value);
    formatImpl(results, status);

    // Do not save the results object if we encountered a failure.
    if (U_SUCCESS(status)) {
        return FormattedNumber(results);
    } else {
        delete results;
        return FormattedNumber(status);
    }
}

FormattedNumber LocalizedNumberFormatter::formatDouble(double value, UErrorCode& status) const {
    if (U_FAILURE(status)) { return FormattedNumber(U_ILLEGAL_ARGUMENT_ERROR); }
    auto* results = new UFormattedNumberData();
    if (results == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return FormattedNumber(status);
    }
    results->quantity.setToDouble(value);
    formatImpl(results, status);

    // Do not save the results object if we encountered a failure.
    if (U_SUCCESS(status)) {
        return FormattedNumber(results);
    } else {
        delete results;
        return FormattedNumber(status);
    }
}

FormattedNumber LocalizedNumberFormatter::formatDecimal(StringPiece value, UErrorCode& status) const {
    if (U_FAILURE(status)) { return FormattedNumber(U_ILLEGAL_ARGUMENT_ERROR); }
    auto* results = new UFormattedNumberData();
    if (results == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return FormattedNumber(status);
    }
    results->quantity.setToDecNumber(value, status);
    formatImpl(results, status);

    // Do not save the results object if we encountered a failure.
    if (U_SUCCESS(status)) {
        return FormattedNumber(results);
    } else {
        delete results;
        return FormattedNumber(status);
    }
}

FormattedNumber
LocalizedNumberFormatter::formatDecimalQuantity(const DecimalQuantity& dq, UErrorCode& status) const {
    if (U_FAILURE(status)) { return FormattedNumber(U_ILLEGAL_ARGUMENT_ERROR); }
    auto* results = new UFormattedNumberData();
    if (results == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return FormattedNumber(status);
    }
    results->quantity = dq;
    formatImpl(results, status);

    // Do not save the results object if we encountered a failure.
    if (U_SUCCESS(status)) {
        return FormattedNumber(results);
    } else {
        delete results;
        return FormattedNumber(status);
    }
}

void LocalizedNumberFormatter::formatImpl(impl::UFormattedNumberData* results, UErrorCode& status) const {
    if (computeCompiled(status)) {
        fCompiled->format(results, status);
    } else {
        NumberFormatterImpl::formatStatic(fMacros, results, status);
    }
    if (U_FAILURE(status)) {
        return;
    }
    results->getStringRef().writeTerminator(status);
}

void LocalizedNumberFormatter::getAffixImpl(bool isPrefix, bool isNegative, UnicodeString& result,
                                            UErrorCode& status) const {
    FormattedStringBuilder string;
    auto signum = static_cast<Signum>(isNegative ? SIGNUM_NEG : SIGNUM_POS);
    // Always return affixes for plural form OTHER.
    static const StandardPlural::Form plural = StandardPlural::OTHER;
    int32_t prefixLength;
    if (computeCompiled(status)) {
        prefixLength = fCompiled->getPrefixSuffix(signum, plural, string, status);
    } else {
        prefixLength = NumberFormatterImpl::getPrefixSuffixStatic(fMacros, signum, plural, string, status);
    }
    result.remove();
    if (isPrefix) {
        result.append(string.toTempUnicodeString().tempSubStringBetween(0, prefixLength));
    } else {
        result.append(string.toTempUnicodeString().tempSubStringBetween(prefixLength, string.length()));
    }
}

bool LocalizedNumberFormatter::computeCompiled(UErrorCode& status) const {
    // fUnsafeCallCount contains memory to be interpreted as an atomic int, most commonly
    // std::atomic<int32_t>.  Since the type of atomic int is platform-dependent, we cast the
    // bytes in fUnsafeCallCount to u_atomic_int32_t, a typedef for the platform-dependent
    // atomic int type defined in umutex.h.
    static_assert(
            sizeof(u_atomic_int32_t) <= sizeof(fUnsafeCallCount),
            "Atomic integer size on this platform exceeds the size allocated by fUnsafeCallCount");
    auto* callCount = reinterpret_cast<u_atomic_int32_t*>(
            const_cast<LocalizedNumberFormatter*>(this)->fUnsafeCallCount);

    // A positive value in the atomic int indicates that the data structure is not yet ready;
    // a negative value indicates that it is ready. If, after the increment, the atomic int
    // is exactly threshold, then it is the current thread's job to build the data structure.
    // Note: We set the callCount to INT32_MIN so that if another thread proceeds to increment
    // the atomic int, the value remains below zero.
    int32_t currentCount = umtx_loadAcquire(*callCount);
    if (0 <= currentCount && currentCount <= fMacros.threshold && fMacros.threshold > 0) {
        currentCount = umtx_atomic_inc(callCount);
    }

    if (currentCount == fMacros.threshold && fMacros.threshold > 0) {
        // Build the data structure and then use it (slow to fast path).
        const NumberFormatterImpl* compiled = new NumberFormatterImpl(fMacros, status);
        if (compiled == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return false;
        }
        U_ASSERT(fCompiled == nullptr);
        const_cast<LocalizedNumberFormatter*>(this)->fCompiled = compiled;
        umtx_storeRelease(*callCount, INT32_MIN);
        return true;
    } else if (currentCount < 0) {
        // The data structure is already built; use it (fast path).
        U_ASSERT(fCompiled != nullptr);
        return true;
    } else {
        // Format the number without building the data structure (slow path).
        return false;
    }
}

const impl::NumberFormatterImpl* LocalizedNumberFormatter::getCompiled() const {
    return fCompiled;
}

int32_t LocalizedNumberFormatter::getCallCount() const {
    auto* callCount = reinterpret_cast<u_atomic_int32_t*>(
            const_cast<LocalizedNumberFormatter*>(this)->fUnsafeCallCount);
    return umtx_loadAcquire(*callCount);
}

// Note: toFormat defined in number_asformat.cpp

UnlocalizedNumberFormatter LocalizedNumberFormatter::withoutLocale() const & {
    MacroProps macros(fMacros);
    macros.locale = Locale();
    return UnlocalizedNumberFormatter(macros);
}

UnlocalizedNumberFormatter LocalizedNumberFormatter::withoutLocale() && {
    MacroProps macros(std::move(fMacros));
    macros.locale = Locale();
    return UnlocalizedNumberFormatter(std::move(macros));
}

const DecimalFormatSymbols* LocalizedNumberFormatter::getDecimalFormatSymbols() const {
    return fMacros.symbols.getDecimalFormatSymbols();
}

#if (U_PF_WINDOWS <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN) && defined(_MSC_VER)
// Warning 4661.
#pragma warning(pop)
#endif

#endif /* #if !UCONFIG_NO_FORMATTING */
