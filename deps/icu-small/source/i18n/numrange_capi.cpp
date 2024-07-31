// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

// Allow implicit conversion from char16_t* to UnicodeString for this file:
// Helpful in toString methods and elsewhere.
#define UNISTR_FROM_STRING_EXPLICIT

#include "fphdlimp.h"
#include "number_utypes.h"
#include "numparse_types.h"
#include "formattedval_impl.h"
#include "numrange_impl.h"
#include "number_decnum.h"
#include "unicode/numberrangeformatter.h"
#include "unicode/unumberrangeformatter.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;


U_NAMESPACE_BEGIN
namespace number::impl {

/**
 * Implementation class for UNumberRangeFormatter. Wraps a LocalizedRangeNumberFormatter.
 */
struct UNumberRangeFormatterData : public UMemory,
        // Magic number as ASCII == "NRF" (NumberRangeFormatter)
        public IcuCApiHelper<UNumberRangeFormatter, UNumberRangeFormatterData, 0x4E524600> {
    LocalizedNumberRangeFormatter fFormatter;
};

struct UFormattedNumberRangeImpl;

// Magic number as ASCII == "FDN" (FormatteDNumber)
typedef IcuCApiHelper<UFormattedNumberRange, UFormattedNumberRangeImpl, 0x46444E00> UFormattedNumberRangeApiHelper;

struct UFormattedNumberRangeImpl : public UFormattedValueImpl, public UFormattedNumberRangeApiHelper {
    UFormattedNumberRangeImpl();
    ~UFormattedNumberRangeImpl();

    FormattedNumberRange fImpl;
    UFormattedNumberRangeData fData;
};

UFormattedNumberRangeImpl::UFormattedNumberRangeImpl()
        : fImpl(&fData) {
    fFormattedValue = &fImpl;
}

UFormattedNumberRangeImpl::~UFormattedNumberRangeImpl() {
    // Disown the data from fImpl so it doesn't get deleted twice
    fImpl.fData = nullptr;
}

} // namespace number::impl
U_NAMESPACE_END


UPRV_FORMATTED_VALUE_CAPI_NO_IMPLTYPE_AUTO_IMPL(
    UFormattedNumberRange,
    UFormattedNumberRangeImpl,
    UFormattedNumberRangeApiHelper,
    unumrf)


const UFormattedNumberRangeData* number::impl::validateUFormattedNumberRange(
        const UFormattedNumberRange* uresult, UErrorCode& status) {
    const auto* result = UFormattedNumberRangeApiHelper::validate(uresult, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    return &result->fData;
}


U_CAPI UNumberRangeFormatter* U_EXPORT2
unumrf_openForSkeletonWithCollapseAndIdentityFallback(
        const char16_t* skeleton,
        int32_t skeletonLen,
        UNumberRangeCollapse collapse,
        UNumberRangeIdentityFallback identityFallback,
        const char* locale,
        UParseError* perror,
        UErrorCode* ec) {
    auto* impl = new UNumberRangeFormatterData();
    if (impl == nullptr) {
        *ec = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    // Readonly-alias constructor (first argument is whether we are NUL-terminated)
    UnicodeString skeletonString(skeletonLen == -1, skeleton, skeletonLen);
    UParseError tempParseError;
    impl->fFormatter = NumberRangeFormatter::withLocale(locale)
        .numberFormatterBoth(NumberFormatter::forSkeleton(skeletonString, (perror == nullptr) ? tempParseError : *perror, *ec))
        .collapse(collapse)
        .identityFallback(identityFallback);
    return impl->exportForC();
}

U_CAPI void U_EXPORT2
unumrf_formatDoubleRange(
        const UNumberRangeFormatter* uformatter,
        double first,
        double second,
        UFormattedNumberRange* uresult,
        UErrorCode* ec) {
    const UNumberRangeFormatterData* formatter = UNumberRangeFormatterData::validate(uformatter, *ec);
    auto* result = UFormattedNumberRangeApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) { return; }

    result->fData.resetString();
    result->fData.quantity1.clear();
    result->fData.quantity2.clear();
    result->fData.quantity1.setToDouble(first);
    result->fData.quantity2.setToDouble(second);
    formatter->fFormatter.formatImpl(result->fData, first == second, *ec);
}

U_CAPI void U_EXPORT2
unumrf_formatDecimalRange(
        const UNumberRangeFormatter* uformatter,
        const char* first, int32_t firstLen,
        const char* second, int32_t secondLen,
        UFormattedNumberRange* uresult,
        UErrorCode* ec) {
    const UNumberRangeFormatterData* formatter = UNumberRangeFormatterData::validate(uformatter, *ec);
    auto* result = UFormattedNumberRangeApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) { return; }

    result->fData.resetString();
    result->fData.quantity1.clear();
    result->fData.quantity2.clear();
    result->fData.quantity1.setToDecNumber({first, firstLen}, *ec);
    result->fData.quantity2.setToDecNumber({second, secondLen}, *ec);
    formatter->fFormatter.formatImpl(result->fData, first == second, *ec);
}

U_CAPI UNumberRangeIdentityResult U_EXPORT2
unumrf_resultGetIdentityResult(
        const UFormattedNumberRange* uresult,
        UErrorCode* ec) {
    const auto* result = UFormattedNumberRangeApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) {
        return UNUM_IDENTITY_RESULT_COUNT;
    }
    return result->fData.identityResult;
}

U_CAPI int32_t U_EXPORT2
unumrf_resultGetFirstDecimalNumber(
        const UFormattedNumberRange* uresult,
        char* dest,
        int32_t destCapacity,
        UErrorCode* ec) {
    const auto* result = UFormattedNumberRangeApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) {
        return 0;
    }
    DecNum decnum;
    return result->fData.quantity1.toDecNum(decnum, *ec)
        .toCharString(*ec)
        .extract(dest, destCapacity, *ec);
}

U_CAPI int32_t U_EXPORT2
unumrf_resultGetSecondDecimalNumber(
        const UFormattedNumberRange* uresult,
        char* dest,
        int32_t destCapacity,
        UErrorCode* ec) {
    const auto* result = UFormattedNumberRangeApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) {
        return 0;
    }
    DecNum decnum;
    return result->fData.quantity2
        .toDecNum(decnum, *ec)
        .toCharString(*ec)
        .extract(dest, destCapacity, *ec);
}

U_CAPI void U_EXPORT2
unumrf_close(UNumberRangeFormatter* f) {
    UErrorCode localStatus = U_ZERO_ERROR;
    const UNumberRangeFormatterData* impl = UNumberRangeFormatterData::validate(f, localStatus);
    delete impl;
}


#endif /* #if !UCONFIG_NO_FORMATTING */
