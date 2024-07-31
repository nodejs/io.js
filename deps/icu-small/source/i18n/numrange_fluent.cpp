// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

// Allow implicit conversion from char16_t* to UnicodeString for this file:
// Helpful in toString methods and elsewhere.
#define UNISTR_FROM_STRING_EXPLICIT

#include "numrange_impl.h"
#include "util.h"
#include "number_utypes.h"
#include "number_decnum.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;


// This function needs to be declared in this namespace so it can be friended.
// NOTE: In Java, this logic is handled in the resolve() function.
void icu::number::impl::touchRangeLocales(RangeMacroProps& macros) {
    macros.formatter1.fMacros.locale = macros.locale;
    macros.formatter2.fMacros.locale = macros.locale;
}


template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterBoth(const UnlocalizedNumberFormatter& formatter) const& {
    Derived copy(*this);
    copy.fMacros.formatter1 = formatter;
    copy.fMacros.singleFormatter = true;
    touchRangeLocales(copy.fMacros);
    return copy;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterBoth(const UnlocalizedNumberFormatter& formatter) && {
    Derived move(std::move(*this));
    move.fMacros.formatter1 = formatter;
    move.fMacros.singleFormatter = true;
    touchRangeLocales(move.fMacros);
    return move;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterBoth(UnlocalizedNumberFormatter&& formatter) const& {
    Derived copy(*this);
    copy.fMacros.formatter1 = std::move(formatter);
    copy.fMacros.singleFormatter = true;
    touchRangeLocales(copy.fMacros);
    return copy;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterBoth(UnlocalizedNumberFormatter&& formatter) && {
    Derived move(std::move(*this));
    move.fMacros.formatter1 = std::move(formatter);
    move.fMacros.singleFormatter = true;
    touchRangeLocales(move.fMacros);
    return move;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterFirst(const UnlocalizedNumberFormatter& formatter) const& {
    Derived copy(*this);
    copy.fMacros.formatter1 = formatter;
    copy.fMacros.singleFormatter = false;
    touchRangeLocales(copy.fMacros);
    return copy;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterFirst(const UnlocalizedNumberFormatter& formatter) && {
    Derived move(std::move(*this));
    move.fMacros.formatter1 = formatter;
    move.fMacros.singleFormatter = false;
    touchRangeLocales(move.fMacros);
    return move;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterFirst(UnlocalizedNumberFormatter&& formatter) const& {
    Derived copy(*this);
    copy.fMacros.formatter1 = std::move(formatter);
    copy.fMacros.singleFormatter = false;
    touchRangeLocales(copy.fMacros);
    return copy;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterFirst(UnlocalizedNumberFormatter&& formatter) && {
    Derived move(std::move(*this));
    move.fMacros.formatter1 = std::move(formatter);
    move.fMacros.singleFormatter = false;
    touchRangeLocales(move.fMacros);
    return move;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterSecond(const UnlocalizedNumberFormatter& formatter) const& {
    Derived copy(*this);
    copy.fMacros.formatter2 = formatter;
    copy.fMacros.singleFormatter = false;
    touchRangeLocales(copy.fMacros);
    return copy;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterSecond(const UnlocalizedNumberFormatter& formatter) && {
    Derived move(std::move(*this));
    move.fMacros.formatter2 = formatter;
    move.fMacros.singleFormatter = false;
    touchRangeLocales(move.fMacros);
    return move;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterSecond(UnlocalizedNumberFormatter&& formatter) const& {
    Derived copy(*this);
    copy.fMacros.formatter2 = std::move(formatter);
    copy.fMacros.singleFormatter = false;
    touchRangeLocales(copy.fMacros);
    return copy;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::numberFormatterSecond(UnlocalizedNumberFormatter&& formatter) && {
    Derived move(std::move(*this));
    move.fMacros.formatter2 = std::move(formatter);
    move.fMacros.singleFormatter = false;
    touchRangeLocales(move.fMacros);
    return move;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::collapse(UNumberRangeCollapse collapse) const& {
    Derived copy(*this);
    copy.fMacros.collapse = collapse;
    return copy;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::collapse(UNumberRangeCollapse collapse) && {
    Derived move(std::move(*this));
    move.fMacros.collapse = collapse;
    return move;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::identityFallback(UNumberRangeIdentityFallback identityFallback) const& {
    Derived copy(*this);
    copy.fMacros.identityFallback = identityFallback;
    return copy;
}

template<typename Derived>
Derived NumberRangeFormatterSettings<Derived>::identityFallback(UNumberRangeIdentityFallback identityFallback) && {
    Derived move(std::move(*this));
    move.fMacros.identityFallback = identityFallback;
    return move;
}

template<typename Derived>
LocalPointer<Derived> NumberRangeFormatterSettings<Derived>::clone() const & {
    return LocalPointer<Derived>(new Derived(*this));
}

template<typename Derived>
LocalPointer<Derived> NumberRangeFormatterSettings<Derived>::clone() && {
    return LocalPointer<Derived>(new Derived(std::move(*this)));
}

// Declare all classes that implement NumberRangeFormatterSettings
// See https://stackoverflow.com/a/495056/1407170
template
class icu::number::NumberRangeFormatterSettings<icu::number::UnlocalizedNumberRangeFormatter>;
template
class icu::number::NumberRangeFormatterSettings<icu::number::LocalizedNumberRangeFormatter>;


UnlocalizedNumberRangeFormatter NumberRangeFormatter::with() {
    UnlocalizedNumberRangeFormatter result;
    return result;
}

LocalizedNumberRangeFormatter NumberRangeFormatter::withLocale(const Locale& locale) {
    return with().locale(locale);
}


template<typename T> using NFS = NumberRangeFormatterSettings<T>;
using LNF = LocalizedNumberRangeFormatter;
using UNF = UnlocalizedNumberRangeFormatter;

UnlocalizedNumberRangeFormatter::UnlocalizedNumberRangeFormatter(const UNF& other)
        : UNF(static_cast<const NFS<UNF>&>(other)) {}

UnlocalizedNumberRangeFormatter::UnlocalizedNumberRangeFormatter(const NFS<UNF>& other)
        : NFS<UNF>(other) {
    // No additional fields to assign
}

// Make default copy constructor call the NumberRangeFormatterSettings copy constructor.
UnlocalizedNumberRangeFormatter::UnlocalizedNumberRangeFormatter(UNF&& src) noexcept
        : UNF(static_cast<NFS<UNF>&&>(src)) {}

UnlocalizedNumberRangeFormatter::UnlocalizedNumberRangeFormatter(NFS<UNF>&& src) noexcept
        : NFS<UNF>(std::move(src)) {
    // No additional fields to assign
}

UnlocalizedNumberRangeFormatter::UnlocalizedNumberRangeFormatter(const impl::RangeMacroProps &macros) {
    fMacros = macros;
}

UnlocalizedNumberRangeFormatter::UnlocalizedNumberRangeFormatter(impl::RangeMacroProps &&macros) {
    fMacros = macros;
}

UnlocalizedNumberRangeFormatter& UnlocalizedNumberRangeFormatter::operator=(const UNF& other) {
    NFS<UNF>::operator=(static_cast<const NFS<UNF>&>(other));
    // No additional fields to assign
    return *this;
}

UnlocalizedNumberRangeFormatter& UnlocalizedNumberRangeFormatter::operator=(UNF&& src) noexcept {
    NFS<UNF>::operator=(static_cast<NFS<UNF>&&>(src));
    // No additional fields to assign
    return *this;
}

// Make default copy constructor call the NumberRangeFormatterSettings copy constructor.
LocalizedNumberRangeFormatter::LocalizedNumberRangeFormatter(const LNF& other)
        : LNF(static_cast<const NFS<LNF>&>(other)) {}

LocalizedNumberRangeFormatter::LocalizedNumberRangeFormatter(const NFS<LNF>& other)
        : NFS<LNF>(other) {
    // No additional fields to assign
}

LocalizedNumberRangeFormatter::LocalizedNumberRangeFormatter(LocalizedNumberRangeFormatter&& src) noexcept
        : LNF(static_cast<NFS<LNF>&&>(src)) {}

LocalizedNumberRangeFormatter::LocalizedNumberRangeFormatter(NFS<LNF>&& src) noexcept
        : NFS<LNF>(std::move(src)) {
    // Steal the compiled formatter
    LNF&& _src = static_cast<LNF&&>(src);
    auto* stolen = _src.fAtomicFormatter.exchange(nullptr);
    delete fAtomicFormatter.exchange(stolen);
}

LocalizedNumberRangeFormatter& LocalizedNumberRangeFormatter::operator=(const LNF& other) {
    if (this == &other) { return *this; }  // self-assignment: no-op
    NFS<LNF>::operator=(static_cast<const NFS<LNF>&>(other));
    // Do not steal; just clear
    delete fAtomicFormatter.exchange(nullptr);
    return *this;
}

LocalizedNumberRangeFormatter& LocalizedNumberRangeFormatter::operator=(LNF&& src) noexcept {
    NFS<LNF>::operator=(static_cast<NFS<LNF>&&>(src));
    // Steal the compiled formatter
    auto* stolen = src.fAtomicFormatter.exchange(nullptr);
    delete fAtomicFormatter.exchange(stolen);
    return *this;
}


LocalizedNumberRangeFormatter::~LocalizedNumberRangeFormatter() {
    delete fAtomicFormatter.exchange(nullptr);
}

LocalizedNumberRangeFormatter::LocalizedNumberRangeFormatter(const RangeMacroProps& macros, const Locale& locale) {
    fMacros = macros;
    fMacros.locale = locale;
    touchRangeLocales(fMacros);
}

LocalizedNumberRangeFormatter::LocalizedNumberRangeFormatter(RangeMacroProps&& macros, const Locale& locale) {
    fMacros = std::move(macros);
    fMacros.locale = locale;
    touchRangeLocales(fMacros);
}

LocalizedNumberRangeFormatter UnlocalizedNumberRangeFormatter::locale(const Locale& locale) const& {
    return LocalizedNumberRangeFormatter(fMacros, locale);
}

LocalizedNumberRangeFormatter UnlocalizedNumberRangeFormatter::locale(const Locale& locale)&& {
    return LocalizedNumberRangeFormatter(std::move(fMacros), locale);
}


UnlocalizedNumberRangeFormatter LocalizedNumberRangeFormatter::withoutLocale() const & {
    RangeMacroProps macros(fMacros);
    macros.locale = Locale();
    return UnlocalizedNumberRangeFormatter(macros);
}

UnlocalizedNumberRangeFormatter LocalizedNumberRangeFormatter::withoutLocale() && {
    RangeMacroProps macros(std::move(fMacros));
    macros.locale = Locale();
    return UnlocalizedNumberRangeFormatter(std::move(macros));
}


FormattedNumberRange LocalizedNumberRangeFormatter::formatFormattableRange(
        const Formattable& first, const Formattable& second, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return FormattedNumberRange(U_ILLEGAL_ARGUMENT_ERROR);
    }

    auto* results = new UFormattedNumberRangeData();
    if (results == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return FormattedNumberRange(status);
    }

    first.populateDecimalQuantity(results->quantity1, status);
    if (U_FAILURE(status)) {
        return FormattedNumberRange(status);
    }

    second.populateDecimalQuantity(results->quantity2, status);
    if (U_FAILURE(status)) {
        return FormattedNumberRange(status);
    }

    formatImpl(*results, first == second, status);

    // Do not save the results object if we encountered a failure.
    if (U_SUCCESS(status)) {
        return FormattedNumberRange(results);
    } else {
        delete results;
        return FormattedNumberRange(status);
    }
}

void LocalizedNumberRangeFormatter::formatImpl(
        UFormattedNumberRangeData& results, bool equalBeforeRounding, UErrorCode& status) const {
    const auto* impl = getFormatter(status);
    if (U_FAILURE(status)) {
        return;
    }
    if (impl == nullptr) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return;
    }
    impl->format(results, equalBeforeRounding, status);
    if (U_FAILURE(status)) {
        return;
    }
    results.getStringRef().writeTerminator(status);
}

const impl::NumberRangeFormatterImpl*
LocalizedNumberRangeFormatter::getFormatter(UErrorCode& status) const {
    // TODO: Move this into umutex.h? (similar logic also in decimfmt.cpp)
    // See ICU-20146

    if (U_FAILURE(status)) {
        return nullptr;
    }

    // First try to get the pre-computed formatter
    auto* ptr = fAtomicFormatter.load();
    if (ptr != nullptr) {
        return ptr;
    }

    // Try computing the formatter on our own
    auto* temp = new NumberRangeFormatterImpl(fMacros, status);
    if (U_FAILURE(status)) {
        delete temp;
        return nullptr;
    }
    if (temp == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    // Note: ptr starts as nullptr; during compare_exchange,
    // it is set to what is actually stored in the atomic
    // if another thread beat us to computing the formatter object.
    auto* nonConstThis = const_cast<LocalizedNumberRangeFormatter*>(this);
    if (!nonConstThis->fAtomicFormatter.compare_exchange_strong(ptr, temp)) {
        // Another thread beat us to computing the formatter
        delete temp;
        return ptr;
    } else {
        // Our copy of the formatter got stored in the atomic
        return temp;
    }

}


#endif /* #if !UCONFIG_NO_FORMATTING */
