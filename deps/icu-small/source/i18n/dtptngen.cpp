// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2007-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File DTPTNGEN.CPP
*
*******************************************************************************
*/

#include "unicode/utypes.h"
#if !UCONFIG_NO_FORMATTING

#include "unicode/datefmt.h"
#include "unicode/decimfmt.h"
#include "unicode/dtfmtsym.h"
#include "unicode/dtptngen.h"
#include "unicode/localpointer.h"
#include "unicode/simpleformatter.h"
#include "unicode/smpdtfmt.h"
#include "unicode/udat.h"
#include "unicode/udatpg.h"
#include "unicode/uniset.h"
#include "unicode/uloc.h"
#include "unicode/ures.h"
#include "unicode/ustring.h"
#include "unicode/rep.h"
#include "unicode/region.h"
#include "cpputils.h"
#include "mutex.h"
#include "umutex.h"
#include "cmemory.h"
#include "cstring.h"
#include "locbased.h"
#include "hash.h"
#include "uhash.h"
#include "ulocimp.h"
#include "uresimp.h"
#include "ulocimp.h"
#include "dtptngen_impl.h"
#include "ucln_in.h"
#include "charstr.h"
#include "uassert.h"

#if U_CHARSET_FAMILY==U_EBCDIC_FAMILY
/**
 * If we are on EBCDIC, use an iterator which will
 * traverse the bundles in ASCII order.
 */
#define U_USE_ASCII_BUNDLE_ITERATOR
#define U_SORT_ASCII_BUNDLE_ITERATOR
#endif

#if defined(U_USE_ASCII_BUNDLE_ITERATOR)

#include "unicode/ustring.h"
#include "uarrsort.h"

struct UResAEntry {
    char16_t *key;
    UResourceBundle *item;
};

struct UResourceBundleAIterator {
    UResourceBundle  *bund;
    UResAEntry *entries;
    int32_t num;
    int32_t cursor;
};

/* Must be C linkage to pass function pointer to the sort function */

U_CDECL_BEGIN

static int32_t U_CALLCONV
ures_a_codepointSort(const void *context, const void *left, const void *right) {
    //CompareContext *cmp=(CompareContext *)context;
    return u_strcmp(((const UResAEntry *)left)->key,
                    ((const UResAEntry *)right)->key);
}

U_CDECL_END

static void ures_a_open(UResourceBundleAIterator *aiter, UResourceBundle *bund, UErrorCode *status) {
    if(U_FAILURE(*status)) {
        return;
    }
    aiter->bund = bund;
    aiter->num = ures_getSize(aiter->bund);
    aiter->cursor = 0;
#if !defined(U_SORT_ASCII_BUNDLE_ITERATOR)
    aiter->entries = nullptr;
#else
    aiter->entries = (UResAEntry*)uprv_malloc(sizeof(UResAEntry)*aiter->num);
    for(int i=0;i<aiter->num;i++) {
        aiter->entries[i].item = ures_getByIndex(aiter->bund, i, nullptr, status);
        const char *akey = ures_getKey(aiter->entries[i].item);
        int32_t len = uprv_strlen(akey)+1;
        aiter->entries[i].key = (char16_t*)uprv_malloc(len*sizeof(char16_t));
        u_charsToUChars(akey, aiter->entries[i].key, len);
    }
    uprv_sortArray(aiter->entries, aiter->num, sizeof(UResAEntry), ures_a_codepointSort, nullptr, true, status);
#endif
}

static void ures_a_close(UResourceBundleAIterator *aiter) {
#if defined(U_SORT_ASCII_BUNDLE_ITERATOR)
    for(int i=0;i<aiter->num;i++) {
        uprv_free(aiter->entries[i].key);
        ures_close(aiter->entries[i].item);
    }
#endif
}

static const char16_t *ures_a_getNextString(UResourceBundleAIterator *aiter, int32_t *len, const char **key, UErrorCode *err) {
#if !defined(U_SORT_ASCII_BUNDLE_ITERATOR)
    return ures_getNextString(aiter->bund, len, key, err);
#else
    if(U_FAILURE(*err)) return nullptr;
    UResourceBundle *item = aiter->entries[aiter->cursor].item;
    const char16_t* ret = ures_getString(item, len, err);
    *key = ures_getKey(item);
    aiter->cursor++;
    return ret;
#endif
}


#endif


U_NAMESPACE_BEGIN

// *****************************************************************************
// class DateTimePatternGenerator
// *****************************************************************************
static const char16_t Canonical_Items[] = {
    // GyQMwWEDFdaHmsSv
    CAP_G, LOW_Y, CAP_Q, CAP_M, LOW_W, CAP_W, CAP_E,
    CAP_D, CAP_F, LOW_D, LOW_A, // The UDATPG_x_FIELD constants and these fields have a different order than in ICU4J
    CAP_H, LOW_M, LOW_S, CAP_S, LOW_V, 0
};

static const dtTypeElem dtTypes[] = {
    // patternChar, field, type, minLen, weight
    {CAP_G, UDATPG_ERA_FIELD, DT_SHORT, 1, 3,},
    {CAP_G, UDATPG_ERA_FIELD, DT_LONG,  4, 0},
    {CAP_G, UDATPG_ERA_FIELD, DT_NARROW, 5, 0},

    {LOW_Y, UDATPG_YEAR_FIELD, DT_NUMERIC, 1, 20},
    {CAP_Y, UDATPG_YEAR_FIELD, DT_NUMERIC + DT_DELTA, 1, 20},
    {LOW_U, UDATPG_YEAR_FIELD, DT_NUMERIC + 2*DT_DELTA, 1, 20},
    {LOW_R, UDATPG_YEAR_FIELD, DT_NUMERIC + 3*DT_DELTA, 1, 20},
    {CAP_U, UDATPG_YEAR_FIELD, DT_SHORT, 1, 3},
    {CAP_U, UDATPG_YEAR_FIELD, DT_LONG, 4, 0},
    {CAP_U, UDATPG_YEAR_FIELD, DT_NARROW, 5, 0},

    {CAP_Q, UDATPG_QUARTER_FIELD, DT_NUMERIC, 1, 2},
    {CAP_Q, UDATPG_QUARTER_FIELD, DT_SHORT, 3, 0},
    {CAP_Q, UDATPG_QUARTER_FIELD, DT_LONG, 4, 0},
    {CAP_Q, UDATPG_QUARTER_FIELD, DT_NARROW, 5, 0},
    {LOW_Q, UDATPG_QUARTER_FIELD, DT_NUMERIC + DT_DELTA, 1, 2},
    {LOW_Q, UDATPG_QUARTER_FIELD, DT_SHORT - DT_DELTA, 3, 0},
    {LOW_Q, UDATPG_QUARTER_FIELD, DT_LONG - DT_DELTA, 4, 0},
    {LOW_Q, UDATPG_QUARTER_FIELD, DT_NARROW - DT_DELTA, 5, 0},

    {CAP_M, UDATPG_MONTH_FIELD, DT_NUMERIC, 1, 2},
    {CAP_M, UDATPG_MONTH_FIELD, DT_SHORT, 3, 0},
    {CAP_M, UDATPG_MONTH_FIELD, DT_LONG, 4, 0},
    {CAP_M, UDATPG_MONTH_FIELD, DT_NARROW, 5, 0},
    {CAP_L, UDATPG_MONTH_FIELD, DT_NUMERIC + DT_DELTA, 1, 2},
    {CAP_L, UDATPG_MONTH_FIELD, DT_SHORT - DT_DELTA, 3, 0},
    {CAP_L, UDATPG_MONTH_FIELD, DT_LONG - DT_DELTA, 4, 0},
    {CAP_L, UDATPG_MONTH_FIELD, DT_NARROW - DT_DELTA, 5, 0},
    {LOW_L, UDATPG_MONTH_FIELD, DT_NUMERIC + DT_DELTA, 1, 1},

    {LOW_W, UDATPG_WEEK_OF_YEAR_FIELD, DT_NUMERIC, 1, 2},

    {CAP_W, UDATPG_WEEK_OF_MONTH_FIELD, DT_NUMERIC, 1, 0},

    {CAP_E, UDATPG_WEEKDAY_FIELD, DT_SHORT, 1, 3},
    {CAP_E, UDATPG_WEEKDAY_FIELD, DT_LONG, 4, 0},
    {CAP_E, UDATPG_WEEKDAY_FIELD, DT_NARROW, 5, 0},
    {CAP_E, UDATPG_WEEKDAY_FIELD, DT_SHORTER, 6, 0},
    {LOW_C, UDATPG_WEEKDAY_FIELD, DT_NUMERIC + 2*DT_DELTA, 1, 2},
    {LOW_C, UDATPG_WEEKDAY_FIELD, DT_SHORT - 2*DT_DELTA, 3, 0},
    {LOW_C, UDATPG_WEEKDAY_FIELD, DT_LONG - 2*DT_DELTA, 4, 0},
    {LOW_C, UDATPG_WEEKDAY_FIELD, DT_NARROW - 2*DT_DELTA, 5, 0},
    {LOW_C, UDATPG_WEEKDAY_FIELD, DT_SHORTER - 2*DT_DELTA, 6, 0},
    {LOW_E, UDATPG_WEEKDAY_FIELD, DT_NUMERIC + DT_DELTA, 1, 2}, // LOW_E is currently not used in CLDR data, should not be canonical
    {LOW_E, UDATPG_WEEKDAY_FIELD, DT_SHORT - DT_DELTA, 3, 0},
    {LOW_E, UDATPG_WEEKDAY_FIELD, DT_LONG - DT_DELTA, 4, 0},
    {LOW_E, UDATPG_WEEKDAY_FIELD, DT_NARROW - DT_DELTA, 5, 0},
    {LOW_E, UDATPG_WEEKDAY_FIELD, DT_SHORTER - DT_DELTA, 6, 0},

    {LOW_D, UDATPG_DAY_FIELD, DT_NUMERIC, 1, 2},
    {LOW_G, UDATPG_DAY_FIELD, DT_NUMERIC + DT_DELTA, 1, 20}, // really internal use, so we don't care

    {CAP_D, UDATPG_DAY_OF_YEAR_FIELD, DT_NUMERIC, 1, 3},

    {CAP_F, UDATPG_DAY_OF_WEEK_IN_MONTH_FIELD, DT_NUMERIC, 1, 0},

    {LOW_A, UDATPG_DAYPERIOD_FIELD, DT_SHORT, 1, 3},
    {LOW_A, UDATPG_DAYPERIOD_FIELD, DT_LONG, 4, 0},
    {LOW_A, UDATPG_DAYPERIOD_FIELD, DT_NARROW, 5, 0},
    {LOW_B, UDATPG_DAYPERIOD_FIELD, DT_SHORT - DT_DELTA, 1, 3},
    {LOW_B, UDATPG_DAYPERIOD_FIELD, DT_LONG - DT_DELTA, 4, 0},
    {LOW_B, UDATPG_DAYPERIOD_FIELD, DT_NARROW - DT_DELTA, 5, 0},
    // b needs to be closer to a than to B, so we make this 3*DT_DELTA
    {CAP_B, UDATPG_DAYPERIOD_FIELD, DT_SHORT - 3*DT_DELTA, 1, 3},
    {CAP_B, UDATPG_DAYPERIOD_FIELD, DT_LONG - 3*DT_DELTA, 4, 0},
    {CAP_B, UDATPG_DAYPERIOD_FIELD, DT_NARROW - 3*DT_DELTA, 5, 0},

    {CAP_H, UDATPG_HOUR_FIELD, DT_NUMERIC + 10*DT_DELTA, 1, 2}, // 24 hour
    {LOW_K, UDATPG_HOUR_FIELD, DT_NUMERIC + 11*DT_DELTA, 1, 2}, // 24 hour
    {LOW_H, UDATPG_HOUR_FIELD, DT_NUMERIC, 1, 2}, // 12 hour
    {CAP_K, UDATPG_HOUR_FIELD, DT_NUMERIC + DT_DELTA, 1, 2}, // 12 hour
    // The C code has had versions of the following 3, keep & update. Should not need these, but...
    // Without these, certain tests using e.g. staticGetSkeleton fail because j/J in patterns
    // get skipped instead of mapped to the right hour chars, for example in
    //   DateFormatTest::TestPatternFromSkeleton
    //   IntlTestDateTimePatternGeneratorAPI:: testStaticGetSkeleton
    //   DateIntervalFormatTest::testTicket11985
    // Need to investigate better handling of jJC replacement e.g. in staticGetSkeleton.
    {CAP_J, UDATPG_HOUR_FIELD, DT_NUMERIC + 5*DT_DELTA, 1, 2}, // 12/24 hour no AM/PM
    {LOW_J, UDATPG_HOUR_FIELD, DT_NUMERIC + 6*DT_DELTA, 1, 6}, // 12/24 hour
    {CAP_C, UDATPG_HOUR_FIELD, DT_NUMERIC + 7*DT_DELTA, 1, 6}, // 12/24 hour with preferred dayPeriods for 12

    {LOW_M, UDATPG_MINUTE_FIELD, DT_NUMERIC, 1, 2},

    {LOW_S, UDATPG_SECOND_FIELD, DT_NUMERIC, 1, 2},
    {CAP_A, UDATPG_SECOND_FIELD, DT_NUMERIC + DT_DELTA, 1, 1000},

    {CAP_S, UDATPG_FRACTIONAL_SECOND_FIELD, DT_NUMERIC, 1, 1000},

    {LOW_V, UDATPG_ZONE_FIELD, DT_SHORT - 2*DT_DELTA, 1, 0},
    {LOW_V, UDATPG_ZONE_FIELD, DT_LONG - 2*DT_DELTA, 4, 0},
    {LOW_Z, UDATPG_ZONE_FIELD, DT_SHORT, 1, 3},
    {LOW_Z, UDATPG_ZONE_FIELD, DT_LONG, 4, 0},
    {CAP_Z, UDATPG_ZONE_FIELD, DT_NARROW - DT_DELTA, 1, 3},
    {CAP_Z, UDATPG_ZONE_FIELD, DT_LONG - DT_DELTA, 4, 0},
    {CAP_Z, UDATPG_ZONE_FIELD, DT_SHORT - DT_DELTA, 5, 0},
    {CAP_O, UDATPG_ZONE_FIELD, DT_SHORT - DT_DELTA, 1, 0},
    {CAP_O, UDATPG_ZONE_FIELD, DT_LONG - DT_DELTA, 4, 0},
    {CAP_V, UDATPG_ZONE_FIELD, DT_SHORT - DT_DELTA, 1, 0},
    {CAP_V, UDATPG_ZONE_FIELD, DT_LONG - DT_DELTA, 2, 0},
    {CAP_V, UDATPG_ZONE_FIELD, DT_LONG-1 - DT_DELTA, 3, 0},
    {CAP_V, UDATPG_ZONE_FIELD, DT_LONG-2 - DT_DELTA, 4, 0},
    {CAP_X, UDATPG_ZONE_FIELD, DT_NARROW - DT_DELTA, 1, 0},
    {CAP_X, UDATPG_ZONE_FIELD, DT_SHORT - DT_DELTA, 2, 0},
    {CAP_X, UDATPG_ZONE_FIELD, DT_LONG - DT_DELTA, 4, 0},
    {LOW_X, UDATPG_ZONE_FIELD, DT_NARROW - DT_DELTA, 1, 0},
    {LOW_X, UDATPG_ZONE_FIELD, DT_SHORT - DT_DELTA, 2, 0},
    {LOW_X, UDATPG_ZONE_FIELD, DT_LONG - DT_DELTA, 4, 0},

    {0, UDATPG_FIELD_COUNT, 0, 0, 0} , // last row of dtTypes[]
 };

static const char* const CLDR_FIELD_APPEND[] = {
    "Era", "Year", "Quarter", "Month", "Week", "*", "Day-Of-Week",
    "*", "*", "Day", "*", // The UDATPG_x_FIELD constants and these fields have a different order than in ICU4J
    "Hour", "Minute", "Second", "*", "Timezone"
};

static const char* const CLDR_FIELD_NAME[UDATPG_FIELD_COUNT] = {
    "era", "year", "quarter", "month", "week", "weekOfMonth", "weekday",
    "dayOfYear", "weekdayOfMonth", "day", "dayperiod", // The UDATPG_x_FIELD constants and these fields have a different order than in ICU4J
    "hour", "minute", "second", "*", "zone"
};

static const char* const CLDR_FIELD_WIDTH[] = { // [UDATPG_WIDTH_COUNT]
    "", "-short", "-narrow"
};

static constexpr UDateTimePGDisplayWidth UDATPG_WIDTH_APPENDITEM = UDATPG_WIDE;
static constexpr int32_t UDATPG_FIELD_KEY_MAX = 24; // max length of CLDR field tag (type + width)

// For appendItems
static const char16_t UDATPG_ItemFormat[]= {0x7B, 0x30, 0x7D, 0x20, 0x251C, 0x7B, 0x32, 0x7D, 0x3A,
    0x20, 0x7B, 0x31, 0x7D, 0x2524, 0};  // {0} \u251C{2}: {1}\u2524

//static const char16_t repeatedPatterns[6]={CAP_G, CAP_E, LOW_Z, LOW_V, CAP_Q, 0}; // "GEzvQ"

static const char DT_DateTimePatternsTag[]="DateTimePatterns";
static const char DT_DateAtTimePatternsTag[]="DateTimePatterns%atTime";
static const char DT_DateTimeCalendarTag[]="calendar";
static const char DT_DateTimeGregorianTag[]="gregorian";
static const char DT_DateTimeAppendItemsTag[]="appendItems";
static const char DT_DateTimeFieldsTag[]="fields";
static const char DT_DateTimeAvailableFormatsTag[]="availableFormats";
//static const UnicodeString repeatedPattern=UnicodeString(repeatedPatterns);

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(DateTimePatternGenerator)
UOBJECT_DEFINE_RTTI_IMPLEMENTATION(DTSkeletonEnumeration)
UOBJECT_DEFINE_RTTI_IMPLEMENTATION(DTRedundantEnumeration)

DateTimePatternGenerator*  U_EXPORT2
DateTimePatternGenerator::createInstance(UErrorCode& status) {
    return createInstance(Locale::getDefault(), status);
}

DateTimePatternGenerator* U_EXPORT2
DateTimePatternGenerator::createInstance(const Locale& locale, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    LocalPointer<DateTimePatternGenerator> result(
            new DateTimePatternGenerator(locale, status), status);
    return U_SUCCESS(status) ? result.orphan() : nullptr;
}

DateTimePatternGenerator* U_EXPORT2
DateTimePatternGenerator::createInstanceNoStdPat(const Locale& locale, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    LocalPointer<DateTimePatternGenerator> result(
            new DateTimePatternGenerator(locale, status, true), status);
    return U_SUCCESS(status) ? result.orphan() : nullptr;
}

DateTimePatternGenerator*  U_EXPORT2
DateTimePatternGenerator::createEmptyInstance(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    LocalPointer<DateTimePatternGenerator> result(
            new DateTimePatternGenerator(status), status);
    return U_SUCCESS(status) ? result.orphan() : nullptr;
}

DateTimePatternGenerator::DateTimePatternGenerator(UErrorCode &status) :
    skipMatcher(nullptr),
    fAvailableFormatKeyHash(nullptr),
    fDefaultHourFormatChar(0),
    internalErrorCode(U_ZERO_ERROR)
{
    fp = new FormatParser();
    dtMatcher = new DateTimeMatcher();
    distanceInfo = new DistanceInfo();
    patternMap = new PatternMap();
    if (fp == nullptr || dtMatcher == nullptr || distanceInfo == nullptr || patternMap == nullptr) {
        internalErrorCode = status = U_MEMORY_ALLOCATION_ERROR;
    }
}

DateTimePatternGenerator::DateTimePatternGenerator(const Locale& locale, UErrorCode &status, UBool skipStdPatterns) :
    skipMatcher(nullptr),
    fAvailableFormatKeyHash(nullptr),
    fDefaultHourFormatChar(0),
    internalErrorCode(U_ZERO_ERROR)
{
    fp = new FormatParser();
    dtMatcher = new DateTimeMatcher();
    distanceInfo = new DistanceInfo();
    patternMap = new PatternMap();
    if (fp == nullptr || dtMatcher == nullptr || distanceInfo == nullptr || patternMap == nullptr) {
        internalErrorCode = status = U_MEMORY_ALLOCATION_ERROR;
    }
    else {
        initData(locale, status, skipStdPatterns);
    }
}

DateTimePatternGenerator::DateTimePatternGenerator(const DateTimePatternGenerator& other) :
    UObject(),
    skipMatcher(nullptr),
    fAvailableFormatKeyHash(nullptr),
    fDefaultHourFormatChar(0),
    internalErrorCode(U_ZERO_ERROR)
{
    fp = new FormatParser();
    dtMatcher = new DateTimeMatcher();
    distanceInfo = new DistanceInfo();
    patternMap = new PatternMap();
    if (fp == nullptr || dtMatcher == nullptr || distanceInfo == nullptr || patternMap == nullptr) {
        internalErrorCode = U_MEMORY_ALLOCATION_ERROR;
    }
    *this=other;
}

DateTimePatternGenerator&
DateTimePatternGenerator::operator=(const DateTimePatternGenerator& other) {
    // reflexive case
    if (&other == this) {
        return *this;
    }
    internalErrorCode = other.internalErrorCode;
    pLocale = other.pLocale;
    fDefaultHourFormatChar = other.fDefaultHourFormatChar;
    *fp = *(other.fp);
    dtMatcher->copyFrom(other.dtMatcher->skeleton);
    *distanceInfo = *(other.distanceInfo);
    for (int32_t style = UDAT_FULL; style <= UDAT_SHORT; style++) {
        dateTimeFormat[style] = other.dateTimeFormat[style];
    }
    decimal = other.decimal;
    for (int32_t style = UDAT_FULL; style <= UDAT_SHORT; style++) {
        dateTimeFormat[style].getTerminatedBuffer(); // NUL-terminate for the C API.
    }
    decimal.getTerminatedBuffer();
    delete skipMatcher;
    if ( other.skipMatcher == nullptr ) {
        skipMatcher = nullptr;
    }
    else {
        skipMatcher = new DateTimeMatcher(*other.skipMatcher);
        if (skipMatcher == nullptr)
        {
            internalErrorCode = U_MEMORY_ALLOCATION_ERROR;
            return *this;
        }
    }
    for (int32_t i=0; i< UDATPG_FIELD_COUNT; ++i ) {
        appendItemFormats[i] = other.appendItemFormats[i];
        appendItemFormats[i].getTerminatedBuffer(); // NUL-terminate for the C API.
        for (int32_t j=0; j< UDATPG_WIDTH_COUNT; ++j ) {
            fieldDisplayNames[i][j] = other.fieldDisplayNames[i][j];
            fieldDisplayNames[i][j].getTerminatedBuffer(); // NUL-terminate for the C API.
        }
    }
    patternMap->copyFrom(*other.patternMap, internalErrorCode);
    copyHashtable(other.fAvailableFormatKeyHash, internalErrorCode);
    return *this;
}


bool
DateTimePatternGenerator::operator==(const DateTimePatternGenerator& other) const {
    if (this == &other) {
        return true;
    }
    if ((pLocale==other.pLocale) && (patternMap->equals(*other.patternMap)) &&
        (decimal==other.decimal)) {
        for (int32_t style = UDAT_FULL; style <= UDAT_SHORT; style++) {
            if (dateTimeFormat[style] != other.dateTimeFormat[style]) {
                return false;
            }
        }
        for ( int32_t i=0 ; i<UDATPG_FIELD_COUNT; ++i ) {
            if (appendItemFormats[i] != other.appendItemFormats[i]) {
                return false;
            }
            for (int32_t j=0; j< UDATPG_WIDTH_COUNT; ++j ) {
                if (fieldDisplayNames[i][j] != other.fieldDisplayNames[i][j]) {
                    return false;
                }
            }
        }
        return true;
    }
    else {
        return false;
    }
}

bool
DateTimePatternGenerator::operator!=(const DateTimePatternGenerator& other) const {
    return  !operator==(other);
}

DateTimePatternGenerator::~DateTimePatternGenerator() {
    delete fAvailableFormatKeyHash;
    delete fp;
    delete dtMatcher;
    delete distanceInfo;
    delete patternMap;
    delete skipMatcher;
}

namespace {

UInitOnce initOnce {};
UHashtable *localeToAllowedHourFormatsMap = nullptr;

// Value deleter for hashmap.
U_CFUNC void U_CALLCONV deleteAllowedHourFormats(void *ptr) {
    uprv_free(ptr);
}

// Close hashmap at cleanup.
U_CFUNC UBool U_CALLCONV allowedHourFormatsCleanup() {
    uhash_close(localeToAllowedHourFormatsMap);
    return true;
}

enum AllowedHourFormat{
    ALLOWED_HOUR_FORMAT_UNKNOWN = -1,
    ALLOWED_HOUR_FORMAT_h,
    ALLOWED_HOUR_FORMAT_H,
    ALLOWED_HOUR_FORMAT_K,  // Added ICU-20383, used by JP
    ALLOWED_HOUR_FORMAT_k,  // Added ICU-20383, not currently used
    ALLOWED_HOUR_FORMAT_hb,
    ALLOWED_HOUR_FORMAT_hB,
    ALLOWED_HOUR_FORMAT_Kb, // Added ICU-20383, not currently used
    ALLOWED_HOUR_FORMAT_KB, // Added ICU-20383, not currently used
    // ICU-20383 The following are unlikely and not currently used
    ALLOWED_HOUR_FORMAT_Hb,
    ALLOWED_HOUR_FORMAT_HB
};

}  // namespace

void
DateTimePatternGenerator::initData(const Locale& locale, UErrorCode &status, UBool skipStdPatterns) {
    //const char *baseLangName = locale.getBaseName(); // unused
    if (U_FAILURE(status)) { return; }
    if (locale.isBogus()) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    skipMatcher = nullptr;
    fAvailableFormatKeyHash=nullptr;
    addCanonicalItems(status);
    if (!skipStdPatterns) { // skip to prevent circular dependency when called from SimpleDateFormat::construct
        addICUPatterns(locale, status);
    }
    addCLDRData(locale, status);
    setDateTimeFromCalendar(locale, status);
    setDecimalSymbols(locale, status);
    umtx_initOnce(initOnce, loadAllowedHourFormatsData, status);
    getAllowedHourFormats(locale, status);
    // If any of the above methods failed then the object is in an invalid state.
    internalErrorCode = status;
} // DateTimePatternGenerator::initData

namespace {

struct AllowedHourFormatsSink : public ResourceSink {
    // Initialize sub-sinks.
    AllowedHourFormatsSink() {}
    virtual ~AllowedHourFormatsSink();

    virtual void put(const char *key, ResourceValue &value, UBool /*noFallback*/,
                     UErrorCode &errorCode) override {
        ResourceTable timeData = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }
        for (int32_t i = 0; timeData.getKeyAndValue(i, key, value); ++i) {
            const char *regionOrLocale = key;
            ResourceTable formatList = value.getTable(errorCode);
            if (U_FAILURE(errorCode)) { return; }
            // below we construct a list[] that has an entry for the "preferred" value at [0],
            // followed by 1 or more entries for the "allowed" values, terminated with an
            // entry for ALLOWED_HOUR_FORMAT_UNKNOWN (not included in length below)
            LocalMemory<int32_t> list;
            int32_t length = 0;
            int32_t preferredFormat = ALLOWED_HOUR_FORMAT_UNKNOWN;
            for (int32_t j = 0; formatList.getKeyAndValue(j, key, value); ++j) {
                if (uprv_strcmp(key, "allowed") == 0) {
                    if (value.getType() == URES_STRING) {
                        length = 2; // 1 preferred to add later, 1 allowed to add now
                        if (list.allocateInsteadAndReset(length + 1) == nullptr) {
                            errorCode = U_MEMORY_ALLOCATION_ERROR;
                            return;
                        }
                        list[1] = getHourFormatFromUnicodeString(value.getUnicodeString(errorCode));
                    }
                    else {
                        ResourceArray allowedFormats = value.getArray(errorCode);
                        length = allowedFormats.getSize() + 1; // 1 preferred, getSize allowed
                        if (list.allocateInsteadAndReset(length + 1) == nullptr) {
                            errorCode = U_MEMORY_ALLOCATION_ERROR;
                            return;
                        }
                        for (int32_t k = 1; k < length; ++k) {
                            allowedFormats.getValue(k-1, value);
                            list[k] = getHourFormatFromUnicodeString(value.getUnicodeString(errorCode));
                        }
                    }
                } else if (uprv_strcmp(key, "preferred") == 0) {
                    preferredFormat = getHourFormatFromUnicodeString(value.getUnicodeString(errorCode));
                }
            }
            if (length > 1) {
                list[0] = (preferredFormat!=ALLOWED_HOUR_FORMAT_UNKNOWN)? preferredFormat: list[1];
            } else {
                // fallback handling for missing data
                length = 2; // 1 preferred, 1 allowed
                if (list.allocateInsteadAndReset(length + 1) == nullptr) {
                    errorCode = U_MEMORY_ALLOCATION_ERROR;
                    return;
                }
                list[0] = (preferredFormat!=ALLOWED_HOUR_FORMAT_UNKNOWN)? preferredFormat: ALLOWED_HOUR_FORMAT_H;
                list[1] = list[0];
            }
            list[length] = ALLOWED_HOUR_FORMAT_UNKNOWN;
            // At this point list[] will have at least two non-ALLOWED_HOUR_FORMAT_UNKNOWN entries,
            // followed by ALLOWED_HOUR_FORMAT_UNKNOWN.
            uhash_put(localeToAllowedHourFormatsMap, const_cast<char *>(regionOrLocale), list.orphan(), &errorCode);
            if (U_FAILURE(errorCode)) { return; }
        }
    }

    AllowedHourFormat getHourFormatFromUnicodeString(const UnicodeString &s) {
        if (s.length() == 1) {
            if (s[0] == LOW_H) { return ALLOWED_HOUR_FORMAT_h; }
            if (s[0] == CAP_H) { return ALLOWED_HOUR_FORMAT_H; }
            if (s[0] == CAP_K) { return ALLOWED_HOUR_FORMAT_K; }
            if (s[0] == LOW_K) { return ALLOWED_HOUR_FORMAT_k; }
        } else if (s.length() == 2) {
            if (s[0] == LOW_H && s[1] == LOW_B) { return ALLOWED_HOUR_FORMAT_hb; }
            if (s[0] == LOW_H && s[1] == CAP_B) { return ALLOWED_HOUR_FORMAT_hB; }
            if (s[0] == CAP_K && s[1] == LOW_B) { return ALLOWED_HOUR_FORMAT_Kb; }
            if (s[0] == CAP_K && s[1] == CAP_B) { return ALLOWED_HOUR_FORMAT_KB; }
            if (s[0] == CAP_H && s[1] == LOW_B) { return ALLOWED_HOUR_FORMAT_Hb; }
            if (s[0] == CAP_H && s[1] == CAP_B) { return ALLOWED_HOUR_FORMAT_HB; }
        }

        return ALLOWED_HOUR_FORMAT_UNKNOWN;
    }
};

}  // namespace

AllowedHourFormatsSink::~AllowedHourFormatsSink() {}

U_CFUNC void U_CALLCONV DateTimePatternGenerator::loadAllowedHourFormatsData(UErrorCode &status) {
    if (U_FAILURE(status)) { return; }
    localeToAllowedHourFormatsMap = uhash_open(
        uhash_hashChars, uhash_compareChars, nullptr, &status);
    if (U_FAILURE(status)) { return; }

    uhash_setValueDeleter(localeToAllowedHourFormatsMap, deleteAllowedHourFormats);
    ucln_i18n_registerCleanup(UCLN_I18N_ALLOWED_HOUR_FORMATS, allowedHourFormatsCleanup);

    LocalUResourceBundlePointer rb(ures_openDirect(nullptr, "supplementalData", &status));
    if (U_FAILURE(status)) { return; }

    AllowedHourFormatsSink sink;
    // TODO: Currently in the enumeration each table allocates a new array.
    // Try to reduce the number of memory allocations. Consider storing a
    // UVector32 with the concatenation of all of the sub-arrays, put the start index
    // into the hashmap, store 6 single-value sub-arrays right at the beginning of the
    // vector (at index enum*2) for easy data sharing, copy sub-arrays into runtime
    // object. Remember to clean up the vector, too.
    ures_getAllItemsWithFallback(rb.getAlias(), "timeData", sink, status);    
}

static int32_t* getAllowedHourFormatsLangCountry(const char* language, const char* country, UErrorCode& status) {
    CharString langCountry;
    langCountry.append(language, status);
    langCountry.append('_', status);
    langCountry.append(country, status);

    int32_t* allowedFormats;
    allowedFormats = (int32_t *)uhash_get(localeToAllowedHourFormatsMap, langCountry.data());
    if (allowedFormats == nullptr) {
        allowedFormats = (int32_t *)uhash_get(localeToAllowedHourFormatsMap, const_cast<char *>(country));
    }

    return allowedFormats;
}

void DateTimePatternGenerator::getAllowedHourFormats(const Locale &locale, UErrorCode &status) {
    if (U_FAILURE(status)) { return; }

    const char *language = locale.getLanguage();
    CharString baseCountry = ulocimp_getRegionForSupplementalData(locale.getName(), false, status);
    const char* country = baseCountry.data();

    Locale maxLocale;  // must be here for correct lifetime
    if (*language == '\0' || *country == '\0') {
        maxLocale = locale;
        UErrorCode localStatus = U_ZERO_ERROR;
        maxLocale.addLikelySubtags(localStatus);
        if (U_SUCCESS(localStatus)) {
            language = maxLocale.getLanguage();
            country = maxLocale.getCountry();
        }
    }
    if (*language == '\0') {
        // Unexpected, but fail gracefully
        language = "und";
    }
    if (*country == '\0') {
        country = "001";
    }

    int32_t* allowedFormats = getAllowedHourFormatsLangCountry(language, country, status);

    // We need to check if there is an hour cycle on locale
    char buffer[8];
    int32_t count = locale.getKeywordValue("hours", buffer, sizeof(buffer), status);

    fDefaultHourFormatChar = 0;
    if (U_SUCCESS(status) && count > 0) {
        if(uprv_strcmp(buffer, "h24") == 0) {
            fDefaultHourFormatChar = LOW_K;
        } else if(uprv_strcmp(buffer, "h23") == 0) {
            fDefaultHourFormatChar = CAP_H;
        } else if(uprv_strcmp(buffer, "h12") == 0) {
            fDefaultHourFormatChar = LOW_H;
        } else if(uprv_strcmp(buffer, "h11") == 0) {
            fDefaultHourFormatChar = CAP_K;
        }
    }

    // Check if the region has an alias
    if (allowedFormats == nullptr) {
        UErrorCode localStatus = U_ZERO_ERROR;
        const Region* region = Region::getInstance(country, localStatus);
        if (U_SUCCESS(localStatus)) {
            country = region->getRegionCode(); // the real region code
            allowedFormats = getAllowedHourFormatsLangCountry(language, country, status);
        }
    }

    if (allowedFormats != nullptr) {  // Lookup is successful
        // Here allowedFormats points to a list consisting of key for preferredFormat,
        // followed by one or more keys for allowedFormats, then followed by ALLOWED_HOUR_FORMAT_UNKNOWN.
        if (!fDefaultHourFormatChar) {
            switch (allowedFormats[0]) {
                case ALLOWED_HOUR_FORMAT_h: fDefaultHourFormatChar = LOW_H; break;
                case ALLOWED_HOUR_FORMAT_H: fDefaultHourFormatChar = CAP_H; break;
                case ALLOWED_HOUR_FORMAT_K: fDefaultHourFormatChar = CAP_K; break;
                case ALLOWED_HOUR_FORMAT_k: fDefaultHourFormatChar = LOW_K; break;
                default: fDefaultHourFormatChar = CAP_H; break;
            }
        }

        for (int32_t i = 0; i < UPRV_LENGTHOF(fAllowedHourFormats); ++i) {
            fAllowedHourFormats[i] = allowedFormats[i + 1];
            if (fAllowedHourFormats[i] == ALLOWED_HOUR_FORMAT_UNKNOWN) {
                break;
            }
        }
    } else {  // Lookup failed, twice
        if (!fDefaultHourFormatChar) {
            fDefaultHourFormatChar = CAP_H;
        }
        fAllowedHourFormats[0] = ALLOWED_HOUR_FORMAT_H;
        fAllowedHourFormats[1] = ALLOWED_HOUR_FORMAT_UNKNOWN;
    }
}

UDateFormatHourCycle
DateTimePatternGenerator::getDefaultHourCycle(UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return UDAT_HOUR_CYCLE_23;
    }
    if (fDefaultHourFormatChar == 0) {
        // We need to return something, but the caller should ignore it
        // anyways since the returned status is a failure.
        status = U_UNSUPPORTED_ERROR;
        return UDAT_HOUR_CYCLE_23;
    }
    switch (fDefaultHourFormatChar) {
        case CAP_K:
            return UDAT_HOUR_CYCLE_11;
        case LOW_H:
            return UDAT_HOUR_CYCLE_12;
        case CAP_H:
            return UDAT_HOUR_CYCLE_23;
        case LOW_K:
            return UDAT_HOUR_CYCLE_24;
        default:
            UPRV_UNREACHABLE_EXIT;
    }
}

UnicodeString
DateTimePatternGenerator::getSkeleton(const UnicodeString& pattern, UErrorCode&
/*status*/) {
    FormatParser fp2;
    DateTimeMatcher matcher;
    PtnSkeleton localSkeleton;
    matcher.set(pattern, &fp2, localSkeleton);
    return localSkeleton.getSkeleton();
}

UnicodeString
DateTimePatternGenerator::staticGetSkeleton(
        const UnicodeString& pattern, UErrorCode& /*status*/) {
    FormatParser fp;
    DateTimeMatcher matcher;
    PtnSkeleton localSkeleton;
    matcher.set(pattern, &fp, localSkeleton);
    return localSkeleton.getSkeleton();
}

UnicodeString
DateTimePatternGenerator::getBaseSkeleton(const UnicodeString& pattern, UErrorCode& /*status*/) {
    FormatParser fp2;
    DateTimeMatcher matcher;
    PtnSkeleton localSkeleton;
    matcher.set(pattern, &fp2, localSkeleton);
    return localSkeleton.getBaseSkeleton();
}

UnicodeString
DateTimePatternGenerator::staticGetBaseSkeleton(
        const UnicodeString& pattern, UErrorCode& /*status*/) {
    FormatParser fp;
    DateTimeMatcher matcher;
    PtnSkeleton localSkeleton;
    matcher.set(pattern, &fp, localSkeleton);
    return localSkeleton.getBaseSkeleton();
}

void
DateTimePatternGenerator::addICUPatterns(const Locale& locale, UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    UnicodeString dfPattern;
    UnicodeString conflictingString;
    DateFormat* df;

    // Load with ICU patterns
    for (int32_t i=DateFormat::kFull; i<=DateFormat::kShort; i++) {
        DateFormat::EStyle style = (DateFormat::EStyle)i;
        df = DateFormat::createDateInstance(style, locale);
        SimpleDateFormat* sdf;
        if (df != nullptr && (sdf = dynamic_cast<SimpleDateFormat*>(df)) != nullptr) {
            sdf->toPattern(dfPattern);
            addPattern(dfPattern, false, conflictingString, status);
        }
        // TODO Maybe we should return an error when the date format isn't simple.
        delete df;
        if (U_FAILURE(status)) { return; }

        df = DateFormat::createTimeInstance(style, locale);
        if (df != nullptr && (sdf = dynamic_cast<SimpleDateFormat*>(df)) != nullptr) {
            sdf->toPattern(dfPattern);
            addPattern(dfPattern, false, conflictingString, status);

            // TODO: C++ and Java are inconsistent (see #12568).
            // C++ uses MEDIUM, but Java uses SHORT.
            if ( i==DateFormat::kShort && !dfPattern.isEmpty() ) {
                consumeShortTimePattern(dfPattern, status);
            }
        }
        // TODO Maybe we should return an error when the date format isn't simple.
        delete df;
        if (U_FAILURE(status)) { return; }
    }
}

void
DateTimePatternGenerator::hackTimes(const UnicodeString& hackPattern, UErrorCode& status)  {
    UnicodeString conflictingString;

    fp->set(hackPattern);
    UnicodeString mmss;
    UBool gotMm=false;
    for (int32_t i=0; i<fp->itemNumber; ++i) {
        UnicodeString field = fp->items[i];
        if ( fp->isQuoteLiteral(field) ) {
            if ( gotMm ) {
               UnicodeString quoteLiteral;
               fp->getQuoteLiteral(quoteLiteral, &i);
               mmss += quoteLiteral;
            }
        }
        else {
            if (fp->isPatternSeparator(field) && gotMm) {
                mmss+=field;
            }
            else {
                char16_t ch=field.charAt(0);
                if (ch==LOW_M) {
                    gotMm=true;
                    mmss+=field;
                }
                else {
                    if (ch==LOW_S) {
                        if (!gotMm) {
                            break;
                        }
                        mmss+= field;
                        addPattern(mmss, false, conflictingString, status);
                        break;
                    }
                    else {
                        if (gotMm || ch==LOW_Z || ch==CAP_Z || ch==LOW_V || ch==CAP_V) {
                            break;
                        }
                    }
                }
            }
        }
    }
}

#define ULOC_LOCALE_IDENTIFIER_CAPACITY (ULOC_FULLNAME_CAPACITY + 1 + ULOC_KEYWORD_AND_VALUES_CAPACITY)

void
DateTimePatternGenerator::getCalendarTypeToUse(const Locale& locale, CharString& destination, UErrorCode& err) {
    destination.clear().append(DT_DateTimeGregorianTag, -1, err); // initial default
    if ( U_SUCCESS(err) ) {
        UErrorCode localStatus = U_ZERO_ERROR;
        char localeWithCalendarKey[ULOC_LOCALE_IDENTIFIER_CAPACITY];
        // obtain a locale that always has the calendar key value that should be used
        ures_getFunctionalEquivalent(
            localeWithCalendarKey,
            ULOC_LOCALE_IDENTIFIER_CAPACITY,
            nullptr,
            "calendar",
            "calendar",
            locale.getName(),
            nullptr,
            false,
            &localStatus);
        localeWithCalendarKey[ULOC_LOCALE_IDENTIFIER_CAPACITY-1] = 0; // ensure null termination
        // now get the calendar key value from that locale
        destination = ulocimp_getKeywordValue(localeWithCalendarKey, "calendar", localStatus);
        // If the input locale was invalid, don't fail with missing resource error, instead
        // continue with default of Gregorian.
        if (U_FAILURE(localStatus) && localStatus != U_MISSING_RESOURCE_ERROR) {
            err = localStatus;
        }
    }
}

void
DateTimePatternGenerator::consumeShortTimePattern(const UnicodeString& shortTimePattern,
        UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    // ICU-20383 No longer set fDefaultHourFormatChar to the hour format character from
    // this pattern; instead it is set from localeToAllowedHourFormatsMap which now
    // includes entries for both preferred and allowed formats.

    // HACK for hh:ss
    hackTimes(shortTimePattern, status);
}

struct DateTimePatternGenerator::AppendItemFormatsSink : public ResourceSink {

    // Destination for data, modified via setters.
    DateTimePatternGenerator& dtpg;

    AppendItemFormatsSink(DateTimePatternGenerator& _dtpg) : dtpg(_dtpg) {}
    virtual ~AppendItemFormatsSink();

    virtual void put(const char *key, ResourceValue &value, UBool /*noFallback*/,
            UErrorCode &errorCode) override {
        UDateTimePatternField field = dtpg.getAppendFormatNumber(key);
        if (field == UDATPG_FIELD_COUNT) { return; }
        const UnicodeString& valueStr = value.getUnicodeString(errorCode);
        if (dtpg.getAppendItemFormat(field).isEmpty() && !valueStr.isEmpty()) {
            dtpg.setAppendItemFormat(field, valueStr);
        }
    }

    void fillInMissing() {
        UnicodeString defaultItemFormat(true, UDATPG_ItemFormat, UPRV_LENGTHOF(UDATPG_ItemFormat)-1);  // Read-only alias.
        for (int32_t i = 0; i < UDATPG_FIELD_COUNT; i++) {
            UDateTimePatternField field = (UDateTimePatternField)i;
            if (dtpg.getAppendItemFormat(field).isEmpty()) {
                dtpg.setAppendItemFormat(field, defaultItemFormat);
            }
        }
    }
};

struct DateTimePatternGenerator::AppendItemNamesSink : public ResourceSink {

    // Destination for data, modified via setters.
    DateTimePatternGenerator& dtpg;

    AppendItemNamesSink(DateTimePatternGenerator& _dtpg) : dtpg(_dtpg) {}
    virtual ~AppendItemNamesSink();

    virtual void put(const char *key, ResourceValue &value, UBool /*noFallback*/,
            UErrorCode &errorCode) override {
        UDateTimePGDisplayWidth width;
        UDateTimePatternField field = dtpg.getFieldAndWidthIndices(key, &width);
        if (field == UDATPG_FIELD_COUNT) { return; }
        ResourceTable detailsTable = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }
        if (!detailsTable.findValue("dn", value)) { return; }
        const UnicodeString& valueStr = value.getUnicodeString(errorCode);
        if (U_SUCCESS(errorCode) && dtpg.getFieldDisplayName(field,width).isEmpty() && !valueStr.isEmpty()) {
            dtpg.setFieldDisplayName(field,width,valueStr);
        }
    }

    void fillInMissing() {
        for (int32_t i = 0; i < UDATPG_FIELD_COUNT; i++) {
            UnicodeString& valueStr = dtpg.getMutableFieldDisplayName((UDateTimePatternField)i, UDATPG_WIDE);
            if (valueStr.isEmpty()) {
                valueStr = CAP_F;
                U_ASSERT(i < 20);
                if (i < 10) {
                    // F0, F1, ..., F9
                    valueStr += (char16_t)(i+0x30);
                } else {
                    // F10, F11, ...
                    valueStr += (char16_t)0x31;
                    valueStr += (char16_t)(i-10 + 0x30);
                }
                // NUL-terminate for the C API.
                valueStr.getTerminatedBuffer();
            }
            for (int32_t j = 1; j < UDATPG_WIDTH_COUNT; j++) {
                UnicodeString& valueStr2 = dtpg.getMutableFieldDisplayName((UDateTimePatternField)i, (UDateTimePGDisplayWidth)j);
                if (valueStr2.isEmpty()) {
                    valueStr2 = dtpg.getFieldDisplayName((UDateTimePatternField)i, (UDateTimePGDisplayWidth)(j-1));
                }
            }
        }
    }
};

struct DateTimePatternGenerator::AvailableFormatsSink : public ResourceSink {

    // Destination for data, modified via setters.
    DateTimePatternGenerator& dtpg;

    // Temporary variable, required for calling addPatternWithSkeleton.
    UnicodeString conflictingPattern;

    AvailableFormatsSink(DateTimePatternGenerator& _dtpg) : dtpg(_dtpg) {}
    virtual ~AvailableFormatsSink();

    virtual void put(const char *key, ResourceValue &value, UBool /*isRoot*/,
            UErrorCode &errorCode) override {
        const UnicodeString formatKey(key, -1, US_INV);
        if (!dtpg.isAvailableFormatSet(formatKey) ) {
            dtpg.setAvailableFormat(formatKey, errorCode);
            // Add pattern with its associated skeleton. Override any duplicate
            // derived from std patterns, but not a previous availableFormats entry:
            const UnicodeString& formatValue = value.getUnicodeString(errorCode);
            conflictingPattern.remove();
            dtpg.addPatternWithSkeleton(formatValue, &formatKey, true, conflictingPattern, errorCode);
        }
    }
};

// Virtual destructors must be defined out of line.
DateTimePatternGenerator::AppendItemFormatsSink::~AppendItemFormatsSink() {}
DateTimePatternGenerator::AppendItemNamesSink::~AppendItemNamesSink() {}
DateTimePatternGenerator::AvailableFormatsSink::~AvailableFormatsSink() {}

void
DateTimePatternGenerator::addCLDRData(const Locale& locale, UErrorCode& errorCode) {
    if (U_FAILURE(errorCode)) { return; }
    UnicodeString rbPattern, value, field;
    CharString path;

    LocalUResourceBundlePointer rb(ures_open(nullptr, locale.getName(), &errorCode));
    if (U_FAILURE(errorCode)) { return; }

    CharString calendarTypeToUse; // to be filled in with the type to use, if all goes well
    getCalendarTypeToUse(locale, calendarTypeToUse, errorCode);
    if (U_FAILURE(errorCode)) { return; }

    // Local err to ignore resource not found exceptions
    UErrorCode err = U_ZERO_ERROR;

    // Load append item formats.
    AppendItemFormatsSink appendItemFormatsSink(*this);
    path.clear()
        .append(DT_DateTimeCalendarTag, errorCode)
        .append('/', errorCode)
        .append(calendarTypeToUse, errorCode)
        .append('/', errorCode)
        .append(DT_DateTimeAppendItemsTag, errorCode); // i.e., calendar/xxx/appendItems
    if (U_FAILURE(errorCode)) { return; }
    ures_getAllChildrenWithFallback(rb.getAlias(), path.data(), appendItemFormatsSink, err);
    appendItemFormatsSink.fillInMissing();

    // Load CLDR item names.
    err = U_ZERO_ERROR;
    AppendItemNamesSink appendItemNamesSink(*this);
    ures_getAllChildrenWithFallback(rb.getAlias(), DT_DateTimeFieldsTag, appendItemNamesSink, err);
    appendItemNamesSink.fillInMissing();

    // Load the available formats from CLDR.
    err = U_ZERO_ERROR;
    initHashtable(errorCode);
    if (U_FAILURE(errorCode)) { return; }
    AvailableFormatsSink availableFormatsSink(*this);
    path.clear()
        .append(DT_DateTimeCalendarTag, errorCode)
        .append('/', errorCode)
        .append(calendarTypeToUse, errorCode)
        .append('/', errorCode)
        .append(DT_DateTimeAvailableFormatsTag, errorCode); // i.e., calendar/xxx/availableFormats
    if (U_FAILURE(errorCode)) { return; }
    ures_getAllChildrenWithFallback(rb.getAlias(), path.data(), availableFormatsSink, err);
}

void
DateTimePatternGenerator::initHashtable(UErrorCode& err) {
    if (U_FAILURE(err)) { return; }
    if (fAvailableFormatKeyHash!=nullptr) {
        return;
    }
    LocalPointer<Hashtable> hash(new Hashtable(false, err), err);
    if (U_SUCCESS(err)) {
        fAvailableFormatKeyHash = hash.orphan();
    }
}

void
DateTimePatternGenerator::setAppendItemFormat(UDateTimePatternField field, const UnicodeString& value) {
    appendItemFormats[field] = value;
    // NUL-terminate for the C API.
    appendItemFormats[field].getTerminatedBuffer();
}

const UnicodeString&
DateTimePatternGenerator::getAppendItemFormat(UDateTimePatternField field) const {
    return appendItemFormats[field];
}

void
DateTimePatternGenerator::setAppendItemName(UDateTimePatternField field, const UnicodeString& value) {
    setFieldDisplayName(field, UDATPG_WIDTH_APPENDITEM, value);
}

const UnicodeString&
DateTimePatternGenerator::getAppendItemName(UDateTimePatternField field) const {
    return fieldDisplayNames[field][UDATPG_WIDTH_APPENDITEM];
}

void
DateTimePatternGenerator::setFieldDisplayName(UDateTimePatternField field, UDateTimePGDisplayWidth width, const UnicodeString& value) {
    fieldDisplayNames[field][width] = value;
    // NUL-terminate for the C API.
    fieldDisplayNames[field][width].getTerminatedBuffer();
}

UnicodeString
DateTimePatternGenerator::getFieldDisplayName(UDateTimePatternField field, UDateTimePGDisplayWidth width) const {
    return fieldDisplayNames[field][width];
}

UnicodeString&
DateTimePatternGenerator::getMutableFieldDisplayName(UDateTimePatternField field, UDateTimePGDisplayWidth width) {
    return fieldDisplayNames[field][width];
}

void
DateTimePatternGenerator::getAppendName(UDateTimePatternField field, UnicodeString& value) {
    value = SINGLE_QUOTE;
    value += fieldDisplayNames[field][UDATPG_WIDTH_APPENDITEM];
    value += SINGLE_QUOTE;
}

UnicodeString
DateTimePatternGenerator::getBestPattern(const UnicodeString& patternForm, UErrorCode& status) {
    return getBestPattern(patternForm, UDATPG_MATCH_NO_OPTIONS, status);
}

UnicodeString
DateTimePatternGenerator::getBestPattern(const UnicodeString& patternForm, UDateTimePatternMatchOptions options, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return {};
    }
    if (U_FAILURE(internalErrorCode)) {
        status = internalErrorCode;
        return {};
    }
    const UnicodeString *bestPattern = nullptr;
    UnicodeString dtFormat;
    UnicodeString resultPattern;
    int32_t flags = kDTPGNoFlags;

    int32_t dateMask=(1<<UDATPG_DAYPERIOD_FIELD) - 1;
    int32_t timeMask=(1<<UDATPG_FIELD_COUNT) - 1 - dateMask;

    // Replace hour metacharacters 'j', 'C' and 'J', set flags as necessary
    UnicodeString patternFormMapped = mapSkeletonMetacharacters(patternForm, &flags, status);
    if (U_FAILURE(status)) {
        return {};
    }

    resultPattern.remove();
    dtMatcher->set(patternFormMapped, fp);
    const PtnSkeleton* specifiedSkeleton = nullptr;
    bestPattern=getBestRaw(*dtMatcher, -1, distanceInfo, status, &specifiedSkeleton);
    if (U_FAILURE(status)) {
        return {};
    }

    if ( distanceInfo->missingFieldMask==0 && distanceInfo->extraFieldMask==0 ) {
        resultPattern = adjustFieldTypes(*bestPattern, specifiedSkeleton, flags, options);

        return resultPattern;
    }
    int32_t neededFields = dtMatcher->getFieldMask();
    UnicodeString datePattern=getBestAppending(neededFields & dateMask, flags, status, options);
    UnicodeString timePattern=getBestAppending(neededFields & timeMask, flags, status, options);
    if (U_FAILURE(status)) {
        return {};
    }
    if (datePattern.length()==0) {
        if (timePattern.length()==0) {
            resultPattern.remove();
        }
        else {
            return timePattern;
        }
    }
    if (timePattern.length()==0) {
        return datePattern;
    }
    resultPattern.remove();
    status = U_ZERO_ERROR;
    // determine which dateTimeFormat to use
    PtnSkeleton* reqSkeleton = dtMatcher->getSkeletonPtr();
    UDateFormatStyle style = UDAT_SHORT;
    int32_t monthFieldLen = reqSkeleton->baseOriginal.getFieldLength(UDATPG_MONTH_FIELD);
    if (monthFieldLen == 4) {
        if (reqSkeleton->baseOriginal.getFieldLength(UDATPG_WEEKDAY_FIELD) > 0) {
            style = UDAT_FULL;
        } else {
            style = UDAT_LONG;
        }
    } else if (monthFieldLen == 3) {
        style = UDAT_MEDIUM;
    }
    // and now use it to compose date and time
    dtFormat=getDateTimeFormat(style, status);
    SimpleFormatter(dtFormat, 2, 2, status).format(timePattern, datePattern, resultPattern, status);
    return resultPattern;
}

/*
 * Map a skeleton that may have metacharacters jJC to one without, by replacing
 * the metacharacters with locale-appropriate fields of h/H/k/K and of a/b/B
 * (depends on fDefaultHourFormatChar and fAllowedHourFormats being set, which in
 * turn depends on initData having been run). This method also updates the flags
 * as necessary. Returns the updated skeleton.
 */
UnicodeString
DateTimePatternGenerator::mapSkeletonMetacharacters(const UnicodeString& patternForm, int32_t* flags, UErrorCode& status) {
    UnicodeString patternFormMapped;
    patternFormMapped.remove();
    UBool inQuoted = false;
    int32_t patPos, patLen = patternForm.length();
    for (patPos = 0; patPos < patLen; patPos++) {
        char16_t patChr = patternForm.charAt(patPos);
        if (patChr == SINGLE_QUOTE) {
            inQuoted = !inQuoted;
        } else if (!inQuoted) {
            // Handle special mappings for 'j' and 'C' in which fields lengths
            // 1,3,5 => hour field length 1
            // 2,4,6 => hour field length 2
            // 1,2 => abbreviated dayPeriod (field length 1..3)
            // 3,4 => long dayPeriod (field length 4)
            // 5,6 => narrow dayPeriod (field length 5)
            if (patChr == LOW_J || patChr == CAP_C) {
                int32_t extraLen = 0; // 1 less than total field length
                while (patPos+1 < patLen && patternForm.charAt(patPos+1)==patChr) {
                    extraLen++;
                    patPos++;
                }
                int32_t hourLen = 1 + (extraLen & 1);
                int32_t dayPeriodLen = (extraLen < 2)? 1: 3 + (extraLen >> 1);
                char16_t hourChar = LOW_H;
                char16_t dayPeriodChar = LOW_A;
                if (patChr == LOW_J) {
                    hourChar = fDefaultHourFormatChar;
                } else {
                    AllowedHourFormat bestAllowed;
                    if (fAllowedHourFormats[0] != ALLOWED_HOUR_FORMAT_UNKNOWN) {
                        bestAllowed = (AllowedHourFormat)fAllowedHourFormats[0];
                    } else {
                        status = U_INVALID_FORMAT_ERROR;
                        return {};
                    }
                    if (bestAllowed == ALLOWED_HOUR_FORMAT_H || bestAllowed == ALLOWED_HOUR_FORMAT_HB || bestAllowed == ALLOWED_HOUR_FORMAT_Hb) {
                        hourChar = CAP_H;
                    } else if (bestAllowed == ALLOWED_HOUR_FORMAT_K || bestAllowed == ALLOWED_HOUR_FORMAT_KB || bestAllowed == ALLOWED_HOUR_FORMAT_Kb) {
                        hourChar = CAP_K;
                    } else if (bestAllowed == ALLOWED_HOUR_FORMAT_k) {
                        hourChar = LOW_K;
                    }
                    // in #13183 just add b/B to skeleton, no longer need to set special flags
                    if (bestAllowed == ALLOWED_HOUR_FORMAT_HB || bestAllowed == ALLOWED_HOUR_FORMAT_hB || bestAllowed == ALLOWED_HOUR_FORMAT_KB) {
                        dayPeriodChar = CAP_B;
                    } else if (bestAllowed == ALLOWED_HOUR_FORMAT_Hb || bestAllowed == ALLOWED_HOUR_FORMAT_hb || bestAllowed == ALLOWED_HOUR_FORMAT_Kb) {
                        dayPeriodChar = LOW_B;
                    }
                }
                if (hourChar==CAP_H || hourChar==LOW_K) {
                    dayPeriodLen = 0;
                }
                while (dayPeriodLen-- > 0) {
                    patternFormMapped.append(dayPeriodChar);
                }
                while (hourLen-- > 0) {
                    patternFormMapped.append(hourChar);
                }
            } else if (patChr == CAP_J) {
                // Get pattern for skeleton with H, then replace H or k
                // with fDefaultHourFormatChar (if different)
                patternFormMapped.append(CAP_H);
                *flags |= kDTPGSkeletonUsesCapJ;
            } else {
                patternFormMapped.append(patChr);
            }
        }
    }
    return patternFormMapped;
}

UnicodeString
DateTimePatternGenerator::replaceFieldTypes(const UnicodeString& pattern,
                                            const UnicodeString& skeleton,
                                            UErrorCode& status) {
    return replaceFieldTypes(pattern, skeleton, UDATPG_MATCH_NO_OPTIONS, status);
}

UnicodeString
DateTimePatternGenerator::replaceFieldTypes(const UnicodeString& pattern,
                                            const UnicodeString& skeleton,
                                            UDateTimePatternMatchOptions options,
                                            UErrorCode& status) {
    if (U_FAILURE(status)) {
        return {};
    }
    if (U_FAILURE(internalErrorCode)) {
        status = internalErrorCode;
        return {};
    }
    dtMatcher->set(skeleton, fp);
    UnicodeString result = adjustFieldTypes(pattern, nullptr, kDTPGNoFlags, options);
    return result;
}

void
DateTimePatternGenerator::setDecimal(const UnicodeString& newDecimal) {
    this->decimal = newDecimal;
    // NUL-terminate for the C API.
    this->decimal.getTerminatedBuffer();
}

const UnicodeString&
DateTimePatternGenerator::getDecimal() const {
    return decimal;
}

void
DateTimePatternGenerator::addCanonicalItems(UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    UnicodeString  conflictingPattern;

    for (int32_t i=0; i<UDATPG_FIELD_COUNT; i++) {
        if (Canonical_Items[i] > 0) {
            addPattern(UnicodeString(Canonical_Items[i]), false, conflictingPattern, status);
        }
        if (U_FAILURE(status)) { return; }
    }
}

void
DateTimePatternGenerator::setDateTimeFormat(const UnicodeString& dtFormat) {
    UErrorCode status = U_ZERO_ERROR;
    for (int32_t style = UDAT_FULL; style <= UDAT_SHORT; style++) {
        setDateTimeFormat((UDateFormatStyle)style, dtFormat, status);
    }
}

const UnicodeString&
DateTimePatternGenerator::getDateTimeFormat() const {
    UErrorCode status = U_ZERO_ERROR;
    return getDateTimeFormat(UDAT_MEDIUM, status);
}

void
DateTimePatternGenerator::setDateTimeFormat(UDateFormatStyle style, const UnicodeString& dtFormat, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (style < UDAT_FULL || style > UDAT_SHORT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    dateTimeFormat[style] = dtFormat;
    // Note for the following: getTerminatedBuffer() can re-allocate the UnicodeString
    // buffer so we do this here before clients request a const ref to the UnicodeString
    // or its buffer.
    dateTimeFormat[style].getTerminatedBuffer(); // NUL-terminate for the C API.
}

const UnicodeString&
DateTimePatternGenerator::getDateTimeFormat(UDateFormatStyle style, UErrorCode& status) const {
    static const UnicodeString emptyString = UNICODE_STRING_SIMPLE("");
    if (U_FAILURE(status)) {
        return emptyString;
    }
    if (style < UDAT_FULL || style > UDAT_SHORT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return emptyString;
    }
    return dateTimeFormat[style];
}

static const int32_t cTypeBufMax = 32;

void
DateTimePatternGenerator::setDateTimeFromCalendar(const Locale& locale, UErrorCode& status) {
    if (U_FAILURE(status)) { return; }

    const char16_t *resStr;
    int32_t resStrLen = 0;

    LocalUResourceBundlePointer calData(ures_open(nullptr, locale.getBaseName(), &status));
    if (U_FAILURE(status)) { return; }
    ures_getByKey(calData.getAlias(), DT_DateTimeCalendarTag, calData.getAlias(), &status);
    if (U_FAILURE(status)) { return; }

    char cType[cTypeBufMax + 1];
    Calendar::getCalendarTypeFromLocale(locale, cType, cTypeBufMax, status);
    cType[cTypeBufMax] = 0;
    if (U_FAILURE(status) || cType[0] == 0) {
        status = U_ZERO_ERROR;
        uprv_strcpy(cType, DT_DateTimeGregorianTag);
    }
    UBool cTypeIsGregorian = (uprv_strcmp(cType, DT_DateTimeGregorianTag) == 0);

    // Currently, for compatibility with pre-CLDR-42 data, we default to the "atTime"
    // combining patterns. Depending on guidance in CLDR 42 spec and on DisplayOptions,
    // we may change this.
    LocalUResourceBundlePointer specificCalBundle;
    LocalUResourceBundlePointer dateTimePatterns;
    int32_t dateTimeOffset = 0; // initially for DateTimePatterns%atTime
    if (!cTypeIsGregorian) {
        specificCalBundle.adoptInstead(ures_getByKeyWithFallback(calData.getAlias(), cType,
                                        nullptr, &status));
        dateTimePatterns.adoptInstead(ures_getByKeyWithFallback(specificCalBundle.getAlias(), DT_DateAtTimePatternsTag, // the %atTime variant, 4 entries
                                        nullptr, &status));
    }
    if (dateTimePatterns.isNull() || status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        specificCalBundle.adoptInstead(ures_getByKeyWithFallback(calData.getAlias(), DT_DateTimeGregorianTag,
                                        nullptr, &status));
        dateTimePatterns.adoptInstead(ures_getByKeyWithFallback(specificCalBundle.getAlias(), DT_DateAtTimePatternsTag, // the %atTime variant, 4 entries
                                        nullptr, &status));
    }
    if (U_SUCCESS(status) && (ures_getSize(dateTimePatterns.getAlias()) < 4)) {
        status = U_INVALID_FORMAT_ERROR;
    }
    if (status == U_MISSING_RESOURCE_ERROR) {
        // Try again with standard variant
        status = U_ZERO_ERROR;
        dateTimePatterns.orphan();
        dateTimeOffset = (int32_t)DateFormat::kDateTimeOffset;
        if (!cTypeIsGregorian) {
            specificCalBundle.adoptInstead(ures_getByKeyWithFallback(calData.getAlias(), cType,
                                            nullptr, &status));
            dateTimePatterns.adoptInstead(ures_getByKeyWithFallback(specificCalBundle.getAlias(), DT_DateTimePatternsTag, // the standard variant, 13 entries
                                            nullptr, &status));
        }
        if (dateTimePatterns.isNull() || status == U_MISSING_RESOURCE_ERROR) {
            status = U_ZERO_ERROR;
            specificCalBundle.adoptInstead(ures_getByKeyWithFallback(calData.getAlias(), DT_DateTimeGregorianTag,
                                            nullptr, &status));
            dateTimePatterns.adoptInstead(ures_getByKeyWithFallback(specificCalBundle.getAlias(), DT_DateTimePatternsTag, // the standard variant, 13 entries
                                            nullptr, &status));
        }
        if (U_SUCCESS(status) && (ures_getSize(dateTimePatterns.getAlias()) <= DateFormat::kDateTimeOffset + DateFormat::kShort)) {
            status = U_INVALID_FORMAT_ERROR;
        }
    }
    if (U_FAILURE(status)) { return; }
    for (int32_t style = UDAT_FULL; style <= UDAT_SHORT; style++) {
        resStr = ures_getStringByIndex(dateTimePatterns.getAlias(), dateTimeOffset + style, &resStrLen, &status);
        setDateTimeFormat((UDateFormatStyle)style, UnicodeString(true, resStr, resStrLen), status);
    }
}

void
DateTimePatternGenerator::setDecimalSymbols(const Locale& locale, UErrorCode& status) {
    DecimalFormatSymbols dfs = DecimalFormatSymbols(locale, status);
    if(U_SUCCESS(status)) {
        decimal = dfs.getSymbol(DecimalFormatSymbols::kDecimalSeparatorSymbol);
        // NUL-terminate for the C API.
        decimal.getTerminatedBuffer();
    }
}

UDateTimePatternConflict
DateTimePatternGenerator::addPattern(
    const UnicodeString& pattern,
    UBool override,
    UnicodeString &conflictingPattern,
    UErrorCode& status)
{
    if (U_FAILURE(internalErrorCode)) {
        status = internalErrorCode;
        return UDATPG_NO_CONFLICT;
    }

    return addPatternWithSkeleton(pattern, nullptr, override, conflictingPattern, status);
}

// For DateTimePatternGenerator::addPatternWithSkeleton -
// If skeletonToUse is specified, then an availableFormats entry is being added. In this case:
// 1. We pass that skeleton to matcher.set instead of having it derive a skeleton from the pattern.
// 2. If the new entry's skeleton or basePattern does match an existing entry but that entry also had a skeleton specified
// (i.e. it was also from availableFormats), then the new entry does not override it regardless of the value of the override
// parameter. This prevents later availableFormats entries from a parent locale overriding earlier ones from the actual
// specified locale. However, availableFormats entries *should* override entries with matching skeleton whose skeleton was
// derived (i.e. entries derived from the standard date/time patters for the specified locale).
// 3. When adding the pattern (patternMap->add), we set a new boolean to indicate that the added entry had a
// specified skeleton (which sets a new field in the PtnElem in the PatternMap).
UDateTimePatternConflict
DateTimePatternGenerator::addPatternWithSkeleton(
    const UnicodeString& pattern,
    const UnicodeString* skeletonToUse,
    UBool override,
    UnicodeString& conflictingPattern,
    UErrorCode& status)
{
    if (U_FAILURE(internalErrorCode)) {
        status = internalErrorCode;
        return UDATPG_NO_CONFLICT;
    }

    UnicodeString basePattern;
    PtnSkeleton   skeleton;
    UDateTimePatternConflict conflictingStatus = UDATPG_NO_CONFLICT;

    DateTimeMatcher matcher;
    if ( skeletonToUse == nullptr ) {
        matcher.set(pattern, fp, skeleton);
        matcher.getBasePattern(basePattern);
    } else {
        matcher.set(*skeletonToUse, fp, skeleton); // no longer trims skeleton fields to max len 3, per #7930
        matcher.getBasePattern(basePattern); // or perhaps instead: basePattern = *skeletonToUse;
    }
    // We only care about base conflicts - and replacing the pattern associated with a base - if:
    // 1. the conflicting previous base pattern did *not* have an explicit skeleton; in that case the previous
    // base + pattern combination was derived from either (a) a canonical item, (b) a standard format, or
    // (c) a pattern specified programmatically with a previous call to addPattern (which would only happen
    // if we are getting here from a subsequent call to addPattern).
    // 2. a skeleton is specified for the current pattern, but override=false; in that case we are checking
    // availableFormats items from root, which should not override any previous entry with the same base.
    UBool entryHadSpecifiedSkeleton;
    const UnicodeString *duplicatePattern = patternMap->getPatternFromBasePattern(basePattern, entryHadSpecifiedSkeleton);
    if (duplicatePattern != nullptr && (!entryHadSpecifiedSkeleton || (skeletonToUse != nullptr && !override))) {
        conflictingStatus = UDATPG_BASE_CONFLICT;
        conflictingPattern = *duplicatePattern;
        if (!override) {
            return conflictingStatus;
        }
    }
    // The only time we get here with override=true and skeletonToUse!=null is when adding availableFormats
    // items from CLDR data. In that case, we don't want an item from a parent locale to replace an item with
    // same skeleton from the specified locale, so skip the current item if skeletonWasSpecified is true for
    // the previously-specified conflicting item.
    const PtnSkeleton* entrySpecifiedSkeleton = nullptr;
    duplicatePattern = patternMap->getPatternFromSkeleton(skeleton, &entrySpecifiedSkeleton);
    if (duplicatePattern != nullptr ) {
        conflictingStatus = UDATPG_CONFLICT;
        conflictingPattern = *duplicatePattern;
        if (!override || (skeletonToUse != nullptr && entrySpecifiedSkeleton != nullptr)) {
            return conflictingStatus;
        }
    }
    patternMap->add(basePattern, skeleton, pattern, skeletonToUse != nullptr, status);
    if(U_FAILURE(status)) {
        return conflictingStatus;
    }

    return UDATPG_NO_CONFLICT;
}


UDateTimePatternField
DateTimePatternGenerator::getAppendFormatNumber(const char* field) const {
    for (int32_t i=0; i<UDATPG_FIELD_COUNT; ++i ) {
        if (uprv_strcmp(CLDR_FIELD_APPEND[i], field)==0) {
            return (UDateTimePatternField)i;
        }
    }
    return UDATPG_FIELD_COUNT;
}

UDateTimePatternField
DateTimePatternGenerator::getFieldAndWidthIndices(const char* key, UDateTimePGDisplayWidth* widthP) const {
    char cldrFieldKey[UDATPG_FIELD_KEY_MAX + 1];
    uprv_strncpy(cldrFieldKey, key, UDATPG_FIELD_KEY_MAX);
    cldrFieldKey[UDATPG_FIELD_KEY_MAX]=0; // ensure termination
    *widthP = UDATPG_WIDE;
    char* hyphenPtr = uprv_strchr(cldrFieldKey, '-');
    if (hyphenPtr) {
        for (int32_t i=UDATPG_WIDTH_COUNT-1; i>0; --i) {
            if (uprv_strcmp(CLDR_FIELD_WIDTH[i], hyphenPtr)==0) {
                *widthP=(UDateTimePGDisplayWidth)i;
                break;
            }
        }
        *hyphenPtr = 0; // now delete width portion of key
    }
    for (int32_t i=0; i<UDATPG_FIELD_COUNT; ++i ) {
        if (uprv_strcmp(CLDR_FIELD_NAME[i],cldrFieldKey)==0) {
            return (UDateTimePatternField)i;
        }
    }
    return UDATPG_FIELD_COUNT;
}

const UnicodeString*
DateTimePatternGenerator::getBestRaw(DateTimeMatcher& source,
                                     int32_t includeMask,
                                     DistanceInfo* missingFields,
                                     UErrorCode &status,
                                     const PtnSkeleton** specifiedSkeletonPtr) {
    int32_t bestDistance = 0x7fffffff;
    int32_t bestMissingFieldMask = -1;
    DistanceInfo tempInfo;
    const UnicodeString *bestPattern=nullptr;
    const PtnSkeleton* specifiedSkeleton=nullptr;

    PatternMapIterator it(status);
    if (U_FAILURE(status)) { return nullptr; }

    for (it.set(*patternMap); it.hasNext(); ) {
        DateTimeMatcher trial = it.next();
        if (trial.equals(skipMatcher)) {
            continue;
        }
        int32_t distance=source.getDistance(trial, includeMask, tempInfo);
        // Because we iterate over a map the order is undefined. Can change between implementations,
        // versions, and will very likely be different between Java and C/C++.
        // So if we have patterns with the same distance we also look at the missingFieldMask,
        // and we favour the smallest one. Because the field is a bitmask this technically means we
        // favour differences in the "least significant fields". For example we prefer the one with differences
        // in seconds field vs one with difference in the hours field.
        if (distance<bestDistance || (distance==bestDistance && bestMissingFieldMask<tempInfo.missingFieldMask)) {
            bestDistance=distance;
            bestMissingFieldMask=tempInfo.missingFieldMask;
            bestPattern=patternMap->getPatternFromSkeleton(*trial.getSkeletonPtr(), &specifiedSkeleton);
            missingFields->setTo(tempInfo);
            if (distance==0) {
                break;
            }
        }
    }

    // If the best raw match had a specified skeleton and that skeleton was requested by the caller,
    // then return it too. This generally happens when the caller needs to pass that skeleton
    // through to adjustFieldTypes so the latter can do a better job.
    if (bestPattern && specifiedSkeletonPtr) {
        *specifiedSkeletonPtr = specifiedSkeleton;
    }
    return bestPattern;
}

UnicodeString
DateTimePatternGenerator::adjustFieldTypes(const UnicodeString& pattern,
                                           const PtnSkeleton* specifiedSkeleton,
                                           int32_t flags,
                                           UDateTimePatternMatchOptions options) {
    UnicodeString newPattern;
    fp->set(pattern);
    for (int32_t i=0; i < fp->itemNumber; i++) {
        UnicodeString field = fp->items[i];
        if ( fp->isQuoteLiteral(field) ) {

            UnicodeString quoteLiteral;
            fp->getQuoteLiteral(quoteLiteral, &i);
            newPattern += quoteLiteral;
        }
        else {
            if (fp->isPatternSeparator(field)) {
                newPattern+=field;
                continue;
            }
            int32_t canonicalIndex = fp->getCanonicalIndex(field);
            if (canonicalIndex < 0) {
                newPattern+=field;
                continue;  // don't adjust
            }
            const dtTypeElem *row = &dtTypes[canonicalIndex];
            int32_t typeValue = row->field;

            // handle day periods - with #13183, no longer need special handling here, integrated with normal types

            if ((flags & kDTPGFixFractionalSeconds) != 0 && typeValue == UDATPG_SECOND_FIELD) {
                field += decimal;
                dtMatcher->skeleton.original.appendFieldTo(UDATPG_FRACTIONAL_SECOND_FIELD, field);
            } else if (dtMatcher->skeleton.type[typeValue]!=0) {
                    // Here:
                    // - "reqField" is the field from the originally requested skeleton after replacement
                    // of metacharacters 'j', 'C' and 'J', with length "reqFieldLen".
                    // - "field" is the field from the found pattern.
                    //
                    // The adjusted field should consist of characters from the originally requested
                    // skeleton, except in the case of UDATPG_MONTH_FIELD or
                    // UDATPG_WEEKDAY_FIELD or UDATPG_YEAR_FIELD, in which case it should consist
                    // of characters from the found pattern. In some cases of UDATPG_HOUR_FIELD,
                    // there is adjustment following the "defaultHourFormatChar". There is explanation
                    // how it is done below.
                    //
                    // The length of the adjusted field (adjFieldLen) should match that in the originally
                    // requested skeleton, except that in the following cases the length of the adjusted field
                    // should match that in the found pattern (i.e. the length of this pattern field should
                    // not be adjusted):
                    // 1. typeValue is UDATPG_HOUR_FIELD/MINUTE/SECOND and the corresponding bit in options is
                    //    not set (ticket #7180). Note, we may want to implement a similar change for other
                    //    numeric fields (MM, dd, etc.) so the default behavior is to get locale preference for
                    //    field length, but options bits can be used to override this.
                    // 2. There is a specified skeleton for the found pattern and one of the following is true:
                    //    a) The length of the field in the skeleton (skelFieldLen) is equal to reqFieldLen.
                    //    b) The pattern field is numeric and the skeleton field is not, or vice versa.

                    char16_t reqFieldChar = dtMatcher->skeleton.original.getFieldChar(typeValue);
                    int32_t reqFieldLen = dtMatcher->skeleton.original.getFieldLength(typeValue);
                    if (reqFieldChar == CAP_E && reqFieldLen < 3)
                        reqFieldLen = 3; // 1-3 for E are equivalent to 3 for c,e
                    int32_t adjFieldLen = reqFieldLen;
                    if ( (typeValue==UDATPG_HOUR_FIELD && (options & UDATPG_MATCH_HOUR_FIELD_LENGTH)==0) ||
                         (typeValue==UDATPG_MINUTE_FIELD && (options & UDATPG_MATCH_MINUTE_FIELD_LENGTH)==0) ||
                         (typeValue==UDATPG_SECOND_FIELD && (options & UDATPG_MATCH_SECOND_FIELD_LENGTH)==0) ) {
                         adjFieldLen = field.length();
                    } else if (specifiedSkeleton && reqFieldChar != LOW_C && reqFieldChar != LOW_E) {
                        // (we skip this section for 'c' and 'e' because unlike the other characters considered in this function,
                        // they have no minimum field length-- 'E' and 'EE' are equivalent to 'EEE', but 'e' and 'ee' are not
                        // equivalent to 'eee' -- see the entries for "week day" in
                        // https://www.unicode.org/reports/tr35/tr35-dates.html#Date_Field_Symbol_Table for more info)
                        int32_t skelFieldLen = specifiedSkeleton->original.getFieldLength(typeValue);
                        UBool patFieldIsNumeric = (row->type > 0);
                        UBool skelFieldIsNumeric = (specifiedSkeleton->type[typeValue] > 0);
                        if (skelFieldLen == reqFieldLen || (patFieldIsNumeric && !skelFieldIsNumeric) || (skelFieldIsNumeric && !patFieldIsNumeric)) {
                            // don't adjust the field length in the found pattern
                            adjFieldLen = field.length();
                        }
                    }
                    char16_t c = (typeValue!= UDATPG_HOUR_FIELD
                            && typeValue!= UDATPG_MONTH_FIELD
                            && typeValue!= UDATPG_WEEKDAY_FIELD
                            && (typeValue!= UDATPG_YEAR_FIELD || reqFieldChar==CAP_Y))
                            ? reqFieldChar
                            : field.charAt(0);
                    if (c == CAP_E && adjFieldLen < 3) {
                        c = LOW_E;
                    }
                    if (typeValue == UDATPG_HOUR_FIELD && fDefaultHourFormatChar != 0) {
                        // The adjustment here is required to match spec (https://www.unicode.org/reports/tr35/tr35-dates.html#dfst-hour).
                        // It is necessary to match the hour-cycle preferred by the Locale.
                        // Given that, we need to do the following adjustments:
                        // 1. When hour-cycle is h11 it should replace 'h' by 'K'.
                        // 2. When hour-cycle is h23 it should replace 'H' by 'k'.
                        // 3. When hour-cycle is h24 it should replace 'k' by 'H'.
                        // 4. When hour-cycle is h12 it should replace 'K' by 'h'.

                        if ((flags & kDTPGSkeletonUsesCapJ) != 0 || reqFieldChar == fDefaultHourFormatChar) {
                            c = fDefaultHourFormatChar;
                        } else if (reqFieldChar == LOW_H && fDefaultHourFormatChar == CAP_K) {
                            c = CAP_K;
                        } else if (reqFieldChar == CAP_H && fDefaultHourFormatChar == LOW_K) {
                            c = LOW_K;
                        } else if (reqFieldChar == LOW_K && fDefaultHourFormatChar == CAP_H) {
                            c = CAP_H;
                        } else if (reqFieldChar == CAP_K && fDefaultHourFormatChar == LOW_H) {
                            c = LOW_H;
                        }
                    }

                    field.remove();
                    for (int32_t j=adjFieldLen; j>0; --j) {
                        field += c;
                    }
            }
            newPattern+=field;
        }
    }
    return newPattern;
}

UnicodeString
DateTimePatternGenerator::getBestAppending(int32_t missingFields, int32_t flags, UErrorCode &status, UDateTimePatternMatchOptions options) {
    if (U_FAILURE(status)) {
        return {};
    }
    UnicodeString  resultPattern, tempPattern;
    const UnicodeString* tempPatternPtr;
    int32_t lastMissingFieldMask=0;
    if (missingFields!=0) {
        resultPattern=UnicodeString();
        const PtnSkeleton* specifiedSkeleton=nullptr;
        tempPatternPtr = getBestRaw(*dtMatcher, missingFields, distanceInfo, status, &specifiedSkeleton);
        if (U_FAILURE(status)) {
            return {};
        }
        tempPattern = *tempPatternPtr;
        resultPattern = adjustFieldTypes(tempPattern, specifiedSkeleton, flags, options);
        if ( distanceInfo->missingFieldMask==0 ) {
            return resultPattern;
        }
        while (distanceInfo->missingFieldMask!=0) { // precondition: EVERY single field must work!
            if ( lastMissingFieldMask == distanceInfo->missingFieldMask ) {
                break;  // cannot find the proper missing field
            }
            if (((distanceInfo->missingFieldMask & UDATPG_SECOND_AND_FRACTIONAL_MASK)==UDATPG_FRACTIONAL_MASK) &&
                ((missingFields & UDATPG_SECOND_AND_FRACTIONAL_MASK) == UDATPG_SECOND_AND_FRACTIONAL_MASK)) {
                resultPattern = adjustFieldTypes(resultPattern, specifiedSkeleton, flags | kDTPGFixFractionalSeconds, options);
                distanceInfo->missingFieldMask &= ~UDATPG_FRACTIONAL_MASK;
                continue;
            }
            int32_t startingMask = distanceInfo->missingFieldMask;
            tempPatternPtr = getBestRaw(*dtMatcher, distanceInfo->missingFieldMask, distanceInfo, status, &specifiedSkeleton);
            if (U_FAILURE(status)) {
                return {};
            }
            tempPattern = *tempPatternPtr;
            tempPattern = adjustFieldTypes(tempPattern, specifiedSkeleton, flags, options);
            int32_t foundMask=startingMask& ~distanceInfo->missingFieldMask;
            int32_t topField=getTopBitNumber(foundMask);

            if (appendItemFormats[topField].length() != 0) {
                UnicodeString appendName;
                getAppendName((UDateTimePatternField)topField, appendName);
                const UnicodeString *values[3] = {
                    &resultPattern,
                    &tempPattern,
                    &appendName
                };
                SimpleFormatter(appendItemFormats[topField], 2, 3, status).
                    formatAndReplace(values, 3, resultPattern, nullptr, 0, status);
            }
            lastMissingFieldMask = distanceInfo->missingFieldMask;
        }
    }
    return resultPattern;
}

int32_t
DateTimePatternGenerator::getTopBitNumber(int32_t foundMask) const {
    if ( foundMask==0 ) {
        return 0;
    }
    int32_t i=0;
    while (foundMask!=0) {
        foundMask >>=1;
        ++i;
    }
    if (i-1 >UDATPG_ZONE_FIELD) {
        return UDATPG_ZONE_FIELD;
    }
    else
        return i-1;
}

void
DateTimePatternGenerator::setAvailableFormat(const UnicodeString &key, UErrorCode& err)
{
    fAvailableFormatKeyHash->puti(key, 1, err);
}

UBool
DateTimePatternGenerator::isAvailableFormatSet(const UnicodeString &key) const {
    return (UBool)(fAvailableFormatKeyHash->geti(key) == 1);
}

void
DateTimePatternGenerator::copyHashtable(Hashtable *other, UErrorCode &status) {
    if (other == nullptr || U_FAILURE(status)) {
        return;
    }
    if (fAvailableFormatKeyHash != nullptr) {
        delete fAvailableFormatKeyHash;
        fAvailableFormatKeyHash = nullptr;
    }
    initHashtable(status);
    if(U_FAILURE(status)){
        return;
    }
    int32_t pos = UHASH_FIRST;
    const UHashElement* elem = nullptr;
    // walk through the hash table and create a deep clone
    while((elem = other->nextElement(pos))!= nullptr){
        const UHashTok otherKeyTok = elem->key;
        UnicodeString* otherKey = (UnicodeString*)otherKeyTok.pointer;
        fAvailableFormatKeyHash->puti(*otherKey, 1, status);
        if(U_FAILURE(status)){
            return;
        }
    }
}

StringEnumeration*
DateTimePatternGenerator::getSkeletons(UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (U_FAILURE(internalErrorCode)) {
        status = internalErrorCode;
        return nullptr;
    }
    LocalPointer<StringEnumeration> skeletonEnumerator(
        new DTSkeletonEnumeration(*patternMap, DT_SKELETON, status), status);

    return U_SUCCESS(status) ? skeletonEnumerator.orphan() : nullptr;
}

const UnicodeString&
DateTimePatternGenerator::getPatternForSkeleton(const UnicodeString& skeleton) const {
    PtnElem *curElem;

    if (skeleton.length() ==0) {
        return emptyString;
    }
    curElem = patternMap->getHeader(skeleton.charAt(0));
    while ( curElem != nullptr ) {
        if ( curElem->skeleton->getSkeleton()==skeleton ) {
            return curElem->pattern;
        }
        curElem = curElem->next.getAlias();
    }
    return emptyString;
}

StringEnumeration*
DateTimePatternGenerator::getBaseSkeletons(UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (U_FAILURE(internalErrorCode)) {
        status = internalErrorCode;
        return nullptr;
    }
    LocalPointer<StringEnumeration> baseSkeletonEnumerator(
        new DTSkeletonEnumeration(*patternMap, DT_BASESKELETON, status), status);

    return U_SUCCESS(status) ? baseSkeletonEnumerator.orphan() : nullptr;
}

StringEnumeration*
DateTimePatternGenerator::getRedundants(UErrorCode& status) {
    if (U_FAILURE(status)) { return nullptr; }
    if (U_FAILURE(internalErrorCode)) {
        status = internalErrorCode;
        return nullptr;
    }
    LocalPointer<StringEnumeration> output(new DTRedundantEnumeration(), status);
    if (U_FAILURE(status)) { return nullptr; }
    const UnicodeString *pattern;
    PatternMapIterator it(status);
    if (U_FAILURE(status)) { return nullptr; }

    for (it.set(*patternMap); it.hasNext(); ) {
        DateTimeMatcher current = it.next();
        pattern = patternMap->getPatternFromSkeleton(*(it.getSkeleton()));
        if ( isCanonicalItem(*pattern) ) {
            continue;
        }
        if ( skipMatcher == nullptr ) {
            skipMatcher = new DateTimeMatcher(current);
            if (skipMatcher == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return nullptr;
            }
        }
        else {
            *skipMatcher = current;
        }
        UnicodeString trial = getBestPattern(current.getPattern(), status);
        if (U_FAILURE(status)) { return nullptr; }
        if (trial == *pattern) {
            ((DTRedundantEnumeration *)output.getAlias())->add(*pattern, status);
            if (U_FAILURE(status)) { return nullptr; }
        }
        if (current.equals(skipMatcher)) {
            continue;
        }
    }
    return output.orphan();
}

UBool
DateTimePatternGenerator::isCanonicalItem(const UnicodeString& item) const {
    if ( item.length() != 1 ) {
        return false;
    }
    for (int32_t i=0; i<UDATPG_FIELD_COUNT; ++i) {
        if (item.charAt(0)==Canonical_Items[i]) {
            return true;
        }
    }
    return false;
}


DateTimePatternGenerator*
DateTimePatternGenerator::clone() const {
    return new DateTimePatternGenerator(*this);
}

PatternMap::PatternMap() {
   for (int32_t i=0; i < MAX_PATTERN_ENTRIES; ++i ) {
       boot[i] = nullptr;
   }
   isDupAllowed = true;
}

void
PatternMap::copyFrom(const PatternMap& other, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    this->isDupAllowed = other.isDupAllowed;
    for (int32_t bootIndex = 0; bootIndex < MAX_PATTERN_ENTRIES; ++bootIndex) {
        PtnElem *curElem, *otherElem, *prevElem=nullptr;
        otherElem = other.boot[bootIndex];
        while (otherElem != nullptr) {
            LocalPointer<PtnElem> newElem(new PtnElem(otherElem->basePattern, otherElem->pattern), status);
            if (U_FAILURE(status)) {
                return; // out of memory
            }
            newElem->skeleton.adoptInsteadAndCheckErrorCode(new PtnSkeleton(*(otherElem->skeleton)), status);
            if (U_FAILURE(status)) {
                return; // out of memory
            }
            newElem->skeletonWasSpecified = otherElem->skeletonWasSpecified;

            // Release ownership from the LocalPointer of the PtnElem object.
            // The PtnElem will now be owned by either the boot (for the first entry in the linked-list)
            // or owned by the previous PtnElem object in the linked-list.
            curElem = newElem.orphan();

            if (this->boot[bootIndex] == nullptr) {
                this->boot[bootIndex] = curElem;
            } else {
                if (prevElem != nullptr) {
                    prevElem->next.adoptInstead(curElem);
                } else {
                    UPRV_UNREACHABLE_EXIT;
                }
            }
            prevElem = curElem;
            otherElem = otherElem->next.getAlias();
        }

    }
}

PtnElem*
PatternMap::getHeader(char16_t baseChar) const {
    PtnElem* curElem;

    if ( (baseChar >= CAP_A) && (baseChar <= CAP_Z) ) {
         curElem = boot[baseChar-CAP_A];
    }
    else {
        if ( (baseChar >=LOW_A) && (baseChar <= LOW_Z) ) {
            curElem = boot[26+baseChar-LOW_A];
        }
        else {
            return nullptr;
        }
    }
    return curElem;
}

PatternMap::~PatternMap() {
   for (int32_t i=0; i < MAX_PATTERN_ENTRIES; ++i ) {
       if (boot[i] != nullptr ) {
           delete boot[i];
           boot[i] = nullptr;
       }
   }
}  // PatternMap destructor

void
PatternMap::add(const UnicodeString& basePattern,
                const PtnSkeleton& skeleton,
                const UnicodeString& value,// mapped pattern value
                UBool skeletonWasSpecified,
                UErrorCode &status) {
    char16_t baseChar = basePattern.charAt(0);
    PtnElem *curElem, *baseElem;
    status = U_ZERO_ERROR;

    // the baseChar must be A-Z or a-z
    if ((baseChar >= CAP_A) && (baseChar <= CAP_Z)) {
        baseElem = boot[baseChar-CAP_A];
    }
    else {
        if ((baseChar >=LOW_A) && (baseChar <= LOW_Z)) {
            baseElem = boot[26+baseChar-LOW_A];
         }
         else {
             status = U_ILLEGAL_CHARACTER;
             return;
         }
    }

    if (baseElem == nullptr) {
        LocalPointer<PtnElem> newElem(new PtnElem(basePattern, value), status);
        if (U_FAILURE(status)) {
            return; // out of memory
        }
        newElem->skeleton.adoptInsteadAndCheckErrorCode(new PtnSkeleton(skeleton), status);
        if (U_FAILURE(status)) {
            return; // out of memory
        }
        newElem->skeletonWasSpecified = skeletonWasSpecified;
        if (baseChar >= LOW_A) {
            boot[26 + (baseChar - LOW_A)] = newElem.orphan(); // the boot array now owns the PtnElem.
        }
        else {
            boot[baseChar - CAP_A] = newElem.orphan(); // the boot array now owns the PtnElem.
        }
    }
    if ( baseElem != nullptr ) {
        curElem = getDuplicateElem(basePattern, skeleton, baseElem);

        if (curElem == nullptr) {
            // add new element to the list.
            curElem = baseElem;
            while( curElem -> next != nullptr )
            {
                curElem = curElem->next.getAlias();
            }

            LocalPointer<PtnElem> newElem(new PtnElem(basePattern, value), status);
            if (U_FAILURE(status)) {
                return; // out of memory
            }
            newElem->skeleton.adoptInsteadAndCheckErrorCode(new PtnSkeleton(skeleton), status);
            if (U_FAILURE(status)) {
                return; // out of memory
            }
            newElem->skeletonWasSpecified = skeletonWasSpecified;
            curElem->next.adoptInstead(newElem.orphan());
            curElem = curElem->next.getAlias();
        }
        else {
            // Pattern exists in the list already.
            if ( !isDupAllowed ) {
                return;
            }
            // Overwrite the value.
            curElem->pattern = value;
            // It was a bug that we were not doing the following previously,
            // though that bug hid other problems by making things partly work.
            curElem->skeletonWasSpecified = skeletonWasSpecified;
        }
    }
}  // PatternMap::add

// Find the pattern from the given basePattern string.
const UnicodeString *
PatternMap::getPatternFromBasePattern(const UnicodeString& basePattern, UBool& skeletonWasSpecified) const { // key to search for
   PtnElem *curElem;

   if ((curElem=getHeader(basePattern.charAt(0)))==nullptr) {
       return nullptr;  // no match
   }

   do  {
       if ( basePattern.compare(curElem->basePattern)==0 ) {
          skeletonWasSpecified = curElem->skeletonWasSpecified;
          return &(curElem->pattern);
       }
       curElem = curElem->next.getAlias();
   } while (curElem != nullptr);

   return nullptr;
}  // PatternMap::getFromBasePattern


// Find the pattern from the given skeleton.
// At least when this is called from getBestRaw & addPattern (in which case specifiedSkeletonPtr is non-nullptr),
// the comparison should be based on skeleton.original (which is unique and tied to the distance measurement in bestRaw)
// and not skeleton.baseOriginal (which is not unique); otherwise we may pick a different skeleton than the one with the
// optimum distance value in getBestRaw. When this is called from public getRedundants (specifiedSkeletonPtr is nullptr),
// for now it will continue to compare based on baseOriginal so as not to change the behavior unnecessarily.
const UnicodeString *
PatternMap::getPatternFromSkeleton(const PtnSkeleton& skeleton, const PtnSkeleton** specifiedSkeletonPtr) const { // key to search for
   PtnElem *curElem;

   if (specifiedSkeletonPtr) {
       *specifiedSkeletonPtr = nullptr;
   }

   // find boot entry
   char16_t baseChar = skeleton.getFirstChar();
   if ((curElem=getHeader(baseChar))==nullptr) {
       return nullptr;  // no match
   }

   do  {
       UBool equal;
       if (specifiedSkeletonPtr != nullptr) { // called from DateTimePatternGenerator::getBestRaw or addPattern, use original
           equal = curElem->skeleton->original == skeleton.original;
       } else { // called from DateTimePatternGenerator::getRedundants, use baseOriginal
           equal = curElem->skeleton->baseOriginal == skeleton.baseOriginal;
       }
       if (equal) {
           if (specifiedSkeletonPtr && curElem->skeletonWasSpecified) {
               *specifiedSkeletonPtr = curElem->skeleton.getAlias();
           }
           return &(curElem->pattern);
       }
       curElem = curElem->next.getAlias();
   } while (curElem != nullptr);

   return nullptr;
}

UBool
PatternMap::equals(const PatternMap& other) const {
    if ( this==&other ) {
        return true;
    }
    for (int32_t bootIndex = 0; bootIndex < MAX_PATTERN_ENTRIES; ++bootIndex) {
        if (boot[bootIndex] == other.boot[bootIndex]) {
            continue;
        }
        if ((boot[bootIndex] == nullptr) || (other.boot[bootIndex] == nullptr)) {
            return false;
        }
        PtnElem *otherElem = other.boot[bootIndex];
        PtnElem *myElem = boot[bootIndex];
        while ((otherElem != nullptr) || (myElem != nullptr)) {
            if ( myElem == otherElem ) {
                break;
            }
            if ((otherElem == nullptr) || (myElem == nullptr)) {
                return false;
            }
            if ( (myElem->basePattern != otherElem->basePattern) ||
                 (myElem->pattern != otherElem->pattern) ) {
                return false;
            }
            if ((myElem->skeleton.getAlias() != otherElem->skeleton.getAlias()) &&
                !myElem->skeleton->equals(*(otherElem->skeleton))) {
                return false;
            }
            myElem = myElem->next.getAlias();
            otherElem = otherElem->next.getAlias();
        }
    }
    return true;
}

// find any key existing in the mapping table already.
// return true if there is an existing key, otherwise return false.
PtnElem*
PatternMap::getDuplicateElem(
            const UnicodeString &basePattern,
            const PtnSkeleton &skeleton,
            PtnElem *baseElem) {
   PtnElem *curElem;

   if ( baseElem == nullptr ) {
         return nullptr;
   }
   else {
         curElem = baseElem;
   }
   do {
     if ( basePattern.compare(curElem->basePattern)==0 ) {
         UBool isEqual = true;
         for (int32_t i = 0; i < UDATPG_FIELD_COUNT; ++i) {
            if (curElem->skeleton->type[i] != skeleton.type[i] ) {
                isEqual = false;
                break;
            }
        }
        if (isEqual) {
            return curElem;
        }
     }
     curElem = curElem->next.getAlias();
   } while( curElem != nullptr );

   // end of the list
   return nullptr;

}  // PatternMap::getDuplicateElem

DateTimeMatcher::DateTimeMatcher() {
}

DateTimeMatcher::~DateTimeMatcher() {}

DateTimeMatcher::DateTimeMatcher(const DateTimeMatcher& other) {
    copyFrom(other.skeleton);
}

DateTimeMatcher& DateTimeMatcher::operator=(const DateTimeMatcher& other) {
    copyFrom(other.skeleton);
    return *this;
}


void
DateTimeMatcher::set(const UnicodeString& pattern, FormatParser* fp) {
    PtnSkeleton localSkeleton;
    return set(pattern, fp, localSkeleton);
}

void
DateTimeMatcher::set(const UnicodeString& pattern, FormatParser* fp, PtnSkeleton& skeletonResult) {
    int32_t i;
    for (i=0; i<UDATPG_FIELD_COUNT; ++i) {
        skeletonResult.type[i] = NONE;
    }
    skeletonResult.original.clear();
    skeletonResult.baseOriginal.clear();
    skeletonResult.addedDefaultDayPeriod = false;

    fp->set(pattern);
    for (i=0; i < fp->itemNumber; i++) {
        const UnicodeString& value = fp->items[i];
        // don't skip 'a' anymore, dayPeriod handled specially below

        if ( fp->isQuoteLiteral(value) ) {
            UnicodeString quoteLiteral;
            fp->getQuoteLiteral(quoteLiteral, &i);
            continue;
        }
        int32_t canonicalIndex = fp->getCanonicalIndex(value);
        if (canonicalIndex < 0) {
            continue;
        }
        const dtTypeElem *row = &dtTypes[canonicalIndex];
        int32_t field = row->field;
        skeletonResult.original.populate(field, value);
        char16_t repeatChar = row->patternChar;
        int32_t repeatCount = row->minLen;
        skeletonResult.baseOriginal.populate(field, repeatChar, repeatCount);
        int16_t subField = row->type;
        if (row->type > 0) {
            U_ASSERT(value.length() < INT16_MAX);
            subField += static_cast<int16_t>(value.length());
        }
        skeletonResult.type[field] = subField;
    }

    // #20739, we have a skeleton with minutes and milliseconds, but no seconds
    //
    // Theoretically we would need to check and fix all fields with "gaps":
    // for example year-day (no month), month-hour (no day), and so on, All the possible field combinations.
    // Plus some smartness: year + hour => should we add month, or add day-of-year?
    // What about month + day-of-week, or month + am/pm indicator.
    // I think beyond a certain point we should not try to fix bad developer input and try guessing what they mean.
    // Garbage in, garbage out.
    if (!skeletonResult.original.isFieldEmpty(UDATPG_MINUTE_FIELD)
        && !skeletonResult.original.isFieldEmpty(UDATPG_FRACTIONAL_SECOND_FIELD)
        && skeletonResult.original.isFieldEmpty(UDATPG_SECOND_FIELD)) {
        // Force the use of seconds
        for (i = 0; dtTypes[i].patternChar != 0; i++) {
            if (dtTypes[i].field == UDATPG_SECOND_FIELD) {
                // first entry for UDATPG_SECOND_FIELD
                skeletonResult.original.populate(UDATPG_SECOND_FIELD, dtTypes[i].patternChar, dtTypes[i].minLen);
                skeletonResult.baseOriginal.populate(UDATPG_SECOND_FIELD, dtTypes[i].patternChar, dtTypes[i].minLen);
                // We add value.length, same as above, when type is first initialized.
                // The value we want to "fake" here is "s", and 1 means "s".length()
                int16_t subField = dtTypes[i].type;
                skeletonResult.type[UDATPG_SECOND_FIELD] = (subField > 0) ? subField + 1 : subField;
                break;
            }
        }
    }

    // #13183, handle special behavior for day period characters (a, b, B)
    if (!skeletonResult.original.isFieldEmpty(UDATPG_HOUR_FIELD)) {
        if (skeletonResult.original.getFieldChar(UDATPG_HOUR_FIELD)==LOW_H || skeletonResult.original.getFieldChar(UDATPG_HOUR_FIELD)==CAP_K) {
            // We have a skeleton with 12-hour-cycle format
            if (skeletonResult.original.isFieldEmpty(UDATPG_DAYPERIOD_FIELD)) {
                // But we do not have a day period in the skeleton; add the default DAYPERIOD (currently "a")
                for (i = 0; dtTypes[i].patternChar != 0; i++) {
                    if ( dtTypes[i].field == UDATPG_DAYPERIOD_FIELD ) {
                        // first entry for UDATPG_DAYPERIOD_FIELD
                        skeletonResult.original.populate(UDATPG_DAYPERIOD_FIELD, dtTypes[i].patternChar, dtTypes[i].minLen);
                        skeletonResult.baseOriginal.populate(UDATPG_DAYPERIOD_FIELD, dtTypes[i].patternChar, dtTypes[i].minLen);
                        skeletonResult.type[UDATPG_DAYPERIOD_FIELD] = dtTypes[i].type;
                        skeletonResult.addedDefaultDayPeriod = true;
                        break;
                    }
                }
            }
        } else {
            // Skeleton has 24-hour-cycle hour format and has dayPeriod, delete dayPeriod (i.e. ignore it)
            skeletonResult.original.clearField(UDATPG_DAYPERIOD_FIELD);
            skeletonResult.baseOriginal.clearField(UDATPG_DAYPERIOD_FIELD);
            skeletonResult.type[UDATPG_DAYPERIOD_FIELD] = NONE;
        }
    }
    copyFrom(skeletonResult);
}

void
DateTimeMatcher::getBasePattern(UnicodeString &result ) {
    result.remove(); // Reset the result first.
    skeleton.baseOriginal.appendTo(result);
}

UnicodeString
DateTimeMatcher::getPattern() {
    UnicodeString result;
    return skeleton.original.appendTo(result);
}

int32_t
DateTimeMatcher::getDistance(const DateTimeMatcher& other, int32_t includeMask, DistanceInfo& distanceInfo) const {
    int32_t result = 0;
    distanceInfo.clear();
    for (int32_t i=0; i<UDATPG_FIELD_COUNT; ++i ) {
        int32_t myType = (includeMask&(1<<i))==0 ? 0 : skeleton.type[i];
        int32_t otherType = other.skeleton.type[i];
        if (myType==otherType) {
            continue;
        }
        if (myType==0) {// and other is not
            result += EXTRA_FIELD;
            distanceInfo.addExtra(i);
        }
        else {
            if (otherType==0) {
                result += MISSING_FIELD;
                distanceInfo.addMissing(i);
            }
            else {
                result += abs(myType - otherType);
            }
        }

    }
    return result;
}

void
DateTimeMatcher::copyFrom(const PtnSkeleton& newSkeleton) {
    skeleton.copyFrom(newSkeleton);
}

void
DateTimeMatcher::copyFrom() {
    // same as clear
    skeleton.clear();
}

UBool
DateTimeMatcher::equals(const DateTimeMatcher* other) const {
    if (other==nullptr) { return false; }
    return skeleton.original == other->skeleton.original;
}

int32_t
DateTimeMatcher::getFieldMask() const {
    int32_t result = 0;

    for (int32_t i=0; i<UDATPG_FIELD_COUNT; ++i) {
        if (skeleton.type[i]!=0) {
            result |= (1<<i);
        }
    }
    return result;
}

PtnSkeleton*
DateTimeMatcher::getSkeletonPtr() {
    return &skeleton;
}

FormatParser::FormatParser () {
    status = START;
    itemNumber = 0;
}


FormatParser::~FormatParser () {
}


// Find the next token with the starting position and length
// Note: the startPos may
FormatParser::TokenStatus
FormatParser::setTokens(const UnicodeString& pattern, int32_t startPos, int32_t *len) {
    int32_t curLoc = startPos;
    if ( curLoc >= pattern.length()) {
        return DONE;
    }
    // check the current char is between A-Z or a-z
    do {
        char16_t c=pattern.charAt(curLoc);
        if ( (c>=CAP_A && c<=CAP_Z) || (c>=LOW_A && c<=LOW_Z) ) {
           curLoc++;
        }
        else {
               startPos = curLoc;
               *len=1;
               return ADD_TOKEN;
        }

        if ( pattern.charAt(curLoc)!= pattern.charAt(startPos) ) {
            break;  // not the same token
        }
    } while(curLoc <= pattern.length());
    *len = curLoc-startPos;
    return ADD_TOKEN;
}

void
FormatParser::set(const UnicodeString& pattern) {
    int32_t startPos = 0;
    TokenStatus result = START;
    int32_t len = 0;
    itemNumber = 0;

    do {
        result = setTokens( pattern, startPos, &len );
        if ( result == ADD_TOKEN )
        {
            items[itemNumber++] = UnicodeString(pattern, startPos, len );
            startPos += len;
        }
        else {
            break;
        }
    } while (result==ADD_TOKEN && itemNumber < MAX_DT_TOKEN);
}

int32_t
FormatParser::getCanonicalIndex(const UnicodeString& s, UBool strict) {
    int32_t len = s.length();
    if (len == 0) {
        return -1;
    }
    char16_t ch = s.charAt(0);

    // Verify that all are the same character.
    for (int32_t l = 1; l < len; l++) {
        if (ch != s.charAt(l)) {
            return -1;
        }
    }
    int32_t i = 0;
    int32_t bestRow = -1;
    while (dtTypes[i].patternChar != 0x0000) {
        if ( dtTypes[i].patternChar != ch ) {
            ++i;
            continue;
        }
        bestRow = i;
        if (dtTypes[i].patternChar != dtTypes[i+1].patternChar) {
            return i;
        }
        if (dtTypes[i+1].minLen <= len) {
            ++i;
            continue;
        }
        return i;
    }
    return strict ? -1 : bestRow;
}

UBool
FormatParser::isQuoteLiteral(const UnicodeString& s) {
    return (UBool)(s.charAt(0) == SINGLE_QUOTE);
}

// This function assumes the current itemIndex points to the quote literal.
// Please call isQuoteLiteral prior to this function.
void
FormatParser::getQuoteLiteral(UnicodeString& quote, int32_t *itemIndex) {
    int32_t i = *itemIndex;

    quote.remove();
    if (items[i].charAt(0)==SINGLE_QUOTE) {
        quote += items[i];
        ++i;
    }
    while ( i < itemNumber ) {
        if ( items[i].charAt(0)==SINGLE_QUOTE ) {
            if ( (i+1<itemNumber) && (items[i+1].charAt(0)==SINGLE_QUOTE)) {
                // two single quotes e.g. 'o''clock'
                quote += items[i++];
                quote += items[i++];
                continue;
            }
            else {
                quote += items[i];
                break;
            }
        }
        else {
            quote += items[i];
        }
        ++i;
    }
    *itemIndex=i;
}

UBool
FormatParser::isPatternSeparator(const UnicodeString& field) const {
    for (int32_t i=0; i<field.length(); ++i ) {
        char16_t c= field.charAt(i);
        if ( (c==SINGLE_QUOTE) || (c==BACKSLASH) || (c==SPACE) || (c==COLON) ||
             (c==QUOTATION_MARK) || (c==COMMA) || (c==HYPHEN) ||(items[i].charAt(0)==DOT) ) {
            continue;
        }
        else {
            return false;
        }
    }
    return true;
}

DistanceInfo::~DistanceInfo() {}

void
DistanceInfo::setTo(const DistanceInfo& other) {
    missingFieldMask = other.missingFieldMask;
    extraFieldMask= other.extraFieldMask;
}

PatternMapIterator::PatternMapIterator(UErrorCode& status) :
    bootIndex(0), nodePtr(nullptr), matcher(nullptr), patternMap(nullptr)
{
    if (U_FAILURE(status)) { return; }
    matcher.adoptInsteadAndCheckErrorCode(new DateTimeMatcher(), status);
}

PatternMapIterator::~PatternMapIterator() {
}

void
PatternMapIterator::set(PatternMap& newPatternMap) {
    this->patternMap=&newPatternMap;
}

PtnSkeleton*
PatternMapIterator::getSkeleton() const {
    if ( nodePtr == nullptr ) {
        return nullptr;
    }
    else {
        return nodePtr->skeleton.getAlias();
    }
}

UBool
PatternMapIterator::hasNext() const {
    int32_t headIndex = bootIndex;
    PtnElem *curPtr = nodePtr;

    if (patternMap==nullptr) {
        return false;
    }
    while ( headIndex < MAX_PATTERN_ENTRIES ) {
        if ( curPtr != nullptr ) {
            if ( curPtr->next != nullptr ) {
                return true;
            }
            else {
                headIndex++;
                curPtr=nullptr;
                continue;
            }
        }
        else {
            if ( patternMap->boot[headIndex] != nullptr ) {
                return true;
            }
            else {
                headIndex++;
                continue;
            }
        }
    }
    return false;
}

DateTimeMatcher&
PatternMapIterator::next() {
    while ( bootIndex < MAX_PATTERN_ENTRIES ) {
        if ( nodePtr != nullptr ) {
            if ( nodePtr->next != nullptr ) {
                nodePtr = nodePtr->next.getAlias();
                break;
            }
            else {
                bootIndex++;
                nodePtr=nullptr;
                continue;
            }
        }
        else {
            if ( patternMap->boot[bootIndex] != nullptr ) {
                nodePtr = patternMap->boot[bootIndex];
                break;
            }
            else {
                bootIndex++;
                continue;
            }
        }
    }
    if (nodePtr!=nullptr) {
        matcher->copyFrom(*nodePtr->skeleton);
    }
    else {
        matcher->copyFrom();
    }
    return *matcher;
}


SkeletonFields::SkeletonFields() {
    // Set initial values to zero
    clear();
}

void SkeletonFields::clear() {
    uprv_memset(chars, 0, sizeof(chars));
    uprv_memset(lengths, 0, sizeof(lengths));
}

void SkeletonFields::copyFrom(const SkeletonFields& other) {
    uprv_memcpy(chars, other.chars, sizeof(chars));
    uprv_memcpy(lengths, other.lengths, sizeof(lengths));
}

void SkeletonFields::clearField(int32_t field) {
    chars[field] = 0;
    lengths[field] = 0;
}

char16_t SkeletonFields::getFieldChar(int32_t field) const {
    return chars[field];
}

int32_t SkeletonFields::getFieldLength(int32_t field) const {
    return lengths[field];
}

void SkeletonFields::populate(int32_t field, const UnicodeString& value) {
    populate(field, value.charAt(0), value.length());
}

void SkeletonFields::populate(int32_t field, char16_t ch, int32_t length) {
    chars[field] = (int8_t) ch;
    lengths[field] = (int8_t) length;
}

UBool SkeletonFields::isFieldEmpty(int32_t field) const {
    return lengths[field] == 0;
}

UnicodeString& SkeletonFields::appendTo(UnicodeString& string) const {
    for (int32_t i = 0; i < UDATPG_FIELD_COUNT; ++i) {
        appendFieldTo(i, string);
    }
    return string;
}

UnicodeString& SkeletonFields::appendFieldTo(int32_t field, UnicodeString& string) const {
    char16_t ch(chars[field]);
    int32_t length = (int32_t) lengths[field];

    for (int32_t i=0; i<length; i++) {
        string += ch;
    }
    return string;
}

char16_t SkeletonFields::getFirstChar() const {
    for (int32_t i = 0; i < UDATPG_FIELD_COUNT; ++i) {
        if (lengths[i] != 0) {
            return chars[i];
        }
    }
    return '\0';
}


PtnSkeleton::PtnSkeleton()
    : addedDefaultDayPeriod(false) {
}

PtnSkeleton::PtnSkeleton(const PtnSkeleton& other) {
    copyFrom(other);
}

void PtnSkeleton::copyFrom(const PtnSkeleton& other) {
    uprv_memcpy(type, other.type, sizeof(type));
    original.copyFrom(other.original);
    baseOriginal.copyFrom(other.baseOriginal);
    addedDefaultDayPeriod = other.addedDefaultDayPeriod;
}

void PtnSkeleton::clear() {
    uprv_memset(type, 0, sizeof(type));
    original.clear();
    baseOriginal.clear();
}

UBool
PtnSkeleton::equals(const PtnSkeleton& other) const  {
    return (original == other.original)
        && (baseOriginal == other.baseOriginal)
        && (uprv_memcmp(type, other.type, sizeof(type)) == 0);
}

UnicodeString
PtnSkeleton::getSkeleton() const {
    UnicodeString result;
    result = original.appendTo(result);
    int32_t pos;
    if (addedDefaultDayPeriod && (pos = result.indexOf(LOW_A)) >= 0) {
        // for backward compatibility: if DateTimeMatcher.set added a single 'a' that
        // was not in the provided skeleton, remove it here before returning skeleton.
        result.remove(pos, 1);
    }
    return result;
}

UnicodeString
PtnSkeleton::getBaseSkeleton() const {
    UnicodeString result;
    result = baseOriginal.appendTo(result);
    int32_t pos;
    if (addedDefaultDayPeriod && (pos = result.indexOf(LOW_A)) >= 0) {
        // for backward compatibility: if DateTimeMatcher.set added a single 'a' that
        // was not in the provided skeleton, remove it here before returning skeleton.
        result.remove(pos, 1);
    }
    return result;
}

char16_t
PtnSkeleton::getFirstChar() const {
    return baseOriginal.getFirstChar();
}

PtnSkeleton::~PtnSkeleton() {
}

PtnElem::PtnElem(const UnicodeString &basePat, const UnicodeString &pat) :
    basePattern(basePat), skeleton(nullptr), pattern(pat), next(nullptr)
{
}

PtnElem::~PtnElem() {
}

DTSkeletonEnumeration::DTSkeletonEnumeration(PatternMap& patternMap, dtStrEnum type, UErrorCode& status) : fSkeletons(nullptr) {
    PtnElem  *curElem;
    PtnSkeleton *curSkeleton;
    UnicodeString s;
    int32_t bootIndex;

    pos=0;
    fSkeletons.adoptInsteadAndCheckErrorCode(new UVector(status), status);
    if (U_FAILURE(status)) {
        return;
    }

    for (bootIndex=0; bootIndex<MAX_PATTERN_ENTRIES; ++bootIndex ) {
        curElem = patternMap.boot[bootIndex];
        while (curElem!=nullptr) {
            switch(type) {
                case DT_BASESKELETON:
                    s=curElem->basePattern;
                    break;
                case DT_PATTERN:
                    s=curElem->pattern;
                    break;
                case DT_SKELETON:
                    curSkeleton=curElem->skeleton.getAlias();
                    s=curSkeleton->getSkeleton();
                    break;
            }
            if ( !isCanonicalItem(s) ) {
                LocalPointer<UnicodeString> newElem(s.clone(), status);
                if (U_FAILURE(status)) { 
                    return;
                }
                fSkeletons->addElement(newElem.getAlias(), status);
                if (U_FAILURE(status)) {
                    fSkeletons.adoptInstead(nullptr);
                    return;
                }
                newElem.orphan(); // fSkeletons vector now owns the UnicodeString (although it
                                  // does not use a deleter function to manage the ownership).
            }
            curElem = curElem->next.getAlias();
        }
    }
    if ((bootIndex==MAX_PATTERN_ENTRIES) && (curElem!=nullptr) ) {
        status = U_BUFFER_OVERFLOW_ERROR;
    }
}

const UnicodeString*
DTSkeletonEnumeration::snext(UErrorCode& status) {
    if (U_SUCCESS(status) && fSkeletons.isValid() && pos < fSkeletons->size()) {
        return (const UnicodeString*)fSkeletons->elementAt(pos++);
    }
    return nullptr;
}

void
DTSkeletonEnumeration::reset(UErrorCode& /*status*/) {
    pos=0;
}

int32_t
DTSkeletonEnumeration::count(UErrorCode& /*status*/) const {
   return (fSkeletons.isNull()) ? 0 : fSkeletons->size();
}

UBool
DTSkeletonEnumeration::isCanonicalItem(const UnicodeString& item) {
    if ( item.length() != 1 ) {
        return false;
    }
    for (int32_t i=0; i<UDATPG_FIELD_COUNT; ++i) {
        if (item.charAt(0)==Canonical_Items[i]) {
            return true;
        }
    }
    return false;
}

DTSkeletonEnumeration::~DTSkeletonEnumeration() {
    UnicodeString *s;
    if (fSkeletons.isValid()) {
        for (int32_t i = 0; i < fSkeletons->size(); ++i) {
            if ((s = (UnicodeString *)fSkeletons->elementAt(i)) != nullptr) {
                delete s;
            }
        }
    }
}

DTRedundantEnumeration::DTRedundantEnumeration() : pos(0), fPatterns(nullptr) {
}

void
DTRedundantEnumeration::add(const UnicodeString& pattern, UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    if (fPatterns.isNull())  {
        fPatterns.adoptInsteadAndCheckErrorCode(new UVector(status), status);
        if (U_FAILURE(status)) {
            return;
       }
    }
    LocalPointer<UnicodeString> newElem(new UnicodeString(pattern), status);
    if (U_FAILURE(status)) {
        return;
    }
    fPatterns->addElement(newElem.getAlias(), status);
    if (U_FAILURE(status)) {
        fPatterns.adoptInstead(nullptr);
        return;
    }
    newElem.orphan(); // fPatterns now owns the string, although a UVector
                      // deleter function is not used to manage that ownership.
}

const UnicodeString*
DTRedundantEnumeration::snext(UErrorCode& status) {
    if (U_SUCCESS(status) && fPatterns.isValid() && pos < fPatterns->size()) {
        return (const UnicodeString*)fPatterns->elementAt(pos++);
    }
    return nullptr;
}

void
DTRedundantEnumeration::reset(UErrorCode& /*status*/) {
    pos=0;
}

int32_t
DTRedundantEnumeration::count(UErrorCode& /*status*/) const {
    return (fPatterns.isNull()) ? 0 : fPatterns->size();
}

UBool
DTRedundantEnumeration::isCanonicalItem(const UnicodeString& item) const {
    if ( item.length() != 1 ) {
        return false;
    }
    for (int32_t i=0; i<UDATPG_FIELD_COUNT; ++i) {
        if (item.charAt(0)==Canonical_Items[i]) {
            return true;
        }
    }
    return false;
}

DTRedundantEnumeration::~DTRedundantEnumeration() {
    UnicodeString *s;
    if (fPatterns.isValid()) {
        for (int32_t i = 0; i < fPatterns->size(); ++i) {
            if ((s = (UnicodeString *)fPatterns->elementAt(i)) != nullptr) {
                delete s;
            }
        }
    }    
}

U_NAMESPACE_END


#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
