// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
* Copyright (c) 2004-2016, International Business Machines
* Corporation and others.  All Rights Reserved.
**********************************************************************
* Author: Alan Liu
* Created: April 20, 2004
* Since: ICU 3.0
**********************************************************************
*/
#include "utypeinfo.h"  // for 'typeid' to work
#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/measfmt.h"
#include "unicode/numfmt.h"
#include "currfmt.h"
#include "unicode/localpointer.h"
#include "resource.h"
#include "unicode/simpleformatter.h"
#include "quantityformatter.h"
#include "unicode/plurrule.h"
#include "unicode/decimfmt.h"
#include "uresimp.h"
#include "unicode/ures.h"
#include "unicode/ustring.h"
#include "ureslocs.h"
#include "cstring.h"
#include "mutex.h"
#include "ucln_in.h"
#include "unicode/listformatter.h"
#include "charstr.h"
#include "unicode/putil.h"
#include "unicode/smpdtfmt.h"
#include "uassert.h"
#include "unicode/numberformatter.h"
#include "number_longnames.h"
#include "number_utypes.h"

#include "sharednumberformat.h"
#include "sharedpluralrules.h"
#include "standardplural.h"
#include "unifiedcache.h"


U_NAMESPACE_BEGIN

using number::impl::UFormattedNumberData;

static constexpr int32_t WIDTH_INDEX_COUNT = UMEASFMT_WIDTH_NARROW + 1;

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(MeasureFormat)

// Used to format durations like 5:47 or 21:35:42.
class NumericDateFormatters : public UMemory {
public:
    // Formats like H:mm
    UnicodeString hourMinute;

    // formats like M:ss
    UnicodeString minuteSecond;

    // formats like H:mm:ss
    UnicodeString hourMinuteSecond;

    // Constructor that takes the actual patterns for hour-minute,
    // minute-second, and hour-minute-second respectively.
    NumericDateFormatters(
            const UnicodeString &hm,
            const UnicodeString &ms,
            const UnicodeString &hms) :
            hourMinute(hm),
            minuteSecond(ms),
            hourMinuteSecond(hms) {
    }
private:
    NumericDateFormatters(const NumericDateFormatters &other);
    NumericDateFormatters &operator=(const NumericDateFormatters &other);
};

static UMeasureFormatWidth getRegularWidth(UMeasureFormatWidth width) {
    if (width >= WIDTH_INDEX_COUNT) {
        return UMEASFMT_WIDTH_NARROW;
    }
    return width;
}

static UNumberUnitWidth getUnitWidth(UMeasureFormatWidth width) {
    switch (width) {
    case UMEASFMT_WIDTH_WIDE:
        return UNUM_UNIT_WIDTH_FULL_NAME;
    case UMEASFMT_WIDTH_NARROW:
    case UMEASFMT_WIDTH_NUMERIC:
        return UNUM_UNIT_WIDTH_NARROW;
    case UMEASFMT_WIDTH_SHORT:
    default:
        return UNUM_UNIT_WIDTH_SHORT;
    }
}

/**
 * Instances contain all MeasureFormat specific data for a particular locale.
 * This data is cached. It is never copied, but is shared via shared pointers.
 *
 * Note: We might change the cache data to have an array[WIDTH_INDEX_COUNT] of
 * complete sets of unit & per patterns,
 * to correspond to the resource data and its aliases.
 *
 * TODO: Maybe store more sparsely in general, with pointers rather than potentially-empty objects.
 */
class MeasureFormatCacheData : public SharedObject {
public:

    /**
     * Redirection data from root-bundle, top-level sideways aliases.
     * - UMEASFMT_WIDTH_COUNT: initial value, just fall back to root
     * - UMEASFMT_WIDTH_WIDE/SHORT/NARROW: sideways alias for missing data
     */
    UMeasureFormatWidth widthFallback[WIDTH_INDEX_COUNT];

    MeasureFormatCacheData();
    virtual ~MeasureFormatCacheData();

    void adoptCurrencyFormat(int32_t widthIndex, NumberFormat *nfToAdopt) {
        delete currencyFormats[widthIndex];
        currencyFormats[widthIndex] = nfToAdopt;
    }
    const NumberFormat *getCurrencyFormat(UMeasureFormatWidth width) const {
        return currencyFormats[getRegularWidth(width)];
    }
    void adoptIntegerFormat(NumberFormat *nfToAdopt) {
        delete integerFormat;
        integerFormat = nfToAdopt;
    }
    const NumberFormat *getIntegerFormat() const {
        return integerFormat;
    }
    void adoptNumericDateFormatters(NumericDateFormatters *formattersToAdopt) {
        delete numericDateFormatters;
        numericDateFormatters = formattersToAdopt;
    }
    const NumericDateFormatters *getNumericDateFormatters() const {
        return numericDateFormatters;
    }

private:
    NumberFormat* currencyFormats[WIDTH_INDEX_COUNT];
    NumberFormat* integerFormat;
    NumericDateFormatters* numericDateFormatters;

    MeasureFormatCacheData(const MeasureFormatCacheData &other);
    MeasureFormatCacheData &operator=(const MeasureFormatCacheData &other);
};

MeasureFormatCacheData::MeasureFormatCacheData()
        : integerFormat(nullptr), numericDateFormatters(nullptr) {
    for (int32_t i = 0; i < WIDTH_INDEX_COUNT; ++i) {
        widthFallback[i] = UMEASFMT_WIDTH_COUNT;
    }
    memset(currencyFormats, 0, sizeof(currencyFormats));
}

MeasureFormatCacheData::~MeasureFormatCacheData() {
    for (int32_t i = 0; i < UPRV_LENGTHOF(currencyFormats); ++i) {
        delete currencyFormats[i];
    }
    // Note: the contents of 'dnams' are pointers into the resource bundle
    delete integerFormat;
    delete numericDateFormatters;
}

static UBool isCurrency(const MeasureUnit &unit) {
    return (uprv_strcmp(unit.getType(), "currency") == 0);
}

static UBool getString(
        const UResourceBundle *resource,
        UnicodeString &result,
        UErrorCode &status) {
    int32_t len = 0;
    const char16_t *resStr = ures_getString(resource, &len, &status);
    if (U_FAILURE(status)) {
        return false;
    }
    result.setTo(true, resStr, len);
    return true;
}

static UnicodeString loadNumericDateFormatterPattern(
        const UResourceBundle *resource,
        const char *pattern,
        UErrorCode &status) {
    UnicodeString result;
    if (U_FAILURE(status)) {
        return result;
    }
    CharString chs;
    chs.append("durationUnits", status)
            .append("/", status).append(pattern, status);
    LocalUResourceBundlePointer patternBundle(
            ures_getByKeyWithFallback(
                resource,
                chs.data(),
                nullptr,
                &status));
    if (U_FAILURE(status)) {
        return result;
    }
    getString(patternBundle.getAlias(), result, status);
    // Replace 'h' with 'H'
    int32_t len = result.length();
    char16_t *buffer = result.getBuffer(len);
    for (int32_t i = 0; i < len; ++i) {
        if (buffer[i] == 0x68) { // 'h'
            buffer[i] = 0x48; // 'H'
        }
    }
    result.releaseBuffer(len);
    return result;
}

static NumericDateFormatters *loadNumericDateFormatters(
        const UResourceBundle *resource,
        UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    NumericDateFormatters *result = new NumericDateFormatters(
        loadNumericDateFormatterPattern(resource, "hm", status),
        loadNumericDateFormatterPattern(resource, "ms", status),
        loadNumericDateFormatterPattern(resource, "hms", status));
    if (U_FAILURE(status)) {
        delete result;
        return nullptr;
    }
    return result;
}

template<> 
const MeasureFormatCacheData *LocaleCacheKey<MeasureFormatCacheData>::createObject(
        const void * /*unused*/, UErrorCode &status) const {
    const char *localeId = fLoc.getName();
    LocalUResourceBundlePointer unitsBundle(ures_open(U_ICUDATA_UNIT, localeId, &status));
    static UNumberFormatStyle currencyStyles[] = {
            UNUM_CURRENCY_PLURAL, UNUM_CURRENCY_ISO, UNUM_CURRENCY};
    LocalPointer<MeasureFormatCacheData> result(new MeasureFormatCacheData(), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    result->adoptNumericDateFormatters(loadNumericDateFormatters(
            unitsBundle.getAlias(), status));
    if (U_FAILURE(status)) {
        return nullptr;
    }

    for (int32_t i = 0; i < WIDTH_INDEX_COUNT; ++i) {
        // NumberFormat::createInstance can erase warning codes from status, so pass it
        // a separate status instance
        UErrorCode localStatus = U_ZERO_ERROR;
        result->adoptCurrencyFormat(i, NumberFormat::createInstance(
                localeId, currencyStyles[i], localStatus));
        if (localStatus != U_ZERO_ERROR) {
            status = localStatus;
        }
        if (U_FAILURE(status)) {
            return nullptr;
        }
    }
    NumberFormat *inf = NumberFormat::createInstance(
            localeId, UNUM_DECIMAL, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    inf->setMaximumFractionDigits(0);
    DecimalFormat *decfmt = dynamic_cast<DecimalFormat *>(inf);
    if (decfmt != nullptr) {
        decfmt->setRoundingMode(DecimalFormat::kRoundDown);
    }
    result->adoptIntegerFormat(inf);
    result->addRef();
    return result.orphan();
}

static UBool isTimeUnit(const MeasureUnit &mu, const char *tu) {
    return uprv_strcmp(mu.getType(), "duration") == 0 &&
            uprv_strcmp(mu.getSubtype(), tu) == 0;
}

// Converts a composite measure into hours-minutes-seconds and stores at hms
// array. [0] is hours; [1] is minutes; [2] is seconds. Returns a bit map of
// units found: 1=hours, 2=minutes, 4=seconds. For example, if measures
// contains hours-minutes, this function would return 3.
//
// If measures cannot be converted into hours, minutes, seconds or if amounts
// are negative, or if hours, minutes, seconds are out of order, returns 0.
static int32_t toHMS(
        const Measure *measures,
        int32_t measureCount,
        Formattable *hms,
        UErrorCode &status) {
    if (U_FAILURE(status)) {
        return 0;
    }
    int32_t result = 0;
    if (U_FAILURE(status)) {
        return 0;
    }
    // We use copy constructor to ensure that both sides of equality operator
    // are instances of MeasureUnit base class and not a subclass. Otherwise,
    // operator== will immediately return false.
    for (int32_t i = 0; i < measureCount; ++i) {
        if (isTimeUnit(measures[i].getUnit(), "hour")) {
            // hour must come first
            if (result >= 1) {
                return 0;
            }
            hms[0] = measures[i].getNumber();
            if (hms[0].getDouble() < 0.0) {
                return 0;
            }
            result |= 1;
        } else if (isTimeUnit(measures[i].getUnit(), "minute")) {
            // minute must come after hour
            if (result >= 2) {
                return 0;
            }
            hms[1] = measures[i].getNumber();
            if (hms[1].getDouble() < 0.0) {
                return 0;
            }
            result |= 2;
        } else if (isTimeUnit(measures[i].getUnit(), "second")) {
            // second must come after hour and minute
            if (result >= 4) {
                return 0;
            }
            hms[2] = measures[i].getNumber();
            if (hms[2].getDouble() < 0.0) {
                return 0;
            }
            result |= 4;
        } else {
            return 0;
        }
    }
    return result;
}


MeasureFormat::MeasureFormat(
        const Locale &locale, UMeasureFormatWidth w, UErrorCode &status)
        : cache(nullptr),
          numberFormat(nullptr),
          pluralRules(nullptr),
          fWidth(w),
          listFormatter(nullptr) {
    initMeasureFormat(locale, w, nullptr, status);
}

MeasureFormat::MeasureFormat(
        const Locale &locale,
        UMeasureFormatWidth w,
        NumberFormat *nfToAdopt,
        UErrorCode &status) 
        : cache(nullptr),
          numberFormat(nullptr),
          pluralRules(nullptr),
          fWidth(w),
          listFormatter(nullptr) {
    initMeasureFormat(locale, w, nfToAdopt, status);
}

MeasureFormat::MeasureFormat(const MeasureFormat &other) :
        Format(other),
        cache(other.cache),
        numberFormat(other.numberFormat),
        pluralRules(other.pluralRules),
        fWidth(other.fWidth),
        listFormatter(nullptr) {
    cache->addRef();
    numberFormat->addRef();
    pluralRules->addRef();
    if (other.listFormatter != nullptr) {
        listFormatter = new ListFormatter(*other.listFormatter);
    }
}

MeasureFormat &MeasureFormat::operator=(const MeasureFormat &other) {
    if (this == &other) {
        return *this;
    }
    Format::operator=(other);
    SharedObject::copyPtr(other.cache, cache);
    SharedObject::copyPtr(other.numberFormat, numberFormat);
    SharedObject::copyPtr(other.pluralRules, pluralRules);
    fWidth = other.fWidth;
    delete listFormatter;
    if (other.listFormatter != nullptr) {
        listFormatter = new ListFormatter(*other.listFormatter);
    } else {
        listFormatter = nullptr;
    }
    return *this;
}

MeasureFormat::MeasureFormat() :
        cache(nullptr),
        numberFormat(nullptr),
        pluralRules(nullptr),
        fWidth(UMEASFMT_WIDTH_SHORT),
        listFormatter(nullptr) {
}

MeasureFormat::~MeasureFormat() {
    if (cache != nullptr) {
        cache->removeRef();
    }
    if (numberFormat != nullptr) {
        numberFormat->removeRef();
    }
    if (pluralRules != nullptr) {
        pluralRules->removeRef();
    }
    delete listFormatter;
}

bool MeasureFormat::operator==(const Format &other) const {
    if (this == &other) { // Same object, equal
        return true;
    }
    if (!Format::operator==(other)) {
        return false;
    }
    const MeasureFormat &rhs = static_cast<const MeasureFormat &>(other);

    // Note: Since the ListFormatter depends only on Locale and width, we
    // don't have to check it here.

    // differing widths aren't equivalent
    if (fWidth != rhs.fWidth) {
        return false;
    }
    // Width the same check locales.
    // We don't need to check locales if both objects have same cache.
    if (cache != rhs.cache) {
        UErrorCode status = U_ZERO_ERROR;
        const char *localeId = getLocaleID(status);
        const char *rhsLocaleId = rhs.getLocaleID(status);
        if (U_FAILURE(status)) {
            // On failure, assume not equal
            return false;
        }
        if (uprv_strcmp(localeId, rhsLocaleId) != 0) {
            return false;
        }
    }
    // Locales same, check NumberFormat if shared data differs.
    return (
            numberFormat == rhs.numberFormat ||
            **numberFormat == **rhs.numberFormat);
}

MeasureFormat *MeasureFormat::clone() const {
    return new MeasureFormat(*this);
}

UnicodeString &MeasureFormat::format(
        const Formattable &obj,
        UnicodeString &appendTo,
        FieldPosition &pos,
        UErrorCode &status) const {
    if (U_FAILURE(status)) return appendTo;
    if (obj.getType() == Formattable::kObject) {
        const UObject* formatObj = obj.getObject();
        const Measure* amount = dynamic_cast<const Measure*>(formatObj);
        if (amount != nullptr) {
            return formatMeasure(
                    *amount, **numberFormat, appendTo, pos, status);
        }
    }
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return appendTo;
}

void MeasureFormat::parseObject(
        const UnicodeString & /*source*/,
        Formattable & /*result*/,
        ParsePosition& /*pos*/) const {
}

UnicodeString &MeasureFormat::formatMeasurePerUnit(
        const Measure &measure,
        const MeasureUnit &perUnit,
        UnicodeString &appendTo,
        FieldPosition &pos,
        UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return appendTo;
    }
    const auto* df = dynamic_cast<const DecimalFormat*>(&getNumberFormatInternal());
    if (df == nullptr) {
        // Don't know how to handle other types of NumberFormat
        status = U_UNSUPPORTED_ERROR;
        return appendTo;
    }
    UFormattedNumberData result;
    if (const auto* lnf = df->toNumberFormatter(status)) {
        result.quantity.setToDouble(measure.getNumber().getDouble(status));
        lnf->unit(measure.getUnit())
            .perUnit(perUnit)
            .unitWidth(getUnitWidth(fWidth))
            .formatImpl(&result, status);
    }
    DecimalFormat::fieldPositionHelper(result, pos, appendTo.length(), status);
    appendTo.append(result.toTempString(status));
    return appendTo;
}

UnicodeString &MeasureFormat::formatMeasures(
        const Measure *measures,
        int32_t measureCount,
        UnicodeString &appendTo,
        FieldPosition &pos,
        UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return appendTo;
    }
    if (measureCount == 0) {
        return appendTo;
    }
    if (measureCount == 1) {
        return formatMeasure(measures[0], **numberFormat, appendTo, pos, status);
    }
    if (fWidth == UMEASFMT_WIDTH_NUMERIC) {
        Formattable hms[3];
        int32_t bitMap = toHMS(measures, measureCount, hms, status);
        if (bitMap > 0) {
            return formatNumeric(hms, bitMap, appendTo, status);
        }
    }
    if (pos.getField() != FieldPosition::DONT_CARE) {
        return formatMeasuresSlowTrack(
                measures, measureCount, appendTo, pos, status);
    }
    UnicodeString *results = new UnicodeString[measureCount];
    if (results == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return appendTo;
    }
    for (int32_t i = 0; i < measureCount; ++i) {
        const NumberFormat *nf = cache->getIntegerFormat();
        if (i == measureCount - 1) {
            nf = numberFormat->get();
        }
        formatMeasure(
                measures[i],
                *nf,
                results[i],
                pos,
                status);
    }
    listFormatter->format(results, measureCount, appendTo, status);
    delete [] results;
    return appendTo;
}

UnicodeString MeasureFormat::getUnitDisplayName(const MeasureUnit& unit, UErrorCode& status) const {
    return number::impl::LongNameHandler::getUnitDisplayName(
        getLocale(status),
        unit,
        getUnitWidth(fWidth),
        status);
}

void MeasureFormat::initMeasureFormat(
        const Locale &locale,
        UMeasureFormatWidth w,
        NumberFormat *nfToAdopt,
        UErrorCode &status) {
    static const UListFormatterWidth listWidths[] = {
        ULISTFMT_WIDTH_WIDE,
        ULISTFMT_WIDTH_SHORT,
        ULISTFMT_WIDTH_NARROW};
    LocalPointer<NumberFormat> nf(nfToAdopt);
    if (U_FAILURE(status)) {
        return;
    }
    const char *name = locale.getName();
    setLocaleIDs(name, name);

    UnifiedCache::getByLocale(locale, cache, status);
    if (U_FAILURE(status)) {
        return;
    }

    const SharedPluralRules *pr = PluralRules::createSharedInstance(
            locale, UPLURAL_TYPE_CARDINAL, status);
    if (U_FAILURE(status)) {
        return;
    }
    SharedObject::copyPtr(pr, pluralRules);
    pr->removeRef();
    if (nf.isNull()) {
        // TODO: Clean this up
        const SharedNumberFormat *shared = NumberFormat::createSharedInstance(
                locale, UNUM_DECIMAL, status);
        if (U_FAILURE(status)) {
            return;
        }
        SharedObject::copyPtr(shared, numberFormat);
        shared->removeRef();
    } else {
        adoptNumberFormat(nf.orphan(), status);
        if (U_FAILURE(status)) {
            return;
        }
    }
    fWidth = w;
    delete listFormatter;
    listFormatter = ListFormatter::createInstance(
            locale,
            ULISTFMT_TYPE_UNITS,
            listWidths[getRegularWidth(fWidth)],
            status);
}

void MeasureFormat::adoptNumberFormat(
        NumberFormat *nfToAdopt, UErrorCode &status) {
    LocalPointer<NumberFormat> nf(nfToAdopt);
    if (U_FAILURE(status)) {
        return;
    }
    SharedNumberFormat *shared = new SharedNumberFormat(nf.getAlias());
    if (shared == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    nf.orphan();
    SharedObject::copyPtr(shared, numberFormat);
}

UBool MeasureFormat::setMeasureFormatLocale(const Locale &locale, UErrorCode &status) {
    if (U_FAILURE(status) || locale == getLocale(status)) {
        return false;
    }
    initMeasureFormat(locale, fWidth, nullptr, status);
    return U_SUCCESS(status);
} 

const NumberFormat &MeasureFormat::getNumberFormatInternal() const {
    return **numberFormat;
}

const NumberFormat &MeasureFormat::getCurrencyFormatInternal() const {
    return *cache->getCurrencyFormat(UMEASFMT_WIDTH_NARROW);
}

const PluralRules &MeasureFormat::getPluralRules() const {
    return **pluralRules;
}

Locale MeasureFormat::getLocale(UErrorCode &status) const {
    return Format::getLocale(ULOC_VALID_LOCALE, status);
}

const char *MeasureFormat::getLocaleID(UErrorCode &status) const {
    return Format::getLocaleID(ULOC_VALID_LOCALE, status);
}

UnicodeString &MeasureFormat::formatMeasure(
        const Measure &measure,
        const NumberFormat &nf,
        UnicodeString &appendTo,
        FieldPosition &pos,
        UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return appendTo;
    }
    const Formattable& amtNumber = measure.getNumber();
    const MeasureUnit& amtUnit = measure.getUnit();
    if (isCurrency(amtUnit)) {
        char16_t isoCode[4];
        u_charsToUChars(amtUnit.getSubtype(), isoCode, 4);
        return cache->getCurrencyFormat(fWidth)->format(
                new CurrencyAmount(amtNumber, isoCode, status),
                appendTo,
                pos,
                status);
    }
    const auto* df = dynamic_cast<const DecimalFormat*>(&nf);
    if (df == nullptr) {
        // Handle other types of NumberFormat using the ICU 63 code, modified to
        // get the unitPattern from LongNameHandler and handle fallback to OTHER.
        UnicodeString formattedNumber;
        StandardPlural::Form pluralForm = QuantityFormatter::selectPlural(
                amtNumber, nf, **pluralRules, formattedNumber, pos, status);
        UnicodeString pattern = number::impl::LongNameHandler::getUnitPattern(getLocale(status),
                amtUnit, getUnitWidth(fWidth), pluralForm, status);
        // The above  handles fallback from other widths to short, and from other plural forms to OTHER
        if (U_FAILURE(status)) {
            return appendTo;
        }
        SimpleFormatter formatter(pattern, 0, 1, status);
        return QuantityFormatter::format(formatter, formattedNumber, appendTo, pos, status);
    }
    UFormattedNumberData result;
    if (const auto* lnf = df->toNumberFormatter(status)) {
        result.quantity.setToDouble(amtNumber.getDouble(status));
        lnf->unit(amtUnit)
            .unitWidth(getUnitWidth(fWidth))
            .formatImpl(&result, status);
    }
    DecimalFormat::fieldPositionHelper(result, pos, appendTo.length(), status);
    appendTo.append(result.toTempString(status));
    return appendTo;
}


// Formats numeric time duration as 5:00:47 or 3:54.
UnicodeString &MeasureFormat::formatNumeric(
        const Formattable *hms,  // always length 3
        int32_t bitMap,   // 1=hour set, 2=minute set, 4=second set
        UnicodeString &appendTo,
        UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return appendTo;
    }

    UnicodeString pattern;

    double hours = hms[0].getDouble(status);
    double minutes = hms[1].getDouble(status);
    double seconds = hms[2].getDouble(status);
    if (U_FAILURE(status)) {
        return appendTo;
    }

    // All possible combinations: "h", "m", "s", "hm", "hs", "ms", "hms"
    if (bitMap == 5 || bitMap == 7) { // "hms" & "hs" (we add minutes if "hs")
        pattern = cache->getNumericDateFormatters()->hourMinuteSecond;
        hours = uprv_trunc(hours);
        minutes = uprv_trunc(minutes);
    } else if (bitMap == 3) { // "hm"
        pattern = cache->getNumericDateFormatters()->hourMinute;
        hours = uprv_trunc(hours);
    } else if (bitMap == 6) { // "ms"
        pattern = cache->getNumericDateFormatters()->minuteSecond;
        minutes = uprv_trunc(minutes);
    } else { // h m s, handled outside formatNumeric. No value is also an error.
        status = U_INTERNAL_PROGRAM_ERROR;
        return appendTo;
    }

    const DecimalFormat *numberFormatter = dynamic_cast<const DecimalFormat*>(numberFormat->get());
    if (!numberFormatter) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return appendTo;
    }
    number::LocalizedNumberFormatter numberFormatter2;
    if (const auto* lnf = numberFormatter->toNumberFormatter(status)) {
        numberFormatter2 = lnf->integerWidth(number::IntegerWidth::zeroFillTo(2));
    } else {
        return appendTo;
    }

    FormattedStringBuilder fsb;

    UBool protect = false;
    const int32_t patternLength = pattern.length();
    for (int32_t i = 0; i < patternLength; i++) {
        char16_t c = pattern[i];

        // Also set the proper field in this switch
        // We don't use DateFormat.Field because this is not a date / time, is a duration.
        double value = 0;
        switch (c) {
            case u'H': value = hours; break;
            case u'm': value = minutes; break;
            case u's': value = seconds; break;
        }

        // There is not enough info to add Field(s) for the unit because all we have are plain
        // text patterns. For example in "21:51" there is no text for something like "hour",
        // while in something like "21h51" there is ("h"). But we can't really tell...
        switch (c) {
            case u'H':
            case u'm':
            case u's':
                if (protect) {
                    fsb.appendChar16(c, kUndefinedField, status);
                } else {
                    UnicodeString tmp;
                    if ((i + 1 < patternLength) && pattern[i + 1] == c) { // doubled
                        tmp = numberFormatter2.formatDouble(value, status).toString(status);
                        i++;
                    } else {
                        numberFormatter->format(value, tmp, status);
                    }
                    // TODO: Use proper Field
                    fsb.append(tmp, kUndefinedField, status);
                }
                break;
            case u'\'':
                // '' is escaped apostrophe
                if ((i + 1 < patternLength) && pattern[i + 1] == c) {
                    fsb.appendChar16(c, kUndefinedField, status);
                    i++;
                } else {
                    protect = !protect;
                }
                break;
            default:
                fsb.appendChar16(c, kUndefinedField, status);
        }
    }

    appendTo.append(fsb.toTempUnicodeString());

    return appendTo;
}

UnicodeString &MeasureFormat::formatMeasuresSlowTrack(
        const Measure *measures,
        int32_t measureCount,
        UnicodeString& appendTo,
        FieldPosition& pos,
        UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return appendTo;
    }
    FieldPosition dontCare(FieldPosition::DONT_CARE);
    FieldPosition fpos(pos.getField());
    LocalArray<UnicodeString> results(new UnicodeString[measureCount], status);
    int32_t fieldPositionFoundIndex = -1;
    for (int32_t i = 0; i < measureCount; ++i) {
        const NumberFormat *nf = cache->getIntegerFormat();
        if (i == measureCount - 1) {
            nf = numberFormat->get();
        }
        if (fieldPositionFoundIndex == -1) {
            formatMeasure(measures[i], *nf, results[i], fpos, status);
            if (U_FAILURE(status)) {
                return appendTo;
            }
            if (fpos.getBeginIndex() != 0 || fpos.getEndIndex() != 0) {
                fieldPositionFoundIndex = i;
            }
        } else {
            formatMeasure(measures[i], *nf, results[i], dontCare, status);
        }
    }
    int32_t offset;
    listFormatter->format(
            results.getAlias(),
            measureCount,
            appendTo,
            fieldPositionFoundIndex,
            offset,
            status);
    if (U_FAILURE(status)) {
        return appendTo;
    }
    // Fix up FieldPosition indexes if our field is found.
    if (fieldPositionFoundIndex != -1 && offset != -1) {
        pos.setBeginIndex(fpos.getBeginIndex() + offset);
        pos.setEndIndex(fpos.getEndIndex() + offset);
    }
    return appendTo;
}

MeasureFormat* U_EXPORT2 MeasureFormat::createCurrencyFormat(const Locale& locale,
                                                   UErrorCode& ec) {
    if (U_FAILURE(ec)) {
        return nullptr;
    }
    LocalPointer<CurrencyFormat> fmt(new CurrencyFormat(locale, ec), ec);
    return fmt.orphan();
}

MeasureFormat* U_EXPORT2 MeasureFormat::createCurrencyFormat(UErrorCode& ec) {
    if (U_FAILURE(ec)) {
        return nullptr;
    }
    return MeasureFormat::createCurrencyFormat(Locale::getDefault(), ec);
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
