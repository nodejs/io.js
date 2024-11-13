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
#include "number_decnum.h"
#include "unicode/numberformatter.h"
#include "unicode/unumberformatter.h"
#include "unicode/simplenumberformatter.h"
#include "unicode/usimplenumberformatter.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;


U_NAMESPACE_BEGIN
namespace number::impl {

/**
 * Implementation class for UNumberFormatter. Wraps a LocalizedNumberFormatter.
 */
struct UNumberFormatterData : public UMemory,
        // Magic number as ASCII == "NFR" (NumberFormatteR)
        public IcuCApiHelper<UNumberFormatter, UNumberFormatterData, 0x4E465200> {
    LocalizedNumberFormatter fFormatter;
};

/**
 * Implementation class for USimpleNumber. Wraps a SimpleNumberFormatter.
 */
struct USimpleNumberData : public UMemory,
        // Magic number as ASCII == "SNM" (SimpleNuMber)
        public IcuCApiHelper<USimpleNumber, USimpleNumberData, 0x534E4D00> {
    SimpleNumber fNumber;
};

/**
 * Implementation class for USimpleNumberFormatter. Wraps a SimpleNumberFormatter.
 */
struct USimpleNumberFormatterData : public UMemory,
        // Magic number as ASCII == "SNF" (SimpleNumberFormatter)
        public IcuCApiHelper<USimpleNumberFormatter, USimpleNumberFormatterData, 0x534E4600> {
    SimpleNumberFormatter fFormatter;
};

struct UFormattedNumberImpl;

// Magic number as ASCII == "FDN" (FormatteDNumber)
typedef IcuCApiHelper<UFormattedNumber, UFormattedNumberImpl, 0x46444E00> UFormattedNumberApiHelper;

struct UFormattedNumberImpl : public UFormattedValueImpl, public UFormattedNumberApiHelper {
    UFormattedNumberImpl();
    ~UFormattedNumberImpl();

    FormattedNumber fImpl;
    UFormattedNumberData fData;

    void setTo(FormattedNumber value);
};

UFormattedNumberImpl::UFormattedNumberImpl()
        : fImpl(&fData) {
    fFormattedValue = &fImpl;
}

UFormattedNumberImpl::~UFormattedNumberImpl() {
    // Disown the data from fImpl so it doesn't get deleted twice
    fImpl.fData = nullptr;
}

void UFormattedNumberImpl::setTo(FormattedNumber value) {
    fData = std::move(*value.fData);
}

} // namespace number::impl
U_NAMESPACE_END


UPRV_FORMATTED_VALUE_CAPI_NO_IMPLTYPE_AUTO_IMPL(
    UFormattedNumber,
    UFormattedNumberImpl,
    UFormattedNumberApiHelper,
    unumf)


const DecimalQuantity* icu::number::impl::validateUFormattedNumberToDecimalQuantity(
        const UFormattedNumber* uresult, UErrorCode& status) {
    const auto* result = UFormattedNumberApiHelper::validate(uresult, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    return &result->fData.quantity;
}



U_CAPI UNumberFormatter* U_EXPORT2
unumf_openForSkeletonAndLocale(const char16_t* skeleton, int32_t skeletonLen, const char* locale,
                               UErrorCode* ec) {
    auto* impl = new UNumberFormatterData();
    if (impl == nullptr) {
        *ec = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    // Readonly-alias constructor (first argument is whether we are NUL-terminated)
    UnicodeString skeletonString(skeletonLen == -1, skeleton, skeletonLen);
    impl->fFormatter = NumberFormatter::forSkeleton(skeletonString, *ec).locale(locale);
    return impl->exportForC();
}

U_CAPI UNumberFormatter* U_EXPORT2
unumf_openForSkeletonAndLocaleWithError(const char16_t* skeleton, int32_t skeletonLen, const char* locale,
                                         UParseError* perror, UErrorCode* ec) {
    auto* impl = new UNumberFormatterData();
    if (impl == nullptr) {
        *ec = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    // Readonly-alias constructor (first argument is whether we are NUL-terminated)
    UnicodeString skeletonString(skeletonLen == -1, skeleton, skeletonLen);
    UParseError tempParseError;
    impl->fFormatter = NumberFormatter::forSkeleton(skeletonString, (perror == nullptr) ? tempParseError : *perror, *ec).locale(locale);
    return impl->exportForC();
}

U_CAPI void U_EXPORT2
unumf_formatInt(const UNumberFormatter* uformatter, int64_t value, UFormattedNumber* uresult,
                UErrorCode* ec) {
    const UNumberFormatterData* formatter = UNumberFormatterData::validate(uformatter, *ec);
    auto* result = UFormattedNumberApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) { return; }

    result->fData.resetString();
    result->fData.quantity.clear();
    result->fData.quantity.setToLong(value);
    formatter->fFormatter.formatImpl(&result->fData, *ec);
}

U_CAPI void U_EXPORT2
unumf_formatDouble(const UNumberFormatter* uformatter, double value, UFormattedNumber* uresult,
                   UErrorCode* ec) {
    const UNumberFormatterData* formatter = UNumberFormatterData::validate(uformatter, *ec);
    auto* result = UFormattedNumberApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) { return; }

    result->fData.resetString();
    result->fData.quantity.clear();
    result->fData.quantity.setToDouble(value);
    formatter->fFormatter.formatImpl(&result->fData, *ec);
}

U_CAPI void U_EXPORT2
unumf_formatDecimal(const UNumberFormatter* uformatter, const char* value, int32_t valueLen,
                    UFormattedNumber* uresult, UErrorCode* ec) {
    const UNumberFormatterData* formatter = UNumberFormatterData::validate(uformatter, *ec);
    auto* result = UFormattedNumberApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) { return; }

    result->fData.resetString();
    result->fData.quantity.clear();
    result->fData.quantity.setToDecNumber({value, valueLen}, *ec);
    if (U_FAILURE(*ec)) { return; }
    formatter->fFormatter.formatImpl(&result->fData, *ec);
}

U_CAPI int32_t U_EXPORT2
unumf_resultToString(const UFormattedNumber* uresult, char16_t* buffer, int32_t bufferCapacity,
                     UErrorCode* ec) {
    const auto* result = UFormattedNumberApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) { return 0; }

    if (buffer == nullptr ? bufferCapacity != 0 : bufferCapacity < 0) {
        *ec = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    return result->fData.toTempString(*ec).extract(buffer, bufferCapacity, *ec);
}

U_CAPI UBool U_EXPORT2
unumf_resultNextFieldPosition(const UFormattedNumber* uresult, UFieldPosition* ufpos, UErrorCode* ec) {
    const auto* result = UFormattedNumberApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) { return false; }

    if (ufpos == nullptr) {
        *ec = U_ILLEGAL_ARGUMENT_ERROR;
        return false;
    }

    FieldPosition fp;
    fp.setField(ufpos->field);
    fp.setBeginIndex(ufpos->beginIndex);
    fp.setEndIndex(ufpos->endIndex);
    bool retval = result->fData.nextFieldPosition(fp, *ec);
    ufpos->beginIndex = fp.getBeginIndex();
    ufpos->endIndex = fp.getEndIndex();
    // NOTE: MSVC sometimes complains when implicitly converting between bool and UBool
    return retval ? true : false;
}

U_CAPI void U_EXPORT2
unumf_resultGetAllFieldPositions(const UFormattedNumber* uresult, UFieldPositionIterator* ufpositer,
                                 UErrorCode* ec) {
    const auto* result = UFormattedNumberApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) { return; }

    if (ufpositer == nullptr) {
        *ec = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    auto* fpi = reinterpret_cast<FieldPositionIterator*>(ufpositer);
    FieldPositionIteratorHandler fpih(fpi, *ec);
    result->fData.getAllFieldPositions(fpih, *ec);
}

U_CAPI int32_t U_EXPORT2
unumf_resultToDecimalNumber(
        const UFormattedNumber* uresult,
        char* dest,
        int32_t destCapacity,
        UErrorCode* ec) {
    const auto* result = UFormattedNumberApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) {
        return 0;
    }
    DecNum decnum;
    return result->fData.quantity
        .toDecNum(decnum, *ec)
        .toCharString(*ec)
        .extract(dest, destCapacity, *ec);
}

U_CAPI void U_EXPORT2
unumf_close(UNumberFormatter* f) {
    UErrorCode localStatus = U_ZERO_ERROR;
    const UNumberFormatterData* impl = UNumberFormatterData::validate(f, localStatus);
    delete impl;
}


///// SIMPLE NUMBER FORMATTER /////

U_CAPI USimpleNumber* U_EXPORT2
usnum_openForInt64(int64_t value, UErrorCode* ec) {
    auto* number = new USimpleNumberData();
    if (number == nullptr) {
        *ec = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    number->fNumber = SimpleNumber::forInt64(value, *ec);
    return number->exportForC();
}

U_CAPI void U_EXPORT2
usnum_setToInt64(USimpleNumber* unumber, int64_t value, UErrorCode* ec) {
    auto* number = USimpleNumberData::validate(unumber, *ec);
    if (U_FAILURE(*ec)) {
        return;
    }
    number->fNumber = SimpleNumber::forInt64(value, *ec);
}

U_CAPI void U_EXPORT2
usnum_multiplyByPowerOfTen(USimpleNumber* unumber, int32_t power, UErrorCode* ec) {
    auto* number = USimpleNumberData::validate(unumber, *ec);
    if (U_FAILURE(*ec)) {
        return;
    }
    number->fNumber.multiplyByPowerOfTen(power, *ec);
}

U_CAPI void U_EXPORT2
usnum_roundTo(USimpleNumber* unumber, int32_t position, UNumberFormatRoundingMode roundingMode, UErrorCode* ec) {
    auto* number = USimpleNumberData::validate(unumber, *ec);
    if (U_FAILURE(*ec)) {
        return;
    }
    number->fNumber.roundTo(position, roundingMode, *ec);
}

U_CAPI void U_EXPORT2
usnum_setMinimumIntegerDigits(USimpleNumber* unumber, int32_t minimumIntegerDigits, UErrorCode* ec) {
    auto* number = USimpleNumberData::validate(unumber, *ec);
    if (U_FAILURE(*ec)) {
        return;
    }
    number->fNumber.setMinimumIntegerDigits(minimumIntegerDigits, *ec);
}

U_CAPI void U_EXPORT2
usnum_setMinimumFractionDigits(USimpleNumber* unumber, int32_t minimumFractionDigits, UErrorCode* ec) {
    auto* number = USimpleNumberData::validate(unumber, *ec);
    if (U_FAILURE(*ec)) {
        return;
    }
    number->fNumber.setMinimumFractionDigits(minimumFractionDigits, *ec);
}

U_CAPI void U_EXPORT2
usnum_setMaximumIntegerDigits(USimpleNumber* unumber, int32_t maximumIntegerDigits, UErrorCode* ec) {
    auto* number = USimpleNumberData::validate(unumber, *ec);
    if (U_FAILURE(*ec)) {
        return;
    }
    number->fNumber.setMaximumIntegerDigits(maximumIntegerDigits, *ec);
}

U_CAPI void U_EXPORT2
usnum_setSign(USimpleNumber* unumber, USimpleNumberSign sign, UErrorCode* ec) {
    auto* number = USimpleNumberData::validate(unumber, *ec);
    if (U_FAILURE(*ec)) {
        return;
    }
    number->fNumber.setSign(sign, *ec);
}

U_CAPI USimpleNumberFormatter* U_EXPORT2
usnumf_openForLocale(const char* locale, UErrorCode* ec) {
    auto* impl = new USimpleNumberFormatterData();
    if (impl == nullptr) {
        *ec = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    impl->fFormatter = SimpleNumberFormatter::forLocale(locale, *ec);
    return impl->exportForC();
}

U_CAPI USimpleNumberFormatter* U_EXPORT2
usnumf_openForLocaleAndGroupingStrategy(
       const char* locale, UNumberGroupingStrategy groupingStrategy, UErrorCode* ec) {
    auto* impl = new USimpleNumberFormatterData();
    if (impl == nullptr) {
        *ec = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    impl->fFormatter = SimpleNumberFormatter::forLocaleAndGroupingStrategy(locale, groupingStrategy, *ec);
    return impl->exportForC();
}

U_CAPI void U_EXPORT2
usnumf_format(
        const USimpleNumberFormatter* uformatter,
        USimpleNumber* unumber,
        UFormattedNumber* uresult,
        UErrorCode* ec) {
    const auto* formatter = USimpleNumberFormatterData::validate(uformatter, *ec);
    auto* number = USimpleNumberData::validate(unumber, *ec);
    auto* result = UFormattedNumberApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) {
        return;
    }
    auto localResult = formatter->fFormatter.format(std::move(number->fNumber), *ec);
    if (U_FAILURE(*ec)) {
        return;
    }
    result->setTo(std::move(localResult));
}

U_CAPI void U_EXPORT2
usnumf_formatInt64(
        const USimpleNumberFormatter* uformatter,
        int64_t value,
        UFormattedNumber* uresult,
        UErrorCode* ec) {
    const auto* formatter = USimpleNumberFormatterData::validate(uformatter, *ec);
    auto* result = UFormattedNumberApiHelper::validate(uresult, *ec);
    if (U_FAILURE(*ec)) {
        return;
    }
    auto localResult = formatter->fFormatter.formatInt64(value, *ec);
    result->setTo(std::move(localResult)); 
}

U_CAPI void U_EXPORT2
usnum_close(USimpleNumber* unumber) {
    UErrorCode localStatus = U_ZERO_ERROR;
    const USimpleNumberData* impl = USimpleNumberData::validate(unumber, localStatus);
    delete impl;
}

U_CAPI void U_EXPORT2
usnumf_close(USimpleNumberFormatter* uformatter) {
    UErrorCode localStatus = U_ZERO_ERROR;
    const USimpleNumberFormatterData* impl = USimpleNumberFormatterData::validate(uformatter, localStatus);
    delete impl;
}


#endif /* #if !UCONFIG_NO_FORMATTING */



























