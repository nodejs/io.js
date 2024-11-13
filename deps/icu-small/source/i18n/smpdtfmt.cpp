// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 1997-2016, International Business Machines Corporation and    *
* others. All Rights Reserved.                                                *
*******************************************************************************
*
* File SMPDTFMT.CPP
*
* Modification History:
*
*   Date        Name        Description
*   02/19/97    aliu        Converted from java.
*   03/31/97    aliu        Modified extensively to work with 50 locales.
*   04/01/97    aliu        Added support for centuries.
*   07/09/97    helena      Made ParsePosition into a class.
*   07/21/98    stephen     Added initializeDefaultCentury.
*                             Removed getZoneIndex (added in DateFormatSymbols)
*                             Removed subParseLong
*                             Removed chk
*   02/22/99    stephen     Removed character literals for EBCDIC safety
*   10/14/99    aliu        Updated 2-digit year parsing so that only "00" thru
*                           "99" are recognized. {j28 4182066}
*   11/15/99    weiv        Added support for week of year/day of week format
********************************************************************************
*/

#define ZID_KEY_MAX 128

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#include "unicode/smpdtfmt.h"
#include "unicode/dtfmtsym.h"
#include "unicode/ures.h"
#include "unicode/msgfmt.h"
#include "unicode/calendar.h"
#include "unicode/gregocal.h"
#include "unicode/timezone.h"
#include "unicode/decimfmt.h"
#include "unicode/dcfmtsym.h"
#include "unicode/uchar.h"
#include "unicode/uniset.h"
#include "unicode/ustring.h"
#include "unicode/basictz.h"
#include "unicode/simpleformatter.h"
#include "unicode/simplenumberformatter.h"
#include "unicode/simpletz.h"
#include "unicode/rbtz.h"
#include "unicode/tzfmt.h"
#include "unicode/ucasemap.h"
#include "unicode/utf16.h"
#include "unicode/vtzone.h"
#include "unicode/udisplaycontext.h"
#include "unicode/brkiter.h"
#include "unicode/rbnf.h"
#include "unicode/dtptngen.h"
#include "uresimp.h"
#include "olsontz.h"
#include "patternprops.h"
#include "fphdlimp.h"
#include "hebrwcal.h"
#include "cstring.h"
#include "uassert.h"
#include "cmemory.h"
#include "umutex.h"
#include "mutex.h"
#include <float.h>
#include "smpdtfst.h"
#include "sharednumberformat.h"
#include "ucasemap_imp.h"
#include "ustr_imp.h"
#include "charstr.h"
#include "uvector.h"
#include "cstr.h"
#include "dayperiodrules.h"
#include "tznames_impl.h"   // ZONE_NAME_U16_MAX
#include "number_utypes.h"

#if defined( U_DEBUG_CALSVC ) || defined (U_DEBUG_CAL)
#include <stdio.h>
#endif

// *****************************************************************************
// class SimpleDateFormat
// *****************************************************************************

U_NAMESPACE_BEGIN

/**
 * Last-resort string to use for "GMT" when constructing time zone strings.
 */
// For time zones that have no names, use strings GMT+minutes and
// GMT-minutes. For instance, in France the time zone is GMT+60.
// Also accepted are GMT+H:MM or GMT-H:MM.
// Currently not being used
//static const char16_t gGmt[]      = {0x0047, 0x004D, 0x0054, 0x0000};         // "GMT"
//static const char16_t gGmtPlus[]  = {0x0047, 0x004D, 0x0054, 0x002B, 0x0000}; // "GMT+"
//static const char16_t gGmtMinus[] = {0x0047, 0x004D, 0x0054, 0x002D, 0x0000}; // "GMT-"
//static const char16_t gDefGmtPat[]       = {0x0047, 0x004D, 0x0054, 0x007B, 0x0030, 0x007D, 0x0000}; /* GMT{0} */
//static const char16_t gDefGmtNegHmsPat[] = {0x002D, 0x0048, 0x0048, 0x003A, 0x006D, 0x006D, 0x003A, 0x0073, 0x0073, 0x0000}; /* -HH:mm:ss */
//static const char16_t gDefGmtNegHmPat[]  = {0x002D, 0x0048, 0x0048, 0x003A, 0x006D, 0x006D, 0x0000}; /* -HH:mm */
//static const char16_t gDefGmtPosHmsPat[] = {0x002B, 0x0048, 0x0048, 0x003A, 0x006D, 0x006D, 0x003A, 0x0073, 0x0073, 0x0000}; /* +HH:mm:ss */
//static const char16_t gDefGmtPosHmPat[]  = {0x002B, 0x0048, 0x0048, 0x003A, 0x006D, 0x006D, 0x0000}; /* +HH:mm */
//static const char16_t gUt[]       = {0x0055, 0x0054, 0x0000};  // "UT"
//static const char16_t gUtc[]      = {0x0055, 0x0054, 0x0043, 0x0000};  // "UT"

typedef enum GmtPatSize {
    kGmtLen = 3,
    kGmtPatLen = 6,
    kNegHmsLen = 9,
    kNegHmLen = 6,
    kPosHmsLen = 9,
    kPosHmLen = 6,
    kUtLen = 2,
    kUtcLen = 3
} GmtPatSize;

// Stuff needed for numbering system overrides

typedef enum OvrStrType {
    kOvrStrDate = 0,
    kOvrStrTime = 1,
    kOvrStrBoth = 2
} OvrStrType;

static const UDateFormatField kDateFields[] = {
    UDAT_YEAR_FIELD,
    UDAT_MONTH_FIELD,
    UDAT_DATE_FIELD,
    UDAT_DAY_OF_YEAR_FIELD,
    UDAT_DAY_OF_WEEK_IN_MONTH_FIELD,
    UDAT_WEEK_OF_YEAR_FIELD,
    UDAT_WEEK_OF_MONTH_FIELD,
    UDAT_YEAR_WOY_FIELD,
    UDAT_EXTENDED_YEAR_FIELD,
    UDAT_JULIAN_DAY_FIELD,
    UDAT_STANDALONE_DAY_FIELD,
    UDAT_STANDALONE_MONTH_FIELD,
    UDAT_QUARTER_FIELD,
    UDAT_STANDALONE_QUARTER_FIELD,
    UDAT_YEAR_NAME_FIELD,
    UDAT_RELATED_YEAR_FIELD };
static const int8_t kDateFieldsCount = 16;

static const UDateFormatField kTimeFields[] = {
    UDAT_HOUR_OF_DAY1_FIELD,
    UDAT_HOUR_OF_DAY0_FIELD,
    UDAT_MINUTE_FIELD,
    UDAT_SECOND_FIELD,
    UDAT_FRACTIONAL_SECOND_FIELD,
    UDAT_HOUR1_FIELD,
    UDAT_HOUR0_FIELD,
    UDAT_MILLISECONDS_IN_DAY_FIELD,
    UDAT_TIMEZONE_RFC_FIELD,
    UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD };
static const int8_t kTimeFieldsCount = 10;


// This is a pattern-of-last-resort used when we can't load a usable pattern out
// of a resource.
static const char16_t gDefaultPattern[] =
{
    0x79, 0x4D, 0x4D, 0x64, 0x64, 0x20, 0x68, 0x68, 0x3A, 0x6D, 0x6D, 0x20, 0x61, 0
};  /* "yMMdd hh:mm a" */

// This prefix is designed to NEVER MATCH real text, in order to
// suppress the parsing of negative numbers.  Adjust as needed (if
// this becomes valid Unicode).
static const char16_t SUPPRESS_NEGATIVE_PREFIX[] = {0xAB00, 0};

/**
 * These are the tags we expect to see in normal resource bundle files associated
 * with a locale.
 */
static const char16_t QUOTE = 0x27; // Single quote

/*
 * The field range check bias for each UDateFormatField.
 * The bias is added to the minimum and maximum values
 * before they are compared to the parsed number.
 * For example, the calendar stores zero-based month numbers
 * but the parsed month numbers start at 1, so the bias is 1.
 *
 * A value of -1 means that the value is not checked.
 */
static const int32_t gFieldRangeBias[] = {
    -1,  // 'G' - UDAT_ERA_FIELD
    -1,  // 'y' - UDAT_YEAR_FIELD
     1,  // 'M' - UDAT_MONTH_FIELD
     0,  // 'd' - UDAT_DATE_FIELD
    -1,  // 'k' - UDAT_HOUR_OF_DAY1_FIELD
    -1,  // 'H' - UDAT_HOUR_OF_DAY0_FIELD
     0,  // 'm' - UDAT_MINUTE_FIELD
     0,  // 's' - UDAT_SECOND_FIELD
    -1,  // 'S' - UDAT_FRACTIONAL_SECOND_FIELD (0-999?)
    -1,  // 'E' - UDAT_DAY_OF_WEEK_FIELD (1-7?)
    -1,  // 'D' - UDAT_DAY_OF_YEAR_FIELD (1 - 366?)
    -1,  // 'F' - UDAT_DAY_OF_WEEK_IN_MONTH_FIELD (1-5?)
    -1,  // 'w' - UDAT_WEEK_OF_YEAR_FIELD (1-52?)
    -1,  // 'W' - UDAT_WEEK_OF_MONTH_FIELD (1-5?)
    -1,  // 'a' - UDAT_AM_PM_FIELD
    -1,  // 'h' - UDAT_HOUR1_FIELD
    -1,  // 'K' - UDAT_HOUR0_FIELD
    -1,  // 'z' - UDAT_TIMEZONE_FIELD
    -1,  // 'Y' - UDAT_YEAR_WOY_FIELD
    -1,  // 'e' - UDAT_DOW_LOCAL_FIELD
    -1,  // 'u' - UDAT_EXTENDED_YEAR_FIELD
    -1,  // 'g' - UDAT_JULIAN_DAY_FIELD
    -1,  // 'A' - UDAT_MILLISECONDS_IN_DAY_FIELD
    -1,  // 'Z' - UDAT_TIMEZONE_RFC_FIELD
    -1,  // 'v' - UDAT_TIMEZONE_GENERIC_FIELD
     0,  // 'c' - UDAT_STANDALONE_DAY_FIELD
     1,  // 'L' - UDAT_STANDALONE_MONTH_FIELD
    -1,  // 'Q' - UDAT_QUARTER_FIELD (1-4?)
    -1,  // 'q' - UDAT_STANDALONE_QUARTER_FIELD
    -1,  // 'V' - UDAT_TIMEZONE_SPECIAL_FIELD
    -1,  // 'U' - UDAT_YEAR_NAME_FIELD
    -1,  // 'O' - UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD
    -1,  // 'X' - UDAT_TIMEZONE_ISO_FIELD
    -1,  // 'x' - UDAT_TIMEZONE_ISO_LOCAL_FIELD
    -1,  // 'r' - UDAT_RELATED_YEAR_FIELD
#if UDAT_HAS_PATTERN_CHAR_FOR_TIME_SEPARATOR
    -1,  // ':' - UDAT_TIME_SEPARATOR_FIELD
#else
    -1,  // (no pattern character currently) - UDAT_TIME_SEPARATOR_FIELD
#endif
};

// When calendar uses hebr numbering (i.e. he@calendar=hebrew),
// offset the years within the current millennium down to 1-999
static const int32_t HEBREW_CAL_CUR_MILLENIUM_START_YEAR = 5000;
static const int32_t HEBREW_CAL_CUR_MILLENIUM_END_YEAR = 6000;

/**
 * Maximum range for detecting daylight offset of a time zone when parsed time zone
 * string indicates it's daylight saving time, but the detected time zone does not
 * observe daylight saving time at the parsed date.
 */
static const double MAX_DAYLIGHT_DETECTION_RANGE = 30*365*24*60*60*1000.0;

static UMutex LOCK;

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(SimpleDateFormat)

SimpleDateFormat::NSOverride::~NSOverride() {
    if (snf != nullptr) {
        snf->removeRef();
    }
}


void SimpleDateFormat::NSOverride::free() {
    NSOverride *cur = this;
    while (cur) {
        NSOverride *next_temp = cur->next;
        delete cur;
        cur = next_temp;
    }
}

// no matter what the locale's default number format looked like, we want
// to modify it so that it doesn't use thousands separators, doesn't always
// show the decimal point, and recognizes integers only when parsing
static void fixNumberFormatForDates(NumberFormat &nf) {
    nf.setGroupingUsed(false);
    DecimalFormat* decfmt = dynamic_cast<DecimalFormat*>(&nf);
    if (decfmt != nullptr) {
        decfmt->setDecimalSeparatorAlwaysShown(false);
    }
    nf.setParseIntegerOnly(true);
    nf.setMinimumFractionDigits(0); // To prevent "Jan 1.00, 1997.00"
}

static const SharedNumberFormat *createSharedNumberFormat(
        NumberFormat *nfToAdopt) {
    fixNumberFormatForDates(*nfToAdopt);
    const SharedNumberFormat *result = new SharedNumberFormat(nfToAdopt);
    if (result == nullptr) {
        delete nfToAdopt;
    }
    return result;
}

static const SharedNumberFormat *createSharedNumberFormat(
        const Locale &loc, UErrorCode &status) {
    NumberFormat *nf = NumberFormat::createInstance(loc, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    const SharedNumberFormat *result = createSharedNumberFormat(nf);
    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

static const SharedNumberFormat **allocSharedNumberFormatters() {
    const SharedNumberFormat** result = static_cast<const SharedNumberFormat**>(
            uprv_malloc(UDAT_FIELD_COUNT * sizeof(const SharedNumberFormat*)));
    if (result == nullptr) {
        return nullptr;
    }
    for (int32_t i = 0; i < UDAT_FIELD_COUNT; ++i) {
        result[i] = nullptr;
    }
    return result;
}

static void freeSharedNumberFormatters(const SharedNumberFormat ** list) {
    for (int32_t i = 0; i < UDAT_FIELD_COUNT; ++i) {
        SharedObject::clearPtr(list[i]);
    }
    uprv_free(list);
}

const NumberFormat *SimpleDateFormat::getNumberFormatByIndex(
        UDateFormatField index) const {
    if (fSharedNumberFormatters == nullptr ||
        fSharedNumberFormatters[index] == nullptr) {
        return fNumberFormat;
    }
    return &(**fSharedNumberFormatters[index]);
}

//----------------------------------------------------------------------

SimpleDateFormat::~SimpleDateFormat()
{
    delete fSymbols;
    if (fSharedNumberFormatters) {
        freeSharedNumberFormatters(fSharedNumberFormatters);
    }
    delete fTimeZoneFormat;
    delete fSimpleNumberFormatter;

#if !UCONFIG_NO_BREAK_ITERATION
    delete fCapitalizationBrkIter;
#endif
}

//----------------------------------------------------------------------

SimpleDateFormat::SimpleDateFormat(UErrorCode& status)
  :   fLocale(Locale::getDefault())
{
    initializeBooleanAttributes();
    construct(kShort, static_cast<EStyle>(kShort + kDateOffset), fLocale, status);
    initializeDefaultCentury();
}

//----------------------------------------------------------------------

SimpleDateFormat::SimpleDateFormat(const UnicodeString& pattern,
                                   UErrorCode &status)
:   fPattern(pattern),
    fLocale(Locale::getDefault())
{
    fDateOverride.setToBogus();
    fTimeOverride.setToBogus();
    initializeBooleanAttributes();
    initializeCalendar(nullptr,fLocale,status);
    fSymbols = DateFormatSymbols::createForLocale(fLocale, status);
    initialize(fLocale, status);
    initializeDefaultCentury();

}
//----------------------------------------------------------------------

SimpleDateFormat::SimpleDateFormat(const UnicodeString& pattern,
                                   const UnicodeString& override,
                                   UErrorCode &status)
:   fPattern(pattern),
    fLocale(Locale::getDefault())
{
    fDateOverride.setTo(override);
    fTimeOverride.setToBogus();
    initializeBooleanAttributes();
    initializeCalendar(nullptr,fLocale,status);
    fSymbols = DateFormatSymbols::createForLocale(fLocale, status);
    initialize(fLocale, status);
    initializeDefaultCentury();

    processOverrideString(fLocale,override,kOvrStrBoth,status);

}

//----------------------------------------------------------------------

SimpleDateFormat::SimpleDateFormat(const UnicodeString& pattern,
                                   const Locale& locale,
                                   UErrorCode& status)
:   fPattern(pattern),
    fLocale(locale)
{

    fDateOverride.setToBogus();
    fTimeOverride.setToBogus();
    initializeBooleanAttributes();

    initializeCalendar(nullptr,fLocale,status);
    fSymbols = DateFormatSymbols::createForLocale(fLocale, status);
    initialize(fLocale, status);
    initializeDefaultCentury();
}

//----------------------------------------------------------------------

SimpleDateFormat::SimpleDateFormat(const UnicodeString& pattern,
                                   const UnicodeString& override,
                                   const Locale& locale,
                                   UErrorCode& status)
:   fPattern(pattern),
    fLocale(locale)
{

    fDateOverride.setTo(override);
    fTimeOverride.setToBogus();
    initializeBooleanAttributes();

    initializeCalendar(nullptr,fLocale,status);
    fSymbols = DateFormatSymbols::createForLocale(fLocale, status);
    initialize(fLocale, status);
    initializeDefaultCentury();

    processOverrideString(locale,override,kOvrStrBoth,status);

}

//----------------------------------------------------------------------

SimpleDateFormat::SimpleDateFormat(const UnicodeString& pattern,
                                   DateFormatSymbols* symbolsToAdopt,
                                   UErrorCode& status)
:   fPattern(pattern),
    fLocale(Locale::getDefault()),
    fSymbols(symbolsToAdopt)
{

    fDateOverride.setToBogus();
    fTimeOverride.setToBogus();
    initializeBooleanAttributes();

    initializeCalendar(nullptr,fLocale,status);
    initialize(fLocale, status);
    initializeDefaultCentury();
}

//----------------------------------------------------------------------

SimpleDateFormat::SimpleDateFormat(const UnicodeString& pattern,
                                   const DateFormatSymbols& symbols,
                                   UErrorCode& status)
:   fPattern(pattern),
    fLocale(Locale::getDefault()),
    fSymbols(new DateFormatSymbols(symbols))
{

    fDateOverride.setToBogus();
    fTimeOverride.setToBogus();
    initializeBooleanAttributes();

    initializeCalendar(nullptr, fLocale, status);
    initialize(fLocale, status);
    initializeDefaultCentury();
}

//----------------------------------------------------------------------

// Not for public consumption; used by DateFormat
SimpleDateFormat::SimpleDateFormat(EStyle timeStyle,
                                   EStyle dateStyle,
                                   const Locale& locale,
                                   UErrorCode& status)
:   fLocale(locale)
{
    initializeBooleanAttributes();
    construct(timeStyle, dateStyle, fLocale, status);
    if(U_SUCCESS(status)) {
      initializeDefaultCentury();
    }
}

//----------------------------------------------------------------------

/**
 * Not for public consumption; used by DateFormat.  This constructor
 * never fails.  If the resource data is not available, it uses the
 * the last resort symbols.
 */
SimpleDateFormat::SimpleDateFormat(const Locale& locale,
                                   UErrorCode& status)
:   fPattern(gDefaultPattern),
    fLocale(locale)
{
    if (U_FAILURE(status)) return;
    initializeBooleanAttributes();
    initializeCalendar(nullptr, fLocale, status);
    fSymbols = DateFormatSymbols::createForLocale(fLocale, status);
    if (U_FAILURE(status))
    {
        status = U_ZERO_ERROR;
        delete fSymbols;
        // This constructor doesn't fail; it uses last resort data
        fSymbols = new DateFormatSymbols(status);
        /* test for nullptr */
        if (fSymbols == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }

    fDateOverride.setToBogus();
    fTimeOverride.setToBogus();

    initialize(fLocale, status);
    if(U_SUCCESS(status)) {
      initializeDefaultCentury();
    }
}

//----------------------------------------------------------------------

SimpleDateFormat::SimpleDateFormat(const SimpleDateFormat& other)
:   DateFormat(other),
    fLocale(other.fLocale)
{
    initializeBooleanAttributes();
    *this = other;
}

//----------------------------------------------------------------------

SimpleDateFormat& SimpleDateFormat::operator=(const SimpleDateFormat& other)
{
    if (this == &other) {
        return *this;
    }

    // fSimpleNumberFormatter references fNumberFormatter, delete it
    // before we call the = operator which may invalidate fNumberFormatter
    delete fSimpleNumberFormatter;
    fSimpleNumberFormatter = nullptr;

    DateFormat::operator=(other);
    fDateOverride = other.fDateOverride;
    fTimeOverride = other.fTimeOverride;

    delete fSymbols;
    fSymbols = nullptr;

    if (other.fSymbols)
        fSymbols = new DateFormatSymbols(*other.fSymbols);

    fDefaultCenturyStart         = other.fDefaultCenturyStart;
    fDefaultCenturyStartYear     = other.fDefaultCenturyStartYear;
    fHaveDefaultCentury          = other.fHaveDefaultCentury;

    fPattern = other.fPattern;
    fHasMinute = other.fHasMinute;
    fHasSecond = other.fHasSecond;

    fLocale = other.fLocale;

    // TimeZoneFormat can now be set independently via setter.
    // If it is nullptr, it will be lazily initialized from locale.
    delete fTimeZoneFormat;
    fTimeZoneFormat = nullptr;
    TimeZoneFormat *otherTZFormat;
    {
        // Synchronization is required here, when accessing other.fTimeZoneFormat,
        // because another thread may be concurrently executing other.tzFormat(),
        // a logically const function that lazily creates other.fTimeZoneFormat.
        //
        // Without synchronization, reordered memory writes could allow us
        // to see a non-null fTimeZoneFormat before the object itself was
        // fully initialized. In case of a race, it doesn't matter whether
        // we see a null or a fully initialized other.fTimeZoneFormat,
        // only that we avoid seeing a partially initialized object.
        //
        // Once initialized, no const function can modify fTimeZoneFormat,
        // meaning that once we have safely grabbed the other.fTimeZoneFormat
        // pointer, continued synchronization is not required to use it.
        Mutex m(&LOCK);
        otherTZFormat = other.fTimeZoneFormat;
    }
    if (otherTZFormat) {
        fTimeZoneFormat = new TimeZoneFormat(*otherTZFormat);
    }

#if !UCONFIG_NO_BREAK_ITERATION
    if (other.fCapitalizationBrkIter != nullptr) {
        fCapitalizationBrkIter = (other.fCapitalizationBrkIter)->clone();
    }
#endif

    if (fSharedNumberFormatters != nullptr) {
        freeSharedNumberFormatters(fSharedNumberFormatters);
        fSharedNumberFormatters = nullptr;
    }
    if (other.fSharedNumberFormatters != nullptr) {
        fSharedNumberFormatters = allocSharedNumberFormatters();
        if (fSharedNumberFormatters) {
            for (int32_t i = 0; i < UDAT_FIELD_COUNT; ++i) {
                SharedObject::copyPtr(
                        other.fSharedNumberFormatters[i],
                        fSharedNumberFormatters[i]);
            }
        }
    }

    UErrorCode localStatus = U_ZERO_ERROR;
    // SimpleNumberFormatter does not have a copy constructor. Furthermore,
    // it references data from an internal field, fNumberFormatter,
    // so we must rematerialize that reference after copying over the number formatter.
    initSimpleNumberFormatter(localStatus);
    return *this;
}

//----------------------------------------------------------------------

SimpleDateFormat*
SimpleDateFormat::clone() const
{
    return new SimpleDateFormat(*this);
}

//----------------------------------------------------------------------

bool
SimpleDateFormat::operator==(const Format& other) const
{
    if (DateFormat::operator==(other)) {
        // The DateFormat::operator== check for fCapitalizationContext equality above
        //   is sufficient to check equality of all derived context-related data.
        // DateFormat::operator== guarantees following cast is safe
        SimpleDateFormat* that = (SimpleDateFormat*)&other;
        return (fPattern             == that->fPattern &&
                fSymbols             != nullptr && // Check for pathological object
                that->fSymbols       != nullptr && // Check for pathological object
                *fSymbols            == *that->fSymbols &&
                fHaveDefaultCentury  == that->fHaveDefaultCentury &&
                fDefaultCenturyStart == that->fDefaultCenturyStart);
    }
    return false;
}

//----------------------------------------------------------------------
static const char16_t* timeSkeletons[4] = {
    u"jmmsszzzz",   // kFull
    u"jmmssz",      // kLong
    u"jmmss",       // kMedium
    u"jmm",         // kShort
};

void SimpleDateFormat::construct(EStyle timeStyle,
                                 EStyle dateStyle,
                                 const Locale& locale,
                                 UErrorCode& status)
{
    // called by several constructors to load pattern data from the resources
    if (U_FAILURE(status)) return;

    // We will need the calendar to know what type of symbols to load.
    initializeCalendar(nullptr, locale, status);
    if (U_FAILURE(status)) return;

    // Load date time patterns directly from resources.
    const char* cType = fCalendar ? fCalendar->getType() : nullptr;
    LocalUResourceBundlePointer bundle(ures_open(nullptr, locale.getBaseName(), &status));
    if (U_FAILURE(status)) return;

    UBool cTypeIsGregorian = true;
    LocalUResourceBundlePointer dateTimePatterns;
    if (cType != nullptr && uprv_strcmp(cType, "gregorian") != 0) {
        CharString resourcePath("calendar/", status);
        resourcePath.append(cType, status).append("/DateTimePatterns", status);
        dateTimePatterns.adoptInstead(
            ures_getByKeyWithFallback(bundle.getAlias(), resourcePath.data(),
                                      (UResourceBundle*)nullptr, &status));
        cTypeIsGregorian = false;
    }

    // Check for "gregorian" fallback.
    if (cTypeIsGregorian || status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        dateTimePatterns.adoptInstead(
            ures_getByKeyWithFallback(bundle.getAlias(),
                                      "calendar/gregorian/DateTimePatterns",
                                      (UResourceBundle*)nullptr, &status));
    }
    if (U_FAILURE(status)) return;

    LocalUResourceBundlePointer currentBundle;

    if (ures_getSize(dateTimePatterns.getAlias()) <= kDateTime)
    {
        status = U_INVALID_FORMAT_ERROR;
        return;
    }

    setLocaleIDs(ures_getLocaleByType(dateTimePatterns.getAlias(), ULOC_VALID_LOCALE, &status),
                 ures_getLocaleByType(dateTimePatterns.getAlias(), ULOC_ACTUAL_LOCALE, &status));

    // create a symbols object from the locale
    fSymbols = DateFormatSymbols::createForLocale(locale, status);
    if (U_FAILURE(status)) return;
    /* test for nullptr */
    if (fSymbols == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    const char16_t *resStr,*ovrStr;
    int32_t resStrLen,ovrStrLen = 0;
    fDateOverride.setToBogus();
    fTimeOverride.setToBogus();

    UnicodeString timePattern;
    if (timeStyle >= kFull && timeStyle <= kShort) {
        bool hasRgOrHcSubtag = false;
        // also use DTPG if the locale has the "rg" or "hc" ("hours") subtag-- even if the overriding region
        // or hour cycle is the same as the one we get by default, we go through the DateTimePatternGenerator
        UErrorCode dummyErr1 = U_ZERO_ERROR, dummyErr2 = U_ZERO_ERROR;
        if (locale.getKeywordValue("rg", nullptr, 0, dummyErr1) > 0 || locale.getKeywordValue("hours", nullptr, 0, dummyErr2) > 0) {
            hasRgOrHcSubtag = true;
        }

        const char* baseLocID = locale.getBaseName();
        if (baseLocID != nullptr && uprv_strcmp(baseLocID,"und")!=0) {
            UErrorCode useStatus = U_ZERO_ERROR;
            Locale baseLoc(baseLocID);
            Locale validLoc(getLocale(ULOC_VALID_LOCALE, useStatus));
            if (hasRgOrHcSubtag || (U_SUCCESS(useStatus) && validLoc!=baseLoc)) {
                bool useDTPG = hasRgOrHcSubtag;
                const char* baseReg = baseLoc.getCountry(); // empty string if no region
                if ((baseReg != nullptr && baseReg[0] != 0 &&
                     uprv_strncmp(baseReg,validLoc.getCountry(),ULOC_COUNTRY_CAPACITY)!=0)
                        || uprv_strncmp(baseLoc.getLanguage(),validLoc.getLanguage(),ULOC_LANG_CAPACITY)!=0) {
                    // use DTPG if
                    // * baseLoc has a region and validLoc does not have the same one (or has none), OR
                    // * validLoc has a different language code than baseLoc
                    // * the original locale has the rg or hc subtag
                    useDTPG = true;
                }
                if (useDTPG) {
                    // The standard time formats may have the wrong time cycle, because:
                    // the valid locale differs in important ways (region, language) from
                    // the base locale.
                    // We could *also* check whether they do actually have a mismatch with
                    // the time cycle preferences for the region, but that is a lot more
                    // work for little or no additional benefit, since just going ahead
                    // and always synthesizing the time format as per the following should
                    // create a locale-appropriate pattern with cycle that matches the
                    // region preferences anyway.
                    LocalPointer<DateTimePatternGenerator> dtpg(DateTimePatternGenerator::createInstanceNoStdPat(locale, useStatus));
                    if (U_SUCCESS(useStatus)) {
                        UnicodeString timeSkeleton(true, timeSkeletons[timeStyle], -1);
                        timePattern = dtpg->getBestPattern(timeSkeleton, useStatus);
                    }
                }
            }
        }
    }

    // if the pattern should include both date and time information, use the date/time
    // pattern string as a guide to tell use how to glue together the appropriate date
    // and time pattern strings.
    if ((timeStyle != kNone) && (dateStyle != kNone))
    {
        UnicodeString tempus1(timePattern);
        if (tempus1.length() == 0) {
            currentBundle.adoptInstead(
                    ures_getByIndex(dateTimePatterns.getAlias(), static_cast<int32_t>(timeStyle), nullptr, &status));
            if (U_FAILURE(status)) {
               status = U_INVALID_FORMAT_ERROR;
               return;
            }
            switch (ures_getType(currentBundle.getAlias())) {
                case URES_STRING: {
                   resStr = ures_getString(currentBundle.getAlias(), &resStrLen, &status);
                   break;
                }
                case URES_ARRAY: {
                   resStr = ures_getStringByIndex(currentBundle.getAlias(), 0, &resStrLen, &status);
                   ovrStr = ures_getStringByIndex(currentBundle.getAlias(), 1, &ovrStrLen, &status);
                   fTimeOverride.setTo(true, ovrStr, ovrStrLen);
                   break;
                }
                default: {
                   status = U_INVALID_FORMAT_ERROR;
                   return;
                }
            }

            tempus1.setTo(true, resStr, resStrLen);
        }

        currentBundle.adoptInstead(
                ures_getByIndex(dateTimePatterns.getAlias(), static_cast<int32_t>(dateStyle), nullptr, &status));
        if (U_FAILURE(status)) {
           status = U_INVALID_FORMAT_ERROR;
           return;
        }
        switch (ures_getType(currentBundle.getAlias())) {
            case URES_STRING: {
               resStr = ures_getString(currentBundle.getAlias(), &resStrLen, &status);
               break;
            }
            case URES_ARRAY: {
               resStr = ures_getStringByIndex(currentBundle.getAlias(), 0, &resStrLen, &status);
               ovrStr = ures_getStringByIndex(currentBundle.getAlias(), 1, &ovrStrLen, &status);
               fDateOverride.setTo(true, ovrStr, ovrStrLen);
               break;
            }
            default: {
               status = U_INVALID_FORMAT_ERROR;
               return;
            }
        }

        UnicodeString tempus2(true, resStr, resStrLen);

        // Currently, for compatibility with pre-CLDR-42 data, we default to the "atTime"
        // combining patterns. Depending on guidance in CLDR 42 spec and on DisplayOptions,
        // we may change this.
        LocalUResourceBundlePointer dateAtTimePatterns;
        if (!cTypeIsGregorian) {
            CharString resourcePath("calendar/", status);
            resourcePath.append(cType, status).append("/DateTimePatterns%atTime", status);
            dateAtTimePatterns.adoptInstead(
                ures_getByKeyWithFallback(bundle.getAlias(), resourcePath.data(),
                                          nullptr, &status));
        }
        if (cTypeIsGregorian || status == U_MISSING_RESOURCE_ERROR) {
            status = U_ZERO_ERROR;
            dateAtTimePatterns.adoptInstead(
                ures_getByKeyWithFallback(bundle.getAlias(),
                                          "calendar/gregorian/DateTimePatterns%atTime",
                                          nullptr, &status));
        }
        if (U_SUCCESS(status) && ures_getSize(dateAtTimePatterns.getAlias()) >= 4) {
            resStr = ures_getStringByIndex(dateAtTimePatterns.getAlias(), dateStyle - kDateOffset, &resStrLen, &status);
        } else {
            status = U_ZERO_ERROR;
            int32_t glueIndex = kDateTime;
            int32_t patternsSize = ures_getSize(dateTimePatterns.getAlias());
            if (patternsSize >= (kDateTimeOffset + kShort + 1)) {
                // Get proper date time format
                glueIndex = static_cast<int32_t>(kDateTimeOffset + (dateStyle - kDateOffset));
            }

            resStr = ures_getStringByIndex(dateTimePatterns.getAlias(), glueIndex, &resStrLen, &status);
        }
        SimpleFormatter(UnicodeString(true, resStr, resStrLen), 2, 2, status).
                format(tempus1, tempus2, fPattern, status);
    }
    // if the pattern includes just time data or just date date, load the appropriate
    // pattern string from the resources
    // setTo() - see DateFormatSymbols::assignArray comments
    else if (timeStyle != kNone) {
        fPattern.setTo(timePattern);
        if (fPattern.length() == 0) {
            currentBundle.adoptInstead(
                    ures_getByIndex(dateTimePatterns.getAlias(), static_cast<int32_t>(timeStyle), nullptr, &status));
            if (U_FAILURE(status)) {
               status = U_INVALID_FORMAT_ERROR;
               return;
            }
            switch (ures_getType(currentBundle.getAlias())) {
                case URES_STRING: {
                   resStr = ures_getString(currentBundle.getAlias(), &resStrLen, &status);
                   break;
                }
                case URES_ARRAY: {
                   resStr = ures_getStringByIndex(currentBundle.getAlias(), 0, &resStrLen, &status);
                   ovrStr = ures_getStringByIndex(currentBundle.getAlias(), 1, &ovrStrLen, &status);
                   fDateOverride.setTo(true, ovrStr, ovrStrLen);
                   break;
                }
                default: {
                   status = U_INVALID_FORMAT_ERROR;
                   return;
                }
            }
            fPattern.setTo(true, resStr, resStrLen);
        }
    }
    else if (dateStyle != kNone) {
        currentBundle.adoptInstead(
                ures_getByIndex(dateTimePatterns.getAlias(), static_cast<int32_t>(dateStyle), nullptr, &status));
        if (U_FAILURE(status)) {
           status = U_INVALID_FORMAT_ERROR;
           return;
        }
        switch (ures_getType(currentBundle.getAlias())) {
            case URES_STRING: {
               resStr = ures_getString(currentBundle.getAlias(), &resStrLen, &status);
               break;
            }
            case URES_ARRAY: {
               resStr = ures_getStringByIndex(currentBundle.getAlias(), 0, &resStrLen, &status);
               ovrStr = ures_getStringByIndex(currentBundle.getAlias(), 1, &ovrStrLen, &status);
               fDateOverride.setTo(true, ovrStr, ovrStrLen);
               break;
            }
            default: {
               status = U_INVALID_FORMAT_ERROR;
               return;
            }
        }
        fPattern.setTo(true, resStr, resStrLen);
    }

    // and if it includes _neither_, that's an error
    else
        status = U_INVALID_FORMAT_ERROR;

    // finally, finish initializing by creating a Calendar and a NumberFormat
    initialize(locale, status);
}

//----------------------------------------------------------------------

Calendar*
SimpleDateFormat::initializeCalendar(TimeZone* adoptZone, const Locale& locale, UErrorCode& status)
{
    if(!U_FAILURE(status)) {
        fCalendar = Calendar::createInstance(
            adoptZone ? adoptZone : TimeZone::forLocaleOrDefault(locale), locale, status);
    }
    return fCalendar;
}

void
SimpleDateFormat::initialize(const Locale& locale,
                             UErrorCode& status)
{
    if (U_FAILURE(status)) return;

    parsePattern(); // Need this before initNumberFormatters(), to set fHasHanYearChar

    // Simple-minded hack to force Gannen year numbering for ja@calendar=japanese
    // if format is non-numeric (includes 年) and fDateOverride is not already specified.
    // Now this does get updated if applyPattern subsequently changes the pattern type.
    if (fDateOverride.isBogus() && fHasHanYearChar &&
            fCalendar != nullptr && uprv_strcmp(fCalendar->getType(),"japanese") == 0 &&
            uprv_strcmp(fLocale.getLanguage(),"ja") == 0) {
        fDateOverride.setTo(u"y=jpanyear", -1);
    }

    // We don't need to check that the row count is >= 1, since all 2d arrays have at
    // least one row
    fNumberFormat = NumberFormat::createInstance(locale, status);
    if (fNumberFormat != nullptr && U_SUCCESS(status))
    {
        fixNumberFormatForDates(*fNumberFormat);
        //fNumberFormat->setLenient(true); // Java uses a custom DateNumberFormat to format/parse

        initNumberFormatters(locale, status);
        initSimpleNumberFormatter(status);

    }
    else if (U_SUCCESS(status))
    {
        status = U_MISSING_RESOURCE_ERROR;
    }
}

/* Initialize the fields we use to disambiguate ambiguous years. Separate
 * so we can call it from readObject().
 */
void SimpleDateFormat::initializeDefaultCentury()
{
  if(fCalendar) {
    fHaveDefaultCentury = fCalendar->haveDefaultCentury();
    if(fHaveDefaultCentury) {
      fDefaultCenturyStart = fCalendar->defaultCenturyStart();
      fDefaultCenturyStartYear = fCalendar->defaultCenturyStartYear();
    } else {
      fDefaultCenturyStart = DBL_MIN;
      fDefaultCenturyStartYear = -1;
    }
  }
}

/*
 * Initialize the boolean attributes. Separate so we can call it from all constructors.
 */
void SimpleDateFormat::initializeBooleanAttributes()
{
    UErrorCode status = U_ZERO_ERROR;

    setBooleanAttribute(UDAT_PARSE_ALLOW_WHITESPACE, true, status);
    setBooleanAttribute(UDAT_PARSE_ALLOW_NUMERIC, true, status);
    setBooleanAttribute(UDAT_PARSE_PARTIAL_LITERAL_MATCH, true, status);
    setBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, true, status);
}

/* Define one-century window into which to disambiguate dates using
 * two-digit years. Make public in JDK 1.2.
 */
void SimpleDateFormat::parseAmbiguousDatesAsAfter(UDate startDate, UErrorCode& status)
{
    if(U_FAILURE(status)) {
        return;
    }
    if(!fCalendar) {
      status = U_ILLEGAL_ARGUMENT_ERROR;
      return;
    }

    fCalendar->setTime(startDate, status);
    if(U_SUCCESS(status)) {
        fHaveDefaultCentury = true;
        fDefaultCenturyStart = startDate;
        fDefaultCenturyStartYear = fCalendar->get(UCAL_YEAR, status);
    }
}

//----------------------------------------------------------------------

UnicodeString&
SimpleDateFormat::format(Calendar& cal, UnicodeString& appendTo, FieldPosition& pos) const
{
  UErrorCode status = U_ZERO_ERROR;
  FieldPositionOnlyHandler handler(pos);
  return _format(cal, appendTo, handler, status);
}

//----------------------------------------------------------------------

UnicodeString&
SimpleDateFormat::format(Calendar& cal, UnicodeString& appendTo,
                         FieldPositionIterator* posIter, UErrorCode& status) const
{
  FieldPositionIteratorHandler handler(posIter, status);
  return _format(cal, appendTo, handler, status);
}

//----------------------------------------------------------------------

UnicodeString&
SimpleDateFormat::_format(Calendar& cal, UnicodeString& appendTo,
                            FieldPositionHandler& handler, UErrorCode& status) const
{
    if ( U_FAILURE(status) ) {
       return appendTo;
    }
    Calendar* workCal = &cal;
    Calendar* calClone = nullptr;
    if (&cal != fCalendar && uprv_strcmp(cal.getType(), fCalendar->getType()) != 0) {
        // Different calendar type
        // We use the time and time zone from the input calendar, but
        // do not use the input calendar for field calculation.
        calClone = fCalendar->clone();
        if (calClone != nullptr) {
            UDate t = cal.getTime(status);
            calClone->setTime(t, status);
            calClone->setTimeZone(cal.getTimeZone());
            workCal = calClone;
        } else {
            status = U_MEMORY_ALLOCATION_ERROR;
            return appendTo;
        }
    }

    UBool inQuote = false;
    char16_t prevCh = 0;
    int32_t count = 0;
    int32_t fieldNum = 0;
    UDisplayContext capitalizationContext = getContext(UDISPCTX_TYPE_CAPITALIZATION, status);

    // loop through the pattern string character by character
    int32_t patternLength = fPattern.length();
    for (int32_t i = 0; i < patternLength && U_SUCCESS(status); ++i) {
        char16_t ch = fPattern[i];

        // Use subFormat() to format a repeated pattern character
        // when a different pattern or non-pattern character is seen
        if (ch != prevCh && count > 0) {
            subFormat(appendTo, prevCh, count, capitalizationContext, fieldNum++,
                      prevCh, handler, *workCal, status);
            count = 0;
        }
        if (ch == QUOTE) {
            // Consecutive single quotes are a single quote literal,
            // either outside of quotes or between quotes
            if ((i+1) < patternLength && fPattern[i+1] == QUOTE) {
                appendTo += QUOTE;
                ++i;
            } else {
                inQuote = ! inQuote;
            }
        }
        else if (!inQuote && isSyntaxChar(ch)) {
            // ch is a date-time pattern character to be interpreted
            // by subFormat(); count the number of times it is repeated
            prevCh = ch;
            ++count;
        }
        else {
            // Append quoted characters and unquoted non-pattern characters
            appendTo += ch;
        }
    }

    // Format the last item in the pattern, if any
    if (count > 0) {
        subFormat(appendTo, prevCh, count, capitalizationContext, fieldNum++,
                  prevCh, handler, *workCal, status);
    }

    delete calClone;

    return appendTo;
}

//----------------------------------------------------------------------

/* Map calendar field into calendar field level.
 * the larger the level, the smaller the field unit.
 * For example, UCAL_ERA level is 0, UCAL_YEAR level is 10,
 * UCAL_MONTH level is 20.
 * NOTE: if new fields adds in, the table needs to update.
 */
const int32_t
SimpleDateFormat::fgCalendarFieldToLevel[] =
{
    /*GyM*/ 0, 10, 20,
    /*wW*/ 20, 30,
    /*dDEF*/ 30, 20, 30, 30,
    /*ahHm*/ 40, 50, 50, 60,
    /*sS*/ 70, 80,
    /*z?Y*/ 0, 0, 10,
    /*eug*/ 30, 10, 0,
    /*A?.*/ 40, 0, 0
};

int32_t SimpleDateFormat::getLevelFromChar(char16_t ch) {
    // Map date field LETTER into calendar field level.
    // the larger the level, the smaller the field unit.
    // NOTE: if new fields adds in, the table needs to update.
    static const int32_t mapCharToLevel[] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        //
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        //       !   "   #   $   %   &   '   (   )   *   +   ,   -   .   /
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
#if UDAT_HAS_PATTERN_CHAR_FOR_TIME_SEPARATOR
        //   0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ?
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  0, -1, -1, -1, -1, -1,
#else
        //   0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ?
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
#endif
        //   @   A   B   C   D   E   F   G   H   I   J   K   L   M   N   O
            -1, 40, -1, -1, 20, 30, 30,  0, 50, -1, -1, 50, 20, 20, -1,  0,
        //   P   Q   R   S   T   U   V   W   X   Y   Z   [   \   ]   ^   _
            -1, 20, -1, 80, -1, 10,  0, 30,  0, 10,  0, -1, -1, -1, -1, -1,
        //   `   a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
            -1, 40, -1, 30, 30, 30, -1,  0, 50, -1, -1, 50,  0, 60, -1, -1,
        //   p   q   r   s   t   u   v   w   x   y   z   {   |   }   ~
            -1, 20, 10, 70, -1, 10,  0, 20,  0, 10,  0, -1, -1, -1, -1, -1
    };

    return ch < UPRV_LENGTHOF(mapCharToLevel) ? mapCharToLevel[ch] : -1;
}

UBool SimpleDateFormat::isSyntaxChar(char16_t ch) {
    static const UBool mapCharToIsSyntax[] = {
        //
        false, false, false, false, false, false, false, false,
        //
        false, false, false, false, false, false, false, false,
        //
        false, false, false, false, false, false, false, false,
        //
        false, false, false, false, false, false, false, false,
        //         !      "      #      $      %      &      '
        false, false, false, false, false, false, false, false,
        //  (      )      *      +      ,      -      .      /
        false, false, false, false, false, false, false, false,
        //  0      1      2      3      4      5      6      7
        false, false, false, false, false, false, false, false,
#if UDAT_HAS_PATTERN_CHAR_FOR_TIME_SEPARATOR
        //  8      9      :      ;      <      =      >      ?
        false, false,  true, false, false, false, false, false,
#else
        //  8      9      :      ;      <      =      >      ?
        false, false, false, false, false, false, false, false,
#endif
        //  @      A      B      C      D      E      F      G
        false,  true,  true,  true,  true,  true,  true,  true,
        //  H      I      J      K      L      M      N      O
         true,  true,  true,  true,  true,  true,  true,  true,
        //  P      Q      R      S      T      U      V      W
         true,  true,  true,  true,  true,  true,  true,  true,
        //  X      Y      Z      [      \      ]      ^      _
         true,  true,  true, false, false, false, false, false,
        //  `      a      b      c      d      e      f      g
        false,  true,  true,  true,  true,  true,  true,  true,
        //  h      i      j      k      l      m      n      o
         true,  true,  true,  true,  true,  true,  true,  true,
        //  p      q      r      s      t      u      v      w
         true,  true,  true,  true,  true,  true,  true,  true,
        //  x      y      z      {      |      }      ~
         true,  true,  true, false, false, false, false, false
    };

    return ch < UPRV_LENGTHOF(mapCharToIsSyntax) ? mapCharToIsSyntax[ch] : false;
}

// Map index into pattern character string to Calendar field number.
const UCalendarDateFields
SimpleDateFormat::fgPatternIndexToCalendarField[] =
{
    /*GyM*/ UCAL_ERA, UCAL_YEAR, UCAL_MONTH,
    /*dkH*/ UCAL_DATE, UCAL_HOUR_OF_DAY, UCAL_HOUR_OF_DAY,
    /*msS*/ UCAL_MINUTE, UCAL_SECOND, UCAL_MILLISECOND,
    /*EDF*/ UCAL_DAY_OF_WEEK, UCAL_DAY_OF_YEAR, UCAL_DAY_OF_WEEK_IN_MONTH,
    /*wWa*/ UCAL_WEEK_OF_YEAR, UCAL_WEEK_OF_MONTH, UCAL_AM_PM,
    /*hKz*/ UCAL_HOUR, UCAL_HOUR, UCAL_ZONE_OFFSET,
    /*Yeu*/ UCAL_YEAR_WOY, UCAL_DOW_LOCAL, UCAL_EXTENDED_YEAR,
    /*gAZ*/ UCAL_JULIAN_DAY, UCAL_MILLISECONDS_IN_DAY, UCAL_ZONE_OFFSET,
    /*v*/   UCAL_ZONE_OFFSET,
    /*c*/   UCAL_DOW_LOCAL,
    /*L*/   UCAL_MONTH,
    /*Q*/   UCAL_MONTH,
    /*q*/   UCAL_MONTH,
    /*V*/   UCAL_ZONE_OFFSET,
    /*U*/   UCAL_YEAR,
    /*O*/   UCAL_ZONE_OFFSET,
    /*Xx*/  UCAL_ZONE_OFFSET, UCAL_ZONE_OFFSET,
    /*r*/   UCAL_EXTENDED_YEAR,
    /*bB*/   UCAL_FIELD_COUNT, UCAL_FIELD_COUNT,  // no mappings to calendar fields
#if UDAT_HAS_PATTERN_CHAR_FOR_TIME_SEPARATOR
    /*:*/   UCAL_FIELD_COUNT, /* => no useful mapping to any calendar field */
#else
    /*no pattern char for UDAT_TIME_SEPARATOR_FIELD*/   UCAL_FIELD_COUNT, /* => no useful mapping to any calendar field */
#endif
};

// Map index into pattern character string to DateFormat field number
const UDateFormatField
SimpleDateFormat::fgPatternIndexToDateFormatField[] = {
    /*GyM*/ UDAT_ERA_FIELD, UDAT_YEAR_FIELD, UDAT_MONTH_FIELD,
    /*dkH*/ UDAT_DATE_FIELD, UDAT_HOUR_OF_DAY1_FIELD, UDAT_HOUR_OF_DAY0_FIELD,
    /*msS*/ UDAT_MINUTE_FIELD, UDAT_SECOND_FIELD, UDAT_FRACTIONAL_SECOND_FIELD,
    /*EDF*/ UDAT_DAY_OF_WEEK_FIELD, UDAT_DAY_OF_YEAR_FIELD, UDAT_DAY_OF_WEEK_IN_MONTH_FIELD,
    /*wWa*/ UDAT_WEEK_OF_YEAR_FIELD, UDAT_WEEK_OF_MONTH_FIELD, UDAT_AM_PM_FIELD,
    /*hKz*/ UDAT_HOUR1_FIELD, UDAT_HOUR0_FIELD, UDAT_TIMEZONE_FIELD,
    /*Yeu*/ UDAT_YEAR_WOY_FIELD, UDAT_DOW_LOCAL_FIELD, UDAT_EXTENDED_YEAR_FIELD,
    /*gAZ*/ UDAT_JULIAN_DAY_FIELD, UDAT_MILLISECONDS_IN_DAY_FIELD, UDAT_TIMEZONE_RFC_FIELD,
    /*v*/   UDAT_TIMEZONE_GENERIC_FIELD,
    /*c*/   UDAT_STANDALONE_DAY_FIELD,
    /*L*/   UDAT_STANDALONE_MONTH_FIELD,
    /*Q*/   UDAT_QUARTER_FIELD,
    /*q*/   UDAT_STANDALONE_QUARTER_FIELD,
    /*V*/   UDAT_TIMEZONE_SPECIAL_FIELD,
    /*U*/   UDAT_YEAR_NAME_FIELD,
    /*O*/   UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD,
    /*Xx*/  UDAT_TIMEZONE_ISO_FIELD, UDAT_TIMEZONE_ISO_LOCAL_FIELD,
    /*r*/   UDAT_RELATED_YEAR_FIELD,
    /*bB*/  UDAT_AM_PM_MIDNIGHT_NOON_FIELD, UDAT_FLEXIBLE_DAY_PERIOD_FIELD,
#if UDAT_HAS_PATTERN_CHAR_FOR_TIME_SEPARATOR
    /*:*/   UDAT_TIME_SEPARATOR_FIELD,
#else
    /*no pattern char for UDAT_TIME_SEPARATOR_FIELD*/   UDAT_TIME_SEPARATOR_FIELD,
#endif
};

//----------------------------------------------------------------------

/**
 * Append symbols[value] to dst.  Make sure the array index is not out
 * of bounds.
 */
static inline void
_appendSymbol(UnicodeString& dst,
              int32_t value,
              const UnicodeString* symbols,
              int32_t symbolsCount) {
    U_ASSERT(0 <= value && value < symbolsCount);
    if (0 <= value && value < symbolsCount) {
        dst += symbols[value];
    }
}

static inline void
_appendSymbolWithMonthPattern(UnicodeString& dst, int32_t value, const UnicodeString* symbols, int32_t symbolsCount,
              const UnicodeString* monthPattern, UErrorCode& status) {
    U_ASSERT(0 <= value && value < symbolsCount);
    if (0 <= value && value < symbolsCount) {
        if (monthPattern == nullptr) {
            dst += symbols[value];
        } else {
            SimpleFormatter(*monthPattern, 1, 1, status).format(symbols[value], dst, status);
        }
    }
}

//----------------------------------------------------------------------

void
SimpleDateFormat::initSimpleNumberFormatter(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    const auto* df = dynamic_cast<const DecimalFormat*>(fNumberFormat);
    if (df == nullptr) {
        return;
    }
    const DecimalFormatSymbols* syms = df->getDecimalFormatSymbols();
    if (syms == nullptr) {
        return;
    }
    fSimpleNumberFormatter = new number::SimpleNumberFormatter(
        number::SimpleNumberFormatter::forLocaleAndSymbolsAndGroupingStrategy(
            fLocale, *syms, UNUM_GROUPING_OFF, status
        )
    );
    if (fSimpleNumberFormatter == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
}

void
SimpleDateFormat::initNumberFormatters(const Locale &locale,UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    if ( fDateOverride.isBogus() && fTimeOverride.isBogus() ) {
        return;
    }
    umtx_lock(&LOCK);
    if (fSharedNumberFormatters == nullptr) {
        fSharedNumberFormatters = allocSharedNumberFormatters();
        if (fSharedNumberFormatters == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
    }
    umtx_unlock(&LOCK);

    if (U_FAILURE(status)) {
        return;
    }

    processOverrideString(locale,fDateOverride,kOvrStrDate,status);
    processOverrideString(locale,fTimeOverride,kOvrStrTime,status);
}

void
SimpleDateFormat::processOverrideString(const Locale &locale, const UnicodeString &str, int8_t type, UErrorCode &status) {
    if (str.isBogus() || U_FAILURE(status)) {
        return;
    }

    int32_t start = 0;
    int32_t len;
    UnicodeString nsName;
    UnicodeString ovrField;
    UBool moreToProcess = true;
    NSOverride *overrideList = nullptr;

    while (moreToProcess) {
        int32_t delimiterPosition = str.indexOf(static_cast<char16_t>(ULOC_KEYWORD_ITEM_SEPARATOR_UNICODE), start);
        if (delimiterPosition == -1) {
            moreToProcess = false;
            len = str.length() - start;
        } else {
            len = delimiterPosition - start;
        }
        UnicodeString currentString(str,start,len);
        int32_t equalSignPosition = currentString.indexOf(static_cast<char16_t>(ULOC_KEYWORD_ASSIGN_UNICODE), 0);
        if (equalSignPosition == -1) { // Simple override string such as "hebrew"
            nsName.setTo(currentString);
            ovrField.setToBogus();
        } else { // Field specific override string such as "y=hebrew"
            nsName.setTo(currentString,equalSignPosition+1);
            ovrField.setTo(currentString,0,1); // We just need the first character.
        }

        int32_t nsNameHash = nsName.hashCode();
        // See if the numbering system is in the override list, if not, then add it.
        NSOverride *curr = overrideList;
        const SharedNumberFormat *snf = nullptr;
        UBool found = false;
        while ( curr && !found ) {
            if ( curr->hash == nsNameHash ) {
                snf = curr->snf;
                found = true;
            }
            curr = curr->next;
        }

        if (!found) {
           LocalPointer<NSOverride> cur(new NSOverride);
           if (!cur.isNull()) {
               char kw[ULOC_KEYWORD_AND_VALUES_CAPACITY];
               uprv_strcpy(kw,"numbers=");
               nsName.extract(0,len,kw+8,ULOC_KEYWORD_AND_VALUES_CAPACITY-8,US_INV);

               Locale ovrLoc(locale.getLanguage(),locale.getCountry(),locale.getVariant(),kw);
               cur->hash = nsNameHash;
               cur->next = overrideList;
               SharedObject::copyPtr(
                       createSharedNumberFormat(ovrLoc, status), cur->snf);
               if (U_FAILURE(status)) {
                   if (overrideList) {
                       overrideList->free();
                   }
                   return;
               }
               snf = cur->snf;
               overrideList = cur.orphan();
           } else {
               status = U_MEMORY_ALLOCATION_ERROR;
               if (overrideList) {
                   overrideList->free();
               }
               return;
           }
        }

        // Now that we have an appropriate number formatter, fill in the appropriate spaces in the
        // number formatters table.
        if (ovrField.isBogus()) {
            switch (type) {
                case kOvrStrDate:
                case kOvrStrBoth: {
                    for ( int8_t i=0 ; i<kDateFieldsCount; i++ ) {
                        SharedObject::copyPtr(snf, fSharedNumberFormatters[kDateFields[i]]);
                    }
                    if (type==kOvrStrDate) {
                        break;
                    }
                    U_FALLTHROUGH;
                }
                case kOvrStrTime : {
                    for ( int8_t i=0 ; i<kTimeFieldsCount; i++ ) {
                        SharedObject::copyPtr(snf, fSharedNumberFormatters[kTimeFields[i]]);
                    }
                    break;
                }
            }
        } else {
           // if the pattern character is unrecognized, signal an error and bail out
           UDateFormatField patternCharIndex =
              DateFormatSymbols::getPatternCharIndex(ovrField.charAt(0));
           if (patternCharIndex == UDAT_FIELD_COUNT) {
               status = U_INVALID_FORMAT_ERROR;
               if (overrideList) {
                   overrideList->free();
               }
               return;
           }
           SharedObject::copyPtr(snf, fSharedNumberFormatters[patternCharIndex]);
        }

        start = delimiterPosition + 1;
    }
    if (overrideList) {
        overrideList->free();
    }
}

//---------------------------------------------------------------------
void
SimpleDateFormat::subFormat(UnicodeString &appendTo,
                            char16_t ch,
                            int32_t count,
                            UDisplayContext capitalizationContext,
                            int32_t fieldNum,
                            char16_t fieldToOutput,
                            FieldPositionHandler& handler,
                            Calendar& cal,
                            UErrorCode& status) const
{
    static const int32_t maxIntCount = 10;
    static const UnicodeString hebr(u"hebr");

    if (U_FAILURE(status)) {
        return;
    }

    // this function gets called by format() to produce the appropriate substitution
    // text for an individual pattern symbol (e.g., "HH" or "yyyy")

    UDateFormatField patternCharIndex = DateFormatSymbols::getPatternCharIndex(ch);
    int32_t beginOffset = appendTo.length();
    DateFormatSymbols::ECapitalizationContextUsageType capContextUsageType = DateFormatSymbols::kCapContextUsageOther;

    // if the pattern character is unrecognized, signal an error and dump out
    if (patternCharIndex == UDAT_FIELD_COUNT)
    {
        if (ch != 0x6C) { // pattern char 'l' (SMALL LETTER L) just gets ignored
            status = U_INVALID_FORMAT_ERROR;
        }
        return;
    }

    UCalendarDateFields field = fgPatternIndexToCalendarField[patternCharIndex];
    int32_t value = 0;
    // Don't get value unless it is useful
    if (field < UCAL_FIELD_COUNT) {
        value = (patternCharIndex != UDAT_RELATED_YEAR_FIELD)? cal.get(field, status): cal.getRelatedYear(status);
        if (U_FAILURE(status)) {
            return;
        }
    }

    const NumberFormat *currentNumberFormat = getNumberFormatByIndex(patternCharIndex);
    if (currentNumberFormat == nullptr) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return;
    }

    switch (patternCharIndex) {

    // for any "G" symbol, write out the appropriate era string
    // "GGGG" is wide era name, "GGGGG" is narrow era name, anything else is abbreviated name
    case UDAT_ERA_FIELD:
        {
            const auto* calType = cal.getType();
            if (uprv_strcmp(calType,"chinese") == 0 || uprv_strcmp(calType,"dangi") == 0) {
                zeroPaddingNumber(currentNumberFormat,appendTo, value, 1, 9); // as in ICU4J
            } else {
                if (count == 5) {
                    _appendSymbol(appendTo, value, fSymbols->fNarrowEras, fSymbols->fNarrowErasCount);
                    capContextUsageType = DateFormatSymbols::kCapContextUsageEraNarrow;
                } else if (count == 4) {
                    _appendSymbol(appendTo, value, fSymbols->fEraNames, fSymbols->fEraNamesCount);
                    capContextUsageType = DateFormatSymbols::kCapContextUsageEraWide;
                } else {
                    _appendSymbol(appendTo, value, fSymbols->fEras, fSymbols->fErasCount);
                    capContextUsageType = DateFormatSymbols::kCapContextUsageEraAbbrev;
                }
            }
        }
        break;

     case UDAT_YEAR_NAME_FIELD:
        if (fSymbols->fShortYearNames != nullptr && value <= fSymbols->fShortYearNamesCount) {
            // the Calendar YEAR field runs 1 through 60 for cyclic years
            _appendSymbol(appendTo, value - 1, fSymbols->fShortYearNames, fSymbols->fShortYearNamesCount);
            break;
        }
        // else fall through to numeric year handling, do not break here
        U_FALLTHROUGH;

   // OLD: for "yyyy", write out the whole year; for "yy", write out the last 2 digits
    // NEW: UTS#35:
//Year         y     yy     yyy     yyyy     yyyyy
//AD 1         1     01     001     0001     00001
//AD 12       12     12     012     0012     00012
//AD 123     123     23     123     0123     00123
//AD 1234   1234     34    1234     1234     01234
//AD 12345 12345     45   12345    12345     12345
    case UDAT_YEAR_FIELD:
    case UDAT_YEAR_WOY_FIELD:
        if (fDateOverride.compare(hebr)==0 && value>HEBREW_CAL_CUR_MILLENIUM_START_YEAR && value<HEBREW_CAL_CUR_MILLENIUM_END_YEAR) {
            value-=HEBREW_CAL_CUR_MILLENIUM_START_YEAR;
        }
        if(count == 2)
            zeroPaddingNumber(currentNumberFormat, appendTo, value, 2, 2);
        else
            zeroPaddingNumber(currentNumberFormat, appendTo, value, count, maxIntCount);
        break;

    // for "MMMM"/"LLLL", write out the whole month name, for "MMM"/"LLL", write out the month
    // abbreviation, for "M"/"L" or "MM"/"LL", write out the month as a number with the
    // appropriate number of digits
    // for "MMMMM"/"LLLLL", use the narrow form
    case UDAT_MONTH_FIELD:
    case UDAT_STANDALONE_MONTH_FIELD:
        if (uprv_strcmp(cal.getType(),"hebrew") == 0) {
           if (HebrewCalendar::isLeapYear(cal.get(UCAL_YEAR,status)) && value == 6 && count >= 3 )
               value = 13; // Show alternate form for Adar II in leap years in Hebrew calendar.
           if (!HebrewCalendar::isLeapYear(cal.get(UCAL_YEAR,status)) && value >= 6 && count < 3 )
               value--; // Adjust the month number down 1 in Hebrew non-leap years, i.e. Adar is 6, not 7.
        }
        {
            int32_t isLeapMonth = (fSymbols->fLeapMonthPatterns != nullptr && fSymbols->fLeapMonthPatternsCount >= DateFormatSymbols::kMonthPatternsCount)?
                        cal.get(UCAL_IS_LEAP_MONTH, status): 0;
            // should consolidate the next section by using arrays of pointers & counts for the right symbols...
            if (count == 5) {
                if (patternCharIndex == UDAT_MONTH_FIELD) {
                    _appendSymbolWithMonthPattern(appendTo, value, fSymbols->fNarrowMonths, fSymbols->fNarrowMonthsCount,
                            (isLeapMonth!=0)? &(fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternFormatNarrow]): nullptr, status);
                } else {
                    _appendSymbolWithMonthPattern(appendTo, value, fSymbols->fStandaloneNarrowMonths, fSymbols->fStandaloneNarrowMonthsCount,
                            (isLeapMonth!=0)? &(fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternStandaloneNarrow]): nullptr, status);
                }
                capContextUsageType = DateFormatSymbols::kCapContextUsageMonthNarrow;
            } else if (count == 4) {
                if (patternCharIndex == UDAT_MONTH_FIELD) {
                    _appendSymbolWithMonthPattern(appendTo, value, fSymbols->fMonths, fSymbols->fMonthsCount,
                            (isLeapMonth!=0)? &(fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternFormatWide]): nullptr, status);
                    capContextUsageType = DateFormatSymbols::kCapContextUsageMonthFormat;
                } else {
                    _appendSymbolWithMonthPattern(appendTo, value, fSymbols->fStandaloneMonths, fSymbols->fStandaloneMonthsCount,
                            (isLeapMonth!=0)? &(fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternStandaloneWide]): nullptr, status);
                    capContextUsageType = DateFormatSymbols::kCapContextUsageMonthStandalone;
                }
            } else if (count == 3) {
                if (patternCharIndex == UDAT_MONTH_FIELD) {
                    _appendSymbolWithMonthPattern(appendTo, value, fSymbols->fShortMonths, fSymbols->fShortMonthsCount,
                            (isLeapMonth!=0)? &(fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternFormatAbbrev]): nullptr, status);
                    capContextUsageType = DateFormatSymbols::kCapContextUsageMonthFormat;
                } else {
                    _appendSymbolWithMonthPattern(appendTo, value, fSymbols->fStandaloneShortMonths, fSymbols->fStandaloneShortMonthsCount,
                            (isLeapMonth!=0)? &(fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternStandaloneAbbrev]): nullptr, status);
                    capContextUsageType = DateFormatSymbols::kCapContextUsageMonthStandalone;
                }
            } else {
                UnicodeString monthNumber;
                zeroPaddingNumber(currentNumberFormat,monthNumber, value + 1, count, maxIntCount);
                _appendSymbolWithMonthPattern(appendTo, 0, &monthNumber, 1,
                        (isLeapMonth!=0)? &(fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternNumeric]): nullptr, status);
            }
        }
        break;

    // for "k" and "kk", write out the hour, adjusting midnight to appear as "24"
    case UDAT_HOUR_OF_DAY1_FIELD:
        if (value == 0)
            zeroPaddingNumber(currentNumberFormat,appendTo, cal.getMaximum(UCAL_HOUR_OF_DAY) + 1, count, maxIntCount);
        else
            zeroPaddingNumber(currentNumberFormat,appendTo, value, count, maxIntCount);
        break;

    case UDAT_FRACTIONAL_SECOND_FIELD:
        // Fractional seconds left-justify
        {
            int32_t minDigits = (count > 3) ? 3 : count;
            if (count == 1) {
                value /= 100;
            } else if (count == 2) {
                value /= 10;
            }
            zeroPaddingNumber(currentNumberFormat, appendTo, value, minDigits, maxIntCount);
            if (count > 3) {
                zeroPaddingNumber(currentNumberFormat, appendTo, 0, count - 3, maxIntCount);
            }
        }
        break;

    // for "ee" or "e", use local numeric day-of-the-week
    // for "EEEEEE" or "eeeeee", write out the short day-of-the-week name
    // for "EEEEE" or "eeeee", write out the narrow day-of-the-week name
    // for "EEEE" or "eeee", write out the wide day-of-the-week name
    // for "EEE" or "EE" or "E" or "eee", write out the abbreviated day-of-the-week name
    case UDAT_DOW_LOCAL_FIELD:
        if ( count < 3 ) {
            zeroPaddingNumber(currentNumberFormat,appendTo, value, count, maxIntCount);
            break;
        }
        // fall through to EEEEE-EEE handling, but for that we don't want local day-of-week,
        // we want standard day-of-week, so first fix value to work for EEEEE-EEE.
        value = cal.get(UCAL_DAY_OF_WEEK, status);
        if (U_FAILURE(status)) {
            return;
        }
        // fall through, do not break here
        U_FALLTHROUGH;
    case UDAT_DAY_OF_WEEK_FIELD:
        if (count == 5) {
            _appendSymbol(appendTo, value, fSymbols->fNarrowWeekdays,
                          fSymbols->fNarrowWeekdaysCount);
            capContextUsageType = DateFormatSymbols::kCapContextUsageDayNarrow;
        } else if (count == 4) {
            _appendSymbol(appendTo, value, fSymbols->fWeekdays,
                          fSymbols->fWeekdaysCount);
            capContextUsageType = DateFormatSymbols::kCapContextUsageDayFormat;
        } else if (count == 6) {
            _appendSymbol(appendTo, value, fSymbols->fShorterWeekdays,
                          fSymbols->fShorterWeekdaysCount);
            capContextUsageType = DateFormatSymbols::kCapContextUsageDayFormat;
        } else {
            _appendSymbol(appendTo, value, fSymbols->fShortWeekdays,
                          fSymbols->fShortWeekdaysCount);
            capContextUsageType = DateFormatSymbols::kCapContextUsageDayFormat;
        }
        break;

    // for "ccc", write out the abbreviated day-of-the-week name
    // for "cccc", write out the wide day-of-the-week name
    // for "ccccc", use the narrow day-of-the-week name
    // for "ccccc", use the short day-of-the-week name
    case UDAT_STANDALONE_DAY_FIELD:
        if ( count < 3 ) {
            zeroPaddingNumber(currentNumberFormat,appendTo, value, 1, maxIntCount);
            break;
        }
        // fall through to alpha DOW handling, but for that we don't want local day-of-week,
        // we want standard day-of-week, so first fix value.
        value = cal.get(UCAL_DAY_OF_WEEK, status);
        if (U_FAILURE(status)) {
            return;
        }
        if (count == 5) {
            _appendSymbol(appendTo, value, fSymbols->fStandaloneNarrowWeekdays,
                          fSymbols->fStandaloneNarrowWeekdaysCount);
            capContextUsageType = DateFormatSymbols::kCapContextUsageDayNarrow;
        } else if (count == 4) {
            _appendSymbol(appendTo, value, fSymbols->fStandaloneWeekdays,
                          fSymbols->fStandaloneWeekdaysCount);
            capContextUsageType = DateFormatSymbols::kCapContextUsageDayStandalone;
        } else if (count == 6) {
            _appendSymbol(appendTo, value, fSymbols->fStandaloneShorterWeekdays,
                          fSymbols->fStandaloneShorterWeekdaysCount);
            capContextUsageType = DateFormatSymbols::kCapContextUsageDayStandalone;
        } else { // count == 3
            _appendSymbol(appendTo, value, fSymbols->fStandaloneShortWeekdays,
                          fSymbols->fStandaloneShortWeekdaysCount);
            capContextUsageType = DateFormatSymbols::kCapContextUsageDayStandalone;
        }
        break;

    // for "a" symbol, write out the whole AM/PM string
    case UDAT_AM_PM_FIELD:
        if (count < 5) {
            _appendSymbol(appendTo, value, fSymbols->fAmPms,
                          fSymbols->fAmPmsCount);
        } else {
            _appendSymbol(appendTo, value, fSymbols->fNarrowAmPms,
                          fSymbols->fNarrowAmPmsCount);
        }
        break;

    // if we see pattern character for UDAT_TIME_SEPARATOR_FIELD (none currently defined),
    // write out the time separator string. Leave support in for future definition.
    case UDAT_TIME_SEPARATOR_FIELD:
        {
            UnicodeString separator;
            appendTo += fSymbols->getTimeSeparatorString(separator);
        }
        break;

    // for "h" and "hh", write out the hour, adjusting noon and midnight to show up
    // as "12"
    case UDAT_HOUR1_FIELD:
        if (value == 0)
            zeroPaddingNumber(currentNumberFormat,appendTo, cal.getLeastMaximum(UCAL_HOUR) + 1, count, maxIntCount);
        else
            zeroPaddingNumber(currentNumberFormat,appendTo, value, count, maxIntCount);
        break;

    case UDAT_TIMEZONE_FIELD: // 'z'
    case UDAT_TIMEZONE_RFC_FIELD: // 'Z'
    case UDAT_TIMEZONE_GENERIC_FIELD: // 'v'
    case UDAT_TIMEZONE_SPECIAL_FIELD: // 'V'
    case UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD: // 'O'
    case UDAT_TIMEZONE_ISO_FIELD: // 'X'
    case UDAT_TIMEZONE_ISO_LOCAL_FIELD: // 'x'
        {
            char16_t zsbuf[ZONE_NAME_U16_MAX];
            UnicodeString zoneString(zsbuf, 0, UPRV_LENGTHOF(zsbuf));
            const TimeZone& tz = cal.getTimeZone();
            UDate date = cal.getTime(status);
            const TimeZoneFormat *tzfmt = tzFormat(status);
            if (U_SUCCESS(status)) {
                switch (patternCharIndex) {
                case UDAT_TIMEZONE_FIELD:
                    if (count < 4) {
                        // "z", "zz", "zzz"
                        tzfmt->format(UTZFMT_STYLE_SPECIFIC_SHORT, tz, date, zoneString);
                        capContextUsageType = DateFormatSymbols::kCapContextUsageMetazoneShort;
                    } else {
                        // "zzzz" or longer
                        tzfmt->format(UTZFMT_STYLE_SPECIFIC_LONG, tz, date, zoneString);
                        capContextUsageType = DateFormatSymbols::kCapContextUsageMetazoneLong;
                    }
                    break;
                case UDAT_TIMEZONE_RFC_FIELD:
                    if (count < 4) {
                        // "Z"
                        tzfmt->format(UTZFMT_STYLE_ISO_BASIC_LOCAL_FULL, tz, date, zoneString);
                    } else if (count == 5) {
                        // "ZZZZZ"
                        tzfmt->format(UTZFMT_STYLE_ISO_EXTENDED_FULL, tz, date, zoneString);
                    } else {
                        // "ZZ", "ZZZ", "ZZZZ"
                        tzfmt->format(UTZFMT_STYLE_LOCALIZED_GMT, tz, date, zoneString);
                    }
                    break;
                case UDAT_TIMEZONE_GENERIC_FIELD:
                    if (count == 1) {
                        // "v"
                        tzfmt->format(UTZFMT_STYLE_GENERIC_SHORT, tz, date, zoneString);
                        capContextUsageType = DateFormatSymbols::kCapContextUsageMetazoneShort;
                    } else if (count == 4) {
                        // "vvvv"
                        tzfmt->format(UTZFMT_STYLE_GENERIC_LONG, tz, date, zoneString);
                        capContextUsageType = DateFormatSymbols::kCapContextUsageMetazoneLong;
                    }
                    break;
                case UDAT_TIMEZONE_SPECIAL_FIELD:
                    if (count == 1) {
                        // "V"
                        tzfmt->format(UTZFMT_STYLE_ZONE_ID_SHORT, tz, date, zoneString);
                    } else if (count == 2) {
                        // "VV"
                        tzfmt->format(UTZFMT_STYLE_ZONE_ID, tz, date, zoneString);
                    } else if (count == 3) {
                        // "VVV"
                        tzfmt->format(UTZFMT_STYLE_EXEMPLAR_LOCATION, tz, date, zoneString);
                    } else if (count == 4) {
                        // "VVVV"
                        tzfmt->format(UTZFMT_STYLE_GENERIC_LOCATION, tz, date, zoneString);
                        capContextUsageType = DateFormatSymbols::kCapContextUsageZoneLong;
                    }
                    break;
                case UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD:
                    if (count == 1) {
                        // "O"
                        tzfmt->format(UTZFMT_STYLE_LOCALIZED_GMT_SHORT, tz, date, zoneString);
                    } else if (count == 4) {
                        // "OOOO"
                        tzfmt->format(UTZFMT_STYLE_LOCALIZED_GMT, tz, date, zoneString);
                    }
                    break;
                case UDAT_TIMEZONE_ISO_FIELD:
                    if (count == 1) {
                        // "X"
                        tzfmt->format(UTZFMT_STYLE_ISO_BASIC_SHORT, tz, date, zoneString);
                    } else if (count == 2) {
                        // "XX"
                        tzfmt->format(UTZFMT_STYLE_ISO_BASIC_FIXED, tz, date, zoneString);
                    } else if (count == 3) {
                        // "XXX"
                        tzfmt->format(UTZFMT_STYLE_ISO_EXTENDED_FIXED, tz, date, zoneString);
                    } else if (count == 4) {
                        // "XXXX"
                        tzfmt->format(UTZFMT_STYLE_ISO_BASIC_FULL, tz, date, zoneString);
                    } else if (count == 5) {
                        // "XXXXX"
                        tzfmt->format(UTZFMT_STYLE_ISO_EXTENDED_FULL, tz, date, zoneString);
                    }
                    break;
                case UDAT_TIMEZONE_ISO_LOCAL_FIELD:
                    if (count == 1) {
                        // "x"
                        tzfmt->format(UTZFMT_STYLE_ISO_BASIC_LOCAL_SHORT, tz, date, zoneString);
                    } else if (count == 2) {
                        // "xx"
                        tzfmt->format(UTZFMT_STYLE_ISO_BASIC_LOCAL_FIXED, tz, date, zoneString);
                    } else if (count == 3) {
                        // "xxx"
                        tzfmt->format(UTZFMT_STYLE_ISO_EXTENDED_LOCAL_FIXED, tz, date, zoneString);
                    } else if (count == 4) {
                        // "xxxx"
                        tzfmt->format(UTZFMT_STYLE_ISO_BASIC_LOCAL_FULL, tz, date, zoneString);
                    } else if (count == 5) {
                        // "xxxxx"
                        tzfmt->format(UTZFMT_STYLE_ISO_EXTENDED_LOCAL_FULL, tz, date, zoneString);
                    }
                    break;
                default:
                    UPRV_UNREACHABLE_EXIT;
                }
            }
            appendTo += zoneString;
        }
        break;

    case UDAT_QUARTER_FIELD:
        if (count >= 5)
            _appendSymbol(appendTo, value/3, fSymbols->fNarrowQuarters,
                          fSymbols->fNarrowQuartersCount);
         else if (count == 4)
            _appendSymbol(appendTo, value/3, fSymbols->fQuarters,
                          fSymbols->fQuartersCount);
        else if (count == 3)
            _appendSymbol(appendTo, value/3, fSymbols->fShortQuarters,
                          fSymbols->fShortQuartersCount);
        else
            zeroPaddingNumber(currentNumberFormat,appendTo, (value/3) + 1, count, maxIntCount);
        break;

    case UDAT_STANDALONE_QUARTER_FIELD:
        if (count >= 5)
            _appendSymbol(appendTo, value/3, fSymbols->fStandaloneNarrowQuarters,
                          fSymbols->fStandaloneNarrowQuartersCount);
        else if (count == 4)
            _appendSymbol(appendTo, value/3, fSymbols->fStandaloneQuarters,
                          fSymbols->fStandaloneQuartersCount);
        else if (count == 3)
            _appendSymbol(appendTo, value/3, fSymbols->fStandaloneShortQuarters,
                          fSymbols->fStandaloneShortQuartersCount);
        else
            zeroPaddingNumber(currentNumberFormat,appendTo, (value/3) + 1, count, maxIntCount);
        break;

    case UDAT_AM_PM_MIDNIGHT_NOON_FIELD:
    {
        const UnicodeString *toAppend = nullptr;
        int32_t hour = cal.get(UCAL_HOUR_OF_DAY, status);

        // Note: "midnight" can be ambiguous as to whether it refers to beginning of day or end of day.
        // For ICU 57 output of "midnight" is temporarily suppressed.

        // For "midnight" and "noon":
        // Time, as displayed, must be exactly noon or midnight.
        // This means minutes and seconds, if present, must be zero.
        if ((/*hour == 0 ||*/ hour == 12) &&
                (!fHasMinute || cal.get(UCAL_MINUTE, status) == 0) &&
                (!fHasSecond || cal.get(UCAL_SECOND, status) == 0)) {
            // Stealing am/pm value to use as our array index.
            // It works out: am/midnight are both 0, pm/noon are both 1,
            // 12 am is 12 midnight, and 12 pm is 12 noon.
            int32_t val = cal.get(UCAL_AM_PM, status);

            if (count <= 3) {
                toAppend = &fSymbols->fAbbreviatedDayPeriods[val];
            } else if (count == 4 || count > 5) {
                toAppend = &fSymbols->fWideDayPeriods[val];
            } else { // count == 5
                toAppend = &fSymbols->fNarrowDayPeriods[val];
            }
        }

        // toAppend is nullptr if time isn't exactly midnight or noon (as displayed).
        // toAppend is bogus if time is midnight or noon, but no localized string exists.
        // In either case, fall back to am/pm.
        if (toAppend == nullptr || toAppend->isBogus()) {
            // Reformat with identical arguments except ch, now changed to 'a'.
            // We are passing a different fieldToOutput because we want to add
            // 'b' to field position. This makes this fallback stable when
            // there is a data change on locales.
            subFormat(appendTo, u'a', count, capitalizationContext, fieldNum, u'b', handler, cal, status);
            return;
        } else {
            appendTo += *toAppend;
        }

        break;
    }

    case UDAT_FLEXIBLE_DAY_PERIOD_FIELD:
    {
        // TODO: Maybe fetch the DayperiodRules during initialization (instead of at the first
        // loading of an instance) if a relevant pattern character (b or B) is used.
        const DayPeriodRules *ruleSet = DayPeriodRules::getInstance(this->getSmpFmtLocale(), status);
        if (U_FAILURE(status)) {
            // Data doesn't conform to spec, therefore loading failed.
            break;
        }
        if (ruleSet == nullptr) {
            // Data doesn't exist for the locale we're looking for.
            // Falling back to am/pm.
            // We are passing a different fieldToOutput because we want to add
            // 'B' to field position. This makes this fallback stable when
            // there is a data change on locales.
            subFormat(appendTo, u'a', count, capitalizationContext, fieldNum, u'B', handler, cal, status);
            return;
        }

        // Get current display time.
        int32_t hour = cal.get(UCAL_HOUR_OF_DAY, status);
        int32_t minute = 0;
        if (fHasMinute) {
            minute = cal.get(UCAL_MINUTE, status);
        }
        int32_t second = 0;
        if (fHasSecond) {
            second = cal.get(UCAL_SECOND, status);
        }

        // Determine day period.
        DayPeriodRules::DayPeriod periodType;
        if (hour == 0 && minute == 0 && second == 0 && ruleSet->hasMidnight()) {
            periodType = DayPeriodRules::DAYPERIOD_MIDNIGHT;
        } else if (hour == 12 && minute == 0 && second == 0 && ruleSet->hasNoon()) {
            periodType = DayPeriodRules::DAYPERIOD_NOON;
        } else {
            periodType = ruleSet->getDayPeriodForHour(hour);
        }

        // Rule set exists, therefore periodType can't be UNKNOWN.
        // Get localized string.
        U_ASSERT(periodType != DayPeriodRules::DAYPERIOD_UNKNOWN);
        UnicodeString *toAppend = nullptr;
        int32_t index;

        // Note: "midnight" can be ambiguous as to whether it refers to beginning of day or end of day.
        // For ICU 57 output of "midnight" is temporarily suppressed.

        if (periodType != DayPeriodRules::DAYPERIOD_AM &&
                periodType != DayPeriodRules::DAYPERIOD_PM &&
                periodType != DayPeriodRules::DAYPERIOD_MIDNIGHT) {
            index = static_cast<int32_t>(periodType);
            if (count <= 3) {
                toAppend = &fSymbols->fAbbreviatedDayPeriods[index];  // i.e. short
            } else if (count == 4 || count > 5) {
                toAppend = &fSymbols->fWideDayPeriods[index];
            } else {  // count == 5
                toAppend = &fSymbols->fNarrowDayPeriods[index];
            }
        }

        // Fallback schedule:
        // Midnight/Noon -> General Periods -> AM/PM.

        // Midnight/Noon -> General Periods.
        if ((toAppend == nullptr || toAppend->isBogus()) &&
                (periodType == DayPeriodRules::DAYPERIOD_MIDNIGHT ||
                 periodType == DayPeriodRules::DAYPERIOD_NOON)) {
            periodType = ruleSet->getDayPeriodForHour(hour);
            index = static_cast<int32_t>(periodType);

            if (count <= 3) {
                toAppend = &fSymbols->fAbbreviatedDayPeriods[index];  // i.e. short
            } else if (count == 4 || count > 5) {
                toAppend = &fSymbols->fWideDayPeriods[index];
            } else {  // count == 5
                toAppend = &fSymbols->fNarrowDayPeriods[index];
            }
        }

        // General Periods -> AM/PM.
        if (periodType == DayPeriodRules::DAYPERIOD_AM ||
            periodType == DayPeriodRules::DAYPERIOD_PM ||
            toAppend->isBogus()) {
            // We are passing a different fieldToOutput because we want to add
            // 'B' to field position iterator. This makes this fallback stable when
            // there is a data change on locales.
            subFormat(appendTo, u'a', count, capitalizationContext, fieldNum, u'B', handler, cal, status);
            return;
        }
        else {
            appendTo += *toAppend;
        }

        break;
    }

    // all of the other pattern symbols can be formatted as simple numbers with
    // appropriate zero padding
    default:
        zeroPaddingNumber(currentNumberFormat,appendTo, value, count, maxIntCount);
        break;
    }
#if !UCONFIG_NO_BREAK_ITERATION
    // if first field, check to see whether we need to and are able to titlecase it
    if (fieldNum == 0 && fCapitalizationBrkIter != nullptr && appendTo.length() > beginOffset &&
            u_islower(appendTo.char32At(beginOffset))) {
        UBool titlecase = false;
        switch (capitalizationContext) {
            case UDISPCTX_CAPITALIZATION_FOR_BEGINNING_OF_SENTENCE:
                titlecase = true;
                break;
            case UDISPCTX_CAPITALIZATION_FOR_UI_LIST_OR_MENU:
                titlecase = fSymbols->fCapitalization[capContextUsageType][0];
                break;
            case UDISPCTX_CAPITALIZATION_FOR_STANDALONE:
                titlecase = fSymbols->fCapitalization[capContextUsageType][1];
                break;
            default:
                // titlecase = false;
                break;
        }
        if (titlecase) {
            BreakIterator* const mutableCapitalizationBrkIter = fCapitalizationBrkIter->clone();
            UnicodeString firstField(appendTo, beginOffset);
            firstField.toTitle(mutableCapitalizationBrkIter, fLocale, U_TITLECASE_NO_LOWERCASE | U_TITLECASE_NO_BREAK_ADJUSTMENT);
            appendTo.replaceBetween(beginOffset, appendTo.length(), firstField);
            delete mutableCapitalizationBrkIter;
        }
    }
#endif

    handler.addAttribute(DateFormatSymbols::getPatternCharIndex(fieldToOutput), beginOffset, appendTo.length());
}

//----------------------------------------------------------------------

void SimpleDateFormat::adoptNumberFormat(NumberFormat *formatToAdopt) {
    // Null out the fast formatter, it references fNumberFormat which we're
    // about to invalidate
    delete fSimpleNumberFormatter;
    fSimpleNumberFormatter = nullptr;

    fixNumberFormatForDates(*formatToAdopt);
    delete fNumberFormat;
    fNumberFormat = formatToAdopt;

    // We successfully set the default number format. Now delete the overrides
    // (can't fail).
    if (fSharedNumberFormatters) {
        freeSharedNumberFormatters(fSharedNumberFormatters);
        fSharedNumberFormatters = nullptr;
    }

    // Recompute fSimpleNumberFormatter if necessary
    UErrorCode localStatus = U_ZERO_ERROR;
    initSimpleNumberFormatter(localStatus);
}

void SimpleDateFormat::adoptNumberFormat(const UnicodeString& fields, NumberFormat *formatToAdopt, UErrorCode &status){
    fixNumberFormatForDates(*formatToAdopt);
    LocalPointer<NumberFormat> fmt(formatToAdopt);
    if (U_FAILURE(status)) {
        return;
    }

    // We must ensure fSharedNumberFormatters is allocated.
    if (fSharedNumberFormatters == nullptr) {
        fSharedNumberFormatters = allocSharedNumberFormatters();
        if (fSharedNumberFormatters == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }
    const SharedNumberFormat *newFormat = createSharedNumberFormat(fmt.orphan());
    if (newFormat == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    for (int i=0; i<fields.length(); i++) {
        char16_t field = fields.charAt(i);
        // if the pattern character is unrecognized, signal an error and bail out
        UDateFormatField patternCharIndex = DateFormatSymbols::getPatternCharIndex(field);
        if (patternCharIndex == UDAT_FIELD_COUNT) {
            status = U_INVALID_FORMAT_ERROR;
            newFormat->deleteIfZeroRefCount();
            return;
        }

        // Set the number formatter in the table
        SharedObject::copyPtr(
                newFormat, fSharedNumberFormatters[patternCharIndex]);
    }
    newFormat->deleteIfZeroRefCount();
}

const NumberFormat *
SimpleDateFormat::getNumberFormatForField(char16_t field) const {
    UDateFormatField index = DateFormatSymbols::getPatternCharIndex(field);
    if (index == UDAT_FIELD_COUNT) {
        return nullptr;
    }
    return getNumberFormatByIndex(index);
}

//----------------------------------------------------------------------
void
SimpleDateFormat::zeroPaddingNumber(
        const NumberFormat *currentNumberFormat,
        UnicodeString &appendTo,
        int32_t value, int32_t minDigits, int32_t maxDigits) const
{

    if (currentNumberFormat == fNumberFormat && fSimpleNumberFormatter) {
        // Can use fast path
        // We create UFormattedNumberData ourselves to avoid a heap allocation
        // and corresponding free. Set the pointer to null afterwards to prevent
        // the implementation from attempting to free it.
        UErrorCode localStatus = U_ZERO_ERROR;
        number::impl::UFormattedNumberData data;
        data.quantity.setToLong(value);
        number::SimpleNumber number(&data, localStatus);
        number.setMinimumIntegerDigits(minDigits, localStatus);
        number.setMaximumIntegerDigits(maxDigits, localStatus);

        number::FormattedNumber result = fSimpleNumberFormatter->format(std::move(number), localStatus);
        if (U_FAILURE(localStatus)) {
            result.fData = nullptr;
            return;
        }
        UnicodeStringAppendable appendable(appendTo);
        result.appendTo(appendable, localStatus);
        result.fData = nullptr;
        return;
    }

    // Check for RBNF (no clone necessary)
    const auto* rbnf = dynamic_cast<const RuleBasedNumberFormat*>(currentNumberFormat);
    if (rbnf != nullptr) {
        FieldPosition pos(FieldPosition::DONT_CARE);
        rbnf->format(value, appendTo, pos);  // 3rd arg is there to speed up processing
        return;
    }

    // Fall back to slow path (clone and mutate the NumberFormat)
    if (currentNumberFormat != nullptr) {
        FieldPosition pos(FieldPosition::DONT_CARE);
        LocalPointer<NumberFormat> nf(currentNumberFormat->clone());
        nf->setMinimumIntegerDigits(minDigits);
        nf->setMaximumIntegerDigits(maxDigits);
        nf->format(value, appendTo, pos);  // 3rd arg is there to speed up processing
    }
}

//----------------------------------------------------------------------

/**
 * Return true if the given format character, occurring count
 * times, represents a numeric field.
 */
UBool SimpleDateFormat::isNumeric(char16_t formatChar, int32_t count) {
    return DateFormatSymbols::isNumericPatternChar(formatChar, count);
}

UBool
SimpleDateFormat::isAtNumericField(const UnicodeString &pattern, int32_t patternOffset) {
    if (patternOffset >= pattern.length()) {
        // not at any field
        return false;
    }
    char16_t ch = pattern.charAt(patternOffset);
    UDateFormatField f = DateFormatSymbols::getPatternCharIndex(ch);
    if (f == UDAT_FIELD_COUNT) {
        // not at any field
        return false;
    }
    int32_t i = patternOffset;
    while (pattern.charAt(++i) == ch) {}
    return DateFormatSymbols::isNumericField(f, i - patternOffset);
}

UBool
SimpleDateFormat::isAfterNonNumericField(const UnicodeString &pattern, int32_t patternOffset) {
    if (patternOffset <= 0) {
        // not after any field
        return false;
    }
    char16_t ch = pattern.charAt(--patternOffset);
    UDateFormatField f = DateFormatSymbols::getPatternCharIndex(ch);
    if (f == UDAT_FIELD_COUNT) {
        // not after any field
        return false;
    }
    int32_t i = patternOffset;
    while (pattern.charAt(--i) == ch) {}
    return !DateFormatSymbols::isNumericField(f, patternOffset - i);
}

void
SimpleDateFormat::parse(const UnicodeString& text, Calendar& cal, ParsePosition& parsePos) const
{
    UErrorCode status = U_ZERO_ERROR;
    int32_t pos = parsePos.getIndex();
    if(parsePos.getIndex() < 0) {
        parsePos.setErrorIndex(0);
        return;
    }
    int32_t start = pos;

    // Hold the day period until everything else is parsed, because we need
    // the hour to interpret time correctly.
    int32_t dayPeriodInt = -1;

    UBool ambiguousYear[] = { false };
    int32_t saveHebrewMonth = -1;
    int32_t count = 0;
    UTimeZoneFormatTimeType tzTimeType = UTZFMT_TIME_TYPE_UNKNOWN;

    // For parsing abutting numeric fields. 'abutPat' is the
    // offset into 'pattern' of the first of 2 or more abutting
    // numeric fields.  'abutStart' is the offset into 'text'
    // where parsing the fields begins. 'abutPass' starts off as 0
    // and increments each time we try to parse the fields.
    int32_t abutPat = -1; // If >=0, we are in a run of abutting numeric fields
    int32_t abutStart = 0;
    int32_t abutPass = 0;
    UBool inQuote = false;

    MessageFormat * numericLeapMonthFormatter = nullptr;

    Calendar* calClone = nullptr;
    Calendar *workCal = &cal;
    if (&cal != fCalendar && uprv_strcmp(cal.getType(), fCalendar->getType()) != 0) {
        // Different calendar type
        // We use the time/zone from the input calendar, but
        // do not use the input calendar for field calculation.
        calClone = fCalendar->clone();
        if (calClone != nullptr) {
            calClone->setTime(cal.getTime(status),status);
            if (U_FAILURE(status)) {
                goto ExitParse;
            }
            calClone->setTimeZone(cal.getTimeZone());
            workCal = calClone;
        } else {
            status = U_MEMORY_ALLOCATION_ERROR;
            goto ExitParse;
        }
    }

    if (fSymbols->fLeapMonthPatterns != nullptr && fSymbols->fLeapMonthPatternsCount >= DateFormatSymbols::kMonthPatternsCount) {
        numericLeapMonthFormatter = new MessageFormat(fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternNumeric], fLocale, status);
        if (numericLeapMonthFormatter == nullptr) {
             status = U_MEMORY_ALLOCATION_ERROR;
             goto ExitParse;
        } else if (U_FAILURE(status)) {
             goto ExitParse; // this will delete numericLeapMonthFormatter
        }
    }

    for (int32_t i=0; i<fPattern.length(); ++i) {
        char16_t ch = fPattern.charAt(i);

        // Handle alphabetic field characters.
        if (!inQuote && isSyntaxChar(ch)) {
            int32_t fieldPat = i;

            // Count the length of this field specifier
            count = 1;
            while ((i+1)<fPattern.length() &&
                   fPattern.charAt(i+1) == ch) {
                ++count;
                ++i;
            }

            if (isNumeric(ch, count)) {
                if (abutPat < 0) {
                    // Determine if there is an abutting numeric field.
                    // Record the start of a set of abutting numeric fields.
                    if (isAtNumericField(fPattern, i + 1)) {
                        abutPat = fieldPat;
                        abutStart = pos;
                        abutPass = 0;
                    }
                }
            } else {
                abutPat = -1; // End of any abutting fields
            }

            // Handle fields within a run of abutting numeric fields.  Take
            // the pattern "HHmmss" as an example. We will try to parse
            // 2/2/2 characters of the input text, then if that fails,
            // 1/2/2.  We only adjust the width of the leftmost field; the
            // others remain fixed.  This allows "123456" => 12:34:56, but
            // "12345" => 1:23:45.  Likewise, for the pattern "yyyyMMdd" we
            // try 4/2/2, 3/2/2, 2/2/2, and finally 1/2/2.
            if (abutPat >= 0) {
                // If we are at the start of a run of abutting fields, then
                // shorten this field in each pass.  If we can't shorten
                // this field any more, then the parse of this set of
                // abutting numeric fields has failed.
                if (fieldPat == abutPat) {
                    count -= abutPass++;
                    if (count == 0) {
                        status = U_PARSE_ERROR;
                        goto ExitParse;
                    }
                }

                pos = subParse(text, pos, ch, count,
                               true, false, ambiguousYear, saveHebrewMonth, *workCal, i, numericLeapMonthFormatter, &tzTimeType);

                // If the parse fails anywhere in the run, back up to the
                // start of the run and retry.
                if (pos < 0) {
                    i = abutPat - 1;
                    pos = abutStart;
                    continue;
                }
            }

            // Handle non-numeric fields and non-abutting numeric
            // fields.
            else if (ch != 0x6C) { // pattern char 'l' (SMALL LETTER L) just gets ignored
                int32_t s = subParse(text, pos, ch, count,
                               false, true, ambiguousYear, saveHebrewMonth, *workCal, i, numericLeapMonthFormatter, &tzTimeType, &dayPeriodInt);

                if (s == -pos-1) {
                    // era not present, in special cases allow this to continue
                    // from the position where the era was expected
                    s = pos;

                    if (i+1 < fPattern.length()) {
                        // move to next pattern character
                        char16_t c = fPattern.charAt(i+1);

                        // check for whitespace
                        if (PatternProps::isWhiteSpace(c)) {
                            i++;
                            // Advance over run in pattern
                            while ((i+1)<fPattern.length() &&
                                   PatternProps::isWhiteSpace(fPattern.charAt(i+1))) {
                                ++i;
                            }
                        }
                    }
                }
                else if (s <= 0) {
                    status = U_PARSE_ERROR;
                    goto ExitParse;
                }
                pos = s;
            }
        }

        // Handle literal pattern characters.  These are any
        // quoted characters and non-alphabetic unquoted
        // characters.
        else {

            abutPat = -1; // End of any abutting fields

            if (! matchLiterals(fPattern, i, text, pos, getBooleanAttribute(UDAT_PARSE_ALLOW_WHITESPACE, status), getBooleanAttribute(UDAT_PARSE_PARTIAL_LITERAL_MATCH, status), isLenient())) {
                status = U_PARSE_ERROR;
                goto ExitParse;
            }
        }
    }

    // Special hack for trailing "." after non-numeric field.
    if (text.charAt(pos) == 0x2e && getBooleanAttribute(UDAT_PARSE_ALLOW_WHITESPACE, status)) {
        // only do if the last field is not numeric
        if (isAfterNonNumericField(fPattern, fPattern.length())) {
            pos++; // skip the extra "."
        }
    }

    // If dayPeriod is set, use it in conjunction with hour-of-day to determine am/pm.
    if (dayPeriodInt >= 0) {
        DayPeriodRules::DayPeriod dayPeriod = static_cast<DayPeriodRules::DayPeriod>(dayPeriodInt);
        const DayPeriodRules *ruleSet = DayPeriodRules::getInstance(this->getSmpFmtLocale(), status);

        if (!cal.isSet(UCAL_HOUR) && !cal.isSet(UCAL_HOUR_OF_DAY)) {
            // If hour is not set, set time to the midpoint of current day period, overwriting
            // minutes if it's set.
            double midPoint = ruleSet->getMidPointForDayPeriod(dayPeriod, status);

            // If we can't get midPoint we do nothing.
            if (U_SUCCESS(status)) {
                // Truncate midPoint toward zero to get the hour.
                // Any leftover means it was a half-hour.
                int32_t midPointHour = static_cast<int32_t>(midPoint);
                int32_t midPointMinute = (midPoint - midPointHour) > 0 ? 30 : 0;

                // No need to set am/pm because hour-of-day is set last therefore takes precedence.
                cal.set(UCAL_HOUR_OF_DAY, midPointHour);
                cal.set(UCAL_MINUTE, midPointMinute);
            }
        } else {
            int hourOfDay;

            if (cal.isSet(UCAL_HOUR_OF_DAY)) {  // Hour is parsed in 24-hour format.
                hourOfDay = cal.get(UCAL_HOUR_OF_DAY, status);
            } else {  // Hour is parsed in 12-hour format.
                hourOfDay = cal.get(UCAL_HOUR, status);
                // cal.get() turns 12 to 0 for 12-hour time; change 0 to 12
                // so 0 unambiguously means a 24-hour time from above.
                if (hourOfDay == 0) { hourOfDay = 12; }
            }
            U_ASSERT(0 <= hourOfDay && hourOfDay <= 23);


            // If hour-of-day is 0 or 13 thru 23 then input time in unambiguously in 24-hour format.
            if (hourOfDay == 0 || (13 <= hourOfDay && hourOfDay <= 23)) {
                // Make hour-of-day take precedence over (hour + am/pm) by setting it again.
                cal.set(UCAL_HOUR_OF_DAY, hourOfDay);
            } else {
                // We have a 12-hour time and need to choose between am and pm.
                // Behave as if dayPeriod spanned 6 hours each way from its center point.
                // This will parse correctly for consistent time + period (e.g. 10 at night) as
                // well as provide a reasonable recovery for inconsistent time + period (e.g.
                // 9 in the afternoon).

                // Assume current time is in the AM.
                // - Change 12 back to 0 for easier handling of 12am.
                // - Append minutes as fractional hours because e.g. 8:15 and 8:45 could be parsed
                // into different half-days if center of dayPeriod is at 14:30.
                // - cal.get(MINUTE) will return 0 if MINUTE is unset, which works.
                if (hourOfDay == 12) { hourOfDay = 0; }
                double currentHour = hourOfDay + (cal.get(UCAL_MINUTE, status)) / 60.0;
                double midPointHour = ruleSet->getMidPointForDayPeriod(dayPeriod, status);

                if (U_SUCCESS(status)) {
                    double hoursAheadMidPoint = currentHour - midPointHour;

                    // Assume current time is in the AM.
                    if (-6 <= hoursAheadMidPoint && hoursAheadMidPoint < 6) {
                        // Assumption holds; set time as such.
                        cal.set(UCAL_AM_PM, 0);
                    } else {
                        cal.set(UCAL_AM_PM, 1);
                    }
                }
            }
        }
    }

    // At this point the fields of Calendar have been set.  Calendar
    // will fill in default values for missing fields when the time
    // is computed.

    parsePos.setIndex(pos);

    // This part is a problem:  When we call parsedDate.after, we compute the time.
    // Take the date April 3 2004 at 2:30 am.  When this is first set up, the year
    // will be wrong if we're parsing a 2-digit year pattern.  It will be 1904.
    // April 3 1904 is a Sunday (unlike 2004) so it is the DST onset day.  2:30 am
    // is therefore an "impossible" time, since the time goes from 1:59 to 3:00 am
    // on that day.  It is therefore parsed out to fields as 3:30 am.  Then we
    // add 100 years, and get April 3 2004 at 3:30 am.  Note that April 3 2004 is
    // a Saturday, so it can have a 2:30 am -- and it should. [LIU]
    /*
        UDate parsedDate = calendar.getTime();
        if( ambiguousYear[0] && !parsedDate.after(fDefaultCenturyStart) ) {
            calendar.add(Calendar.YEAR, 100);
            parsedDate = calendar.getTime();
        }
    */
    // Because of the above condition, save off the fields in case we need to readjust.
    // The procedure we use here is not particularly efficient, but there is no other
    // way to do this given the API restrictions present in Calendar.  We minimize
    // inefficiency by only performing this computation when it might apply, that is,
    // when the two-digit year is equal to the start year, and thus might fall at the
    // front or the back of the default century.  This only works because we adjust
    // the year correctly to start with in other cases -- see subParse().
    if (ambiguousYear[0] || tzTimeType != UTZFMT_TIME_TYPE_UNKNOWN) // If this is true then the two-digit year == the default start year
    {
        // We need a copy of the fields, and we need to avoid triggering a call to
        // complete(), which will recalculate the fields.  Since we can't access
        // the fields[] array in Calendar, we clone the entire object.  This will
        // stop working if Calendar.clone() is ever rewritten to call complete().
        Calendar *copy;
        if (ambiguousYear[0]) {
            copy = cal.clone();
            // Check for failed cloning.
            if (copy == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                goto ExitParse;
            }
            UDate parsedDate = copy->getTime(status);
            // {sfb} check internalGetDefaultCenturyStart
            if (fHaveDefaultCentury && (parsedDate < fDefaultCenturyStart)) {
                // We can't use add here because that does a complete() first.
                cal.set(UCAL_YEAR, fDefaultCenturyStartYear + 100);
            }
            delete copy;
        }

        if (tzTimeType != UTZFMT_TIME_TYPE_UNKNOWN) {
            copy = cal.clone();
            // Check for failed cloning.
            if (copy == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                goto ExitParse;
            }
            const TimeZone & tz = cal.getTimeZone();
            BasicTimeZone *btz = nullptr;

            if (dynamic_cast<const OlsonTimeZone *>(&tz) != nullptr
                || dynamic_cast<const SimpleTimeZone *>(&tz) != nullptr
                || dynamic_cast<const RuleBasedTimeZone *>(&tz) != nullptr
                || dynamic_cast<const VTimeZone *>(&tz) != nullptr) {
                btz = (BasicTimeZone*)&tz;
            }

            // Get local millis
            copy->set(UCAL_ZONE_OFFSET, 0);
            copy->set(UCAL_DST_OFFSET, 0);
            UDate localMillis = copy->getTime(status);

            // Make sure parsed time zone type (Standard or Daylight)
            // matches the rule used by the parsed time zone.
            int32_t raw, dst;
            if (btz != nullptr) {
                if (tzTimeType == UTZFMT_TIME_TYPE_STANDARD) {
                    btz->getOffsetFromLocal(localMillis,
                        UCAL_TZ_LOCAL_STANDARD_FORMER, UCAL_TZ_LOCAL_STANDARD_LATTER, raw, dst, status);
                } else {
                    btz->getOffsetFromLocal(localMillis,
                        UCAL_TZ_LOCAL_DAYLIGHT_FORMER, UCAL_TZ_LOCAL_DAYLIGHT_LATTER, raw, dst, status);
                }
            } else {
                // No good way to resolve ambiguous time at transition,
                // but following code work in most case.
                tz.getOffset(localMillis, true, raw, dst, status);
            }

            // Now, compare the results with parsed type, either standard or daylight saving time
            int32_t resolvedSavings = dst;
            if (tzTimeType == UTZFMT_TIME_TYPE_STANDARD) {
                if (dst != 0) {
                    // Override DST_OFFSET = 0 in the result calendar
                    resolvedSavings = 0;
                }
            } else { // tztype == TZTYPE_DST
                if (dst == 0) {
                    if (btz != nullptr) {
                        // This implementation resolves daylight saving time offset
                        // closest rule after the given time.
                        UDate baseTime = localMillis + raw;
                        UDate time = baseTime;
                        UDate limit = baseTime + MAX_DAYLIGHT_DETECTION_RANGE;
                        TimeZoneTransition trs;
                        UBool trsAvail;

                        // Search for DST rule after the given time
                        while (time < limit) {
                            trsAvail = btz->getNextTransition(time, false, trs);
                            if (!trsAvail) {
                                break;
                            }
                            resolvedSavings = trs.getTo()->getDSTSavings();
                            if (resolvedSavings != 0) {
                                break;
                            }
                            time = trs.getTime();
                        }

                        if (resolvedSavings == 0) {
                            // If no DST rule after the given time was found, search for
                            // DST rule before.
                            time = baseTime;
                            limit = baseTime - MAX_DAYLIGHT_DETECTION_RANGE;
                            while (time > limit) {
                                trsAvail = btz->getPreviousTransition(time, true, trs);
                                if (!trsAvail) {
                                    break;
                                }
                                resolvedSavings = trs.getFrom()->getDSTSavings();
                                if (resolvedSavings != 0) {
                                    break;
                                }
                                time = trs.getTime() - 1;
                            }

                            if (resolvedSavings == 0) {
                                resolvedSavings = btz->getDSTSavings();
                            }
                        }
                    } else {
                        resolvedSavings = tz.getDSTSavings();
                    }
                    if (resolvedSavings == 0) {
                        // final fallback
                        resolvedSavings = U_MILLIS_PER_HOUR;
                    }
                }
            }
            cal.set(UCAL_ZONE_OFFSET, raw);
            cal.set(UCAL_DST_OFFSET, resolvedSavings);
            delete copy;
        }
    }
ExitParse:
    // Set the parsed result if local calendar is used
    // instead of the input calendar
    if (U_SUCCESS(status) && workCal != &cal) {
        cal.setTimeZone(workCal->getTimeZone());
        cal.setTime(workCal->getTime(status), status);
    }

    delete numericLeapMonthFormatter;
    delete calClone;

    // If any Calendar calls failed, we pretend that we
    // couldn't parse the string, when in reality this isn't quite accurate--
    // we did parse it; the Calendar calls just failed.
    if (U_FAILURE(status)) {
        parsePos.setErrorIndex(pos);
        parsePos.setIndex(start);
    }
}

//----------------------------------------------------------------------

static int32_t
matchStringWithOptionalDot(const UnicodeString &text,
                            int32_t index,
                            const UnicodeString &data);

int32_t SimpleDateFormat::matchQuarterString(const UnicodeString& text,
                              int32_t start,
                              UCalendarDateFields field,
                              const UnicodeString* data,
                              int32_t dataCount,
                              Calendar& cal) const
{
    int32_t i = 0;
    int32_t count = dataCount;

    // There may be multiple strings in the data[] array which begin with
    // the same prefix (e.g., Cerven and Cervenec (June and July) in Czech).
    // We keep track of the longest match, and return that.  Note that this
    // unfortunately requires us to test all array elements.
    int32_t bestMatchLength = 0, bestMatch = -1;
    UnicodeString bestMatchName;

    for (; i < count; ++i) {
        int32_t matchLength = 0;
        if ((matchLength = matchStringWithOptionalDot(text, start, data[i])) > bestMatchLength) {
            bestMatchLength = matchLength;
            bestMatch = i;
        }
    }

    if (bestMatch >= 0) {
        cal.set(field, bestMatch * 3);
        return start + bestMatchLength;
    }

    return -start;
}

int32_t SimpleDateFormat::matchDayPeriodStrings(const UnicodeString& text, int32_t start,
                              const UnicodeString* data, int32_t dataCount,
                              int32_t &dayPeriod) const
{

    int32_t bestMatchLength = 0, bestMatch = -1;

    for (int32_t i = 0; i < dataCount; ++i) {
        int32_t matchLength = 0;
        if ((matchLength = matchStringWithOptionalDot(text, start, data[i])) > bestMatchLength) {
            bestMatchLength = matchLength;
            bestMatch = i;
        }
    }

    if (bestMatch >= 0) {
        dayPeriod = bestMatch;
        return start + bestMatchLength;
    }

    return -start;
}

//----------------------------------------------------------------------
UBool SimpleDateFormat::matchLiterals(const UnicodeString &pattern,
                                      int32_t &patternOffset,
                                      const UnicodeString &text,
                                      int32_t &textOffset,
                                      UBool whitespaceLenient,
                                      UBool partialMatchLenient,
                                      UBool oldLeniency)
{
    UBool inQuote = false;
    UnicodeString literal;
    int32_t i = patternOffset;

    // scan pattern looking for contiguous literal characters
    for ( ; i < pattern.length(); i += 1) {
        char16_t ch = pattern.charAt(i);

        if (!inQuote && isSyntaxChar(ch)) {
            break;
        }

        if (ch == QUOTE) {
            // Match a quote literal ('') inside OR outside of quotes
            if ((i + 1) < pattern.length() && pattern.charAt(i + 1) == QUOTE) {
                i += 1;
            } else {
                inQuote = !inQuote;
                continue;
            }
        }

        literal += ch;
    }

    // at this point, literal contains the literal text
    // and i is the index of the next non-literal pattern character.
    int32_t p;
    int32_t t = textOffset;

    if (whitespaceLenient) {
        // trim leading, trailing whitespace from
        // the literal text
        literal.trim();

        // ignore any leading whitespace in the text
        while (t < text.length() && u_isWhitespace(text.charAt(t))) {
            t += 1;
        }
    }

    for (p = 0; p < literal.length() && t < text.length();) {
        UBool needWhitespace = false;

        while (p < literal.length() && PatternProps::isWhiteSpace(literal.charAt(p))) {
            needWhitespace = true;
            p += 1;
        }

        if (needWhitespace) {
            int32_t tStart = t;

            while (t < text.length()) {
                char16_t tch = text.charAt(t);

                if (!u_isUWhiteSpace(tch) && !PatternProps::isWhiteSpace(tch)) {
                    break;
                }

                t += 1;
            }

            // TODO: should we require internal spaces
            // in lenient mode? (There won't be any
            // leading or trailing spaces)
            if (!whitespaceLenient && t == tStart) {
                // didn't find matching whitespace:
                // an error in strict mode
                return false;
            }

            // In strict mode, this run of whitespace
            // may have been at the end.
            if (p >= literal.length()) {
                break;
            }
        }
        if (t >= text.length() || literal.charAt(p) != text.charAt(t)) {
            // Ran out of text, or found a non-matching character:
            // OK in lenient mode, an error in strict mode.
            if (whitespaceLenient) {
                if (t == textOffset && text.charAt(t) == 0x2e &&
                        isAfterNonNumericField(pattern, patternOffset)) {
                    // Lenient mode and the literal input text begins with a "." and
                    // we are after a non-numeric field: We skip the "."
                    ++t;
                    continue;  // Do not update p.
                }
                // if it is actual whitespace and we're whitespace lenient it's OK

                char16_t wsc = text.charAt(t);
                if(PatternProps::isWhiteSpace(wsc)) {
                    // Lenient mode and it's just whitespace we skip it
                    ++t;
                    continue;  // Do not update p.
                }
            }
            // hack around oldleniency being a bit of a catch-all bucket and we're just adding support specifically for partial matches
            if(partialMatchLenient && oldLeniency) {
                break;
            }

            return false;
        }
        ++p;
        ++t;
    }

    // At this point if we're in strict mode we have a complete match.
    // If we're in lenient mode we may have a partial match, or no
    // match at all.
    if (p <= 0) {
        // no match. Pretend it matched a run of whitespace
        // and ignorables in the text.
        const  UnicodeSet *ignorables = nullptr;
        UDateFormatField patternCharIndex = DateFormatSymbols::getPatternCharIndex(pattern.charAt(i));
        if (patternCharIndex != UDAT_FIELD_COUNT) {
            ignorables = SimpleDateFormatStaticSets::getIgnorables(patternCharIndex);
        }

        for (t = textOffset; t < text.length(); t += 1) {
            char16_t ch = text.charAt(t);

            if (ignorables == nullptr || !ignorables->contains(ch)) {
                break;
            }
        }
    }

    // if we get here, we've got a complete match.
    patternOffset = i - 1;
    textOffset = t;

    return true;
}

//----------------------------------------------------------------------
// check both wide and abbrev months.
// Does not currently handle monthPattern.
// UCalendarDateFields field = UCAL_MONTH

int32_t SimpleDateFormat::matchAlphaMonthStrings(const UnicodeString& text,
                              int32_t start,
                              const UnicodeString* wideData,
                              const UnicodeString* shortData,
                              int32_t dataCount,
                              Calendar& cal) const
{
    int32_t i;
    int32_t bestMatchLength = 0, bestMatch = -1;

    for (i = 0; i < dataCount; ++i) {
        int32_t matchLen = 0;
        if ((matchLen = matchStringWithOptionalDot(text, start, wideData[i])) > bestMatchLength) {
            bestMatch = i;
            bestMatchLength = matchLen;
        }
    }
    for (i = 0; i < dataCount; ++i) {
        int32_t matchLen = 0;
        if ((matchLen = matchStringWithOptionalDot(text, start, shortData[i])) > bestMatchLength) {
            bestMatch = i;
            bestMatchLength = matchLen;
        }
    }

    if (bestMatch >= 0) { 
        // Adjustment for Hebrew Calendar month Adar II
        if (!strcmp(cal.getType(),"hebrew") && bestMatch==13) {
            cal.set(UCAL_MONTH,6);
        } else {
            cal.set(UCAL_MONTH, bestMatch);
        }
        return start + bestMatchLength;
    }

    return -start;
}

//----------------------------------------------------------------------

int32_t SimpleDateFormat::matchString(const UnicodeString& text,
                              int32_t start,
                              UCalendarDateFields field,
                              const UnicodeString* data,
                              int32_t dataCount,
                              const UnicodeString* monthPattern,
                              Calendar& cal) const
{
    int32_t i = 0;
    int32_t count = dataCount;

    if (field == UCAL_DAY_OF_WEEK) i = 1;

    // There may be multiple strings in the data[] array which begin with
    // the same prefix (e.g., Cerven and Cervenec (June and July) in Czech).
    // We keep track of the longest match, and return that.  Note that this
    // unfortunately requires us to test all array elements.
    // But this does not really work for cases such as Chuvash in which
    // May is "ҫу" and August is "ҫурла"/"ҫур.", hence matchAlphaMonthStrings.
    int32_t bestMatchLength = 0, bestMatch = -1;
    UnicodeString bestMatchName;
    int32_t isLeapMonth = 0;

    for (; i < count; ++i) {
        int32_t matchLen = 0;
        if ((matchLen = matchStringWithOptionalDot(text, start, data[i])) > bestMatchLength) {
            bestMatch = i;
            bestMatchLength = matchLen;
        }

        if (monthPattern != nullptr) {
            UErrorCode status = U_ZERO_ERROR;
            UnicodeString leapMonthName;
            SimpleFormatter(*monthPattern, 1, 1, status).format(data[i], leapMonthName, status);
            if (U_SUCCESS(status)) {
                if ((matchLen = matchStringWithOptionalDot(text, start, leapMonthName)) > bestMatchLength) {
                    bestMatch = i;
                    bestMatchLength = matchLen;
                    isLeapMonth = 1;
                }
            }
        }
    }

    if (bestMatch >= 0) {
        if (field < UCAL_FIELD_COUNT) {
            // Adjustment for Hebrew Calendar month Adar II
            if (!strcmp(cal.getType(),"hebrew") && field==UCAL_MONTH && bestMatch==13) {
                cal.set(field,6);
            } else {
                if (field == UCAL_YEAR) {
                    bestMatch++; // only get here for cyclic year names, which match 1-based years 1-60
                }
                cal.set(field, bestMatch);
            }
            if (monthPattern != nullptr) {
                cal.set(UCAL_IS_LEAP_MONTH, isLeapMonth);
            }
        }

        return start + bestMatchLength;
    }

    return -start;
}

static int32_t
matchStringWithOptionalDot(const UnicodeString &text,
                            int32_t index,
                            const UnicodeString &data) {
    UErrorCode sts = U_ZERO_ERROR;
    int32_t matchLenText = 0;
    int32_t matchLenData = 0;

    u_caseInsensitivePrefixMatch(text.getBuffer() + index, text.length() - index,
                                 data.getBuffer(), data.length(),
                                 0 /* default case option */,
                                 &matchLenText, &matchLenData,
                                 &sts);
    U_ASSERT (U_SUCCESS(sts));

    if (matchLenData == data.length() /* normal match */
        || (data.charAt(data.length() - 1) == 0x2e
            && matchLenData == data.length() - 1 /* match without trailing dot */)) {
        return matchLenText;
    }

    return 0;
}

//----------------------------------------------------------------------

void
SimpleDateFormat::set2DigitYearStart(UDate d, UErrorCode& status)
{
    parseAmbiguousDatesAsAfter(d, status);
}

/**
 * Private member function that converts the parsed date strings into
 * timeFields. Returns -start (for ParsePosition) if failed.
 */
int32_t SimpleDateFormat::subParse(const UnicodeString& text, int32_t& start, char16_t ch, int32_t count,
                           UBool obeyCount, UBool allowNegative, UBool ambiguousYear[], int32_t& saveHebrewMonth, Calendar& cal,
                           int32_t patLoc, MessageFormat * numericLeapMonthFormatter, UTimeZoneFormatTimeType *tzTimeType,
                           int32_t *dayPeriod) const
{
    Formattable number;
    int32_t value = 0;
    int32_t i;
    int32_t ps = 0;
    UErrorCode status = U_ZERO_ERROR;
    ParsePosition pos(0);
    UDateFormatField patternCharIndex = DateFormatSymbols::getPatternCharIndex(ch);
    const NumberFormat *currentNumberFormat;
    UnicodeString temp;
    UBool gotNumber = false;

#if defined (U_DEBUG_CAL)
    //fprintf(stderr, "%s:%d - [%c]  st=%d \n", __FILE__, __LINE__, (char) ch, start);
#endif

    if (patternCharIndex == UDAT_FIELD_COUNT) {
        return -start;
    }

    currentNumberFormat = getNumberFormatByIndex(patternCharIndex);
    if (currentNumberFormat == nullptr) {
        return -start;
    }
    UCalendarDateFields field = fgPatternIndexToCalendarField[patternCharIndex]; // UCAL_FIELD_COUNT if irrelevant
    UnicodeString hebr("hebr", 4, US_INV);

    if (numericLeapMonthFormatter != nullptr) {
        numericLeapMonthFormatter->setFormats(reinterpret_cast<const Format**>(&currentNumberFormat), 1);
    }
    UBool isChineseCalendar = (uprv_strcmp(cal.getType(),"chinese") == 0 || uprv_strcmp(cal.getType(),"dangi") == 0);

    // If there are any spaces here, skip over them.  If we hit the end
    // of the string, then fail.
    for (;;) {
        if (start >= text.length()) {
            return -start;
        }
        UChar32 c = text.char32At(start);
        if (!u_isUWhiteSpace(c) /*||*/ && !PatternProps::isWhiteSpace(c)) {
            break;
        }
        start += U16_LENGTH(c);
    }
    pos.setIndex(start);

    // We handle a few special cases here where we need to parse
    // a number value.  We handle further, more generic cases below.  We need
    // to handle some of them here because some fields require extra processing on
    // the parsed value.
    if (patternCharIndex == UDAT_HOUR_OF_DAY1_FIELD ||                       // k
        patternCharIndex == UDAT_HOUR_OF_DAY0_FIELD ||                       // H
        patternCharIndex == UDAT_HOUR1_FIELD ||                              // h
        patternCharIndex == UDAT_HOUR0_FIELD ||                              // K
        (patternCharIndex == UDAT_DOW_LOCAL_FIELD && count <= 2) ||          // e
        (patternCharIndex == UDAT_STANDALONE_DAY_FIELD && count <= 2) ||     // c
        (patternCharIndex == UDAT_MONTH_FIELD && count <= 2) ||              // M
        (patternCharIndex == UDAT_STANDALONE_MONTH_FIELD && count <= 2) ||   // L
        (patternCharIndex == UDAT_QUARTER_FIELD && count <= 2) ||            // Q
        (patternCharIndex == UDAT_STANDALONE_QUARTER_FIELD && count <= 2) || // q
        patternCharIndex == UDAT_YEAR_FIELD ||                               // y
        patternCharIndex == UDAT_YEAR_WOY_FIELD ||                           // Y
        patternCharIndex == UDAT_YEAR_NAME_FIELD ||                          // U (falls back to numeric)
        (patternCharIndex == UDAT_ERA_FIELD && isChineseCalendar) ||         // G
        patternCharIndex == UDAT_FRACTIONAL_SECOND_FIELD)                    // S
    {
        int32_t parseStart = pos.getIndex();
        // It would be good to unify this with the obeyCount logic below,
        // but that's going to be difficult.
        const UnicodeString* src;

        UBool parsedNumericLeapMonth = false;
        if (numericLeapMonthFormatter != nullptr && (patternCharIndex == UDAT_MONTH_FIELD || patternCharIndex == UDAT_STANDALONE_MONTH_FIELD)) {
            int32_t argCount;
            Formattable * args = numericLeapMonthFormatter->parse(text, pos, argCount);
            if (args != nullptr && argCount == 1 && pos.getIndex() > parseStart && args[0].isNumeric()) {
                parsedNumericLeapMonth = true;
                number.setLong(args[0].getLong());
                cal.set(UCAL_IS_LEAP_MONTH, 1);
                delete[] args;
            } else {
                pos.setIndex(parseStart);
                cal.set(UCAL_IS_LEAP_MONTH, 0);
            }
        }

        if (!parsedNumericLeapMonth) {
            if (obeyCount) {
                if ((start+count) > text.length()) {
                    return -start;
                }

                text.extractBetween(0, start + count, temp);
                src = &temp;
            } else {
                src = &text;
            }

            parseInt(*src, number, pos, allowNegative,currentNumberFormat);
        }

        int32_t txtLoc = pos.getIndex();

        if (txtLoc > parseStart) {
            value = number.getLong();
            gotNumber = true;

            // suffix processing
            if (value < 0 ) {
                txtLoc = checkIntSuffix(text, txtLoc, patLoc+1, true);
                if (txtLoc != pos.getIndex()) {
                    value *= -1;
                }
            }
            else {
                txtLoc = checkIntSuffix(text, txtLoc, patLoc+1, false);
            }

            if (!getBooleanAttribute(UDAT_PARSE_ALLOW_WHITESPACE, status)) {
                // Check the range of the value
                int32_t bias = gFieldRangeBias[patternCharIndex];
                if (bias >= 0 && (value > cal.getMaximum(field) + bias || value < cal.getMinimum(field) + bias)) {
                    return -start;
                }
            }

            pos.setIndex(txtLoc);
        }
    }

    // Make sure that we got a number if
    // we want one, and didn't get one
    // if we don't want one.
    switch (patternCharIndex) {
        case UDAT_HOUR_OF_DAY1_FIELD:
        case UDAT_HOUR_OF_DAY0_FIELD:
        case UDAT_HOUR1_FIELD:
        case UDAT_HOUR0_FIELD:
            // special range check for hours:
            if (value < 0 || value > 24) {
                return -start;
            }

            // fall through to gotNumber check
            U_FALLTHROUGH;
        case UDAT_YEAR_FIELD:
        case UDAT_YEAR_WOY_FIELD:
        case UDAT_FRACTIONAL_SECOND_FIELD:
            // these must be a number
            if (! gotNumber) {
                return -start;
            }

            break;

        default:
            // we check the rest of the fields below.
            break;
    }

    switch (patternCharIndex) {
    case UDAT_ERA_FIELD:
        if (isChineseCalendar) {
            if (!gotNumber) {
                return -start;
            }
            cal.set(UCAL_ERA, value);
            return pos.getIndex();
        }
        if (count == 5) {
            ps = matchString(text, start, UCAL_ERA, fSymbols->fNarrowEras, fSymbols->fNarrowErasCount, nullptr, cal);
        } else if (count == 4) {
            ps = matchString(text, start, UCAL_ERA, fSymbols->fEraNames, fSymbols->fEraNamesCount, nullptr, cal);
        } else {
            ps = matchString(text, start, UCAL_ERA, fSymbols->fEras, fSymbols->fErasCount, nullptr, cal);
        }

        // check return position, if it equals -start, then matchString error
        // special case the return code so we don't necessarily fail out until we
        // verify no year information also
        if (ps == -start)
            ps--;

        return ps;

    case UDAT_YEAR_FIELD:
        // If there are 3 or more YEAR pattern characters, this indicates
        // that the year value is to be treated literally, without any
        // two-digit year adjustments (e.g., from "01" to 2001).  Otherwise
        // we made adjustments to place the 2-digit year in the proper
        // century, for parsed strings from "00" to "99".  Any other string
        // is treated literally:  "2250", "-1", "1", "002".
        if (fDateOverride.compare(hebr)==0 && value < 1000) {
            value += HEBREW_CAL_CUR_MILLENIUM_START_YEAR;
        } else if (text.moveIndex32(start, 2) == pos.getIndex() && !isChineseCalendar
            && u_isdigit(text.char32At(start))
            && u_isdigit(text.char32At(text.moveIndex32(start, 1))))
        {
            // only adjust year for patterns less than 3.
            if(count < 3) {
                // Assume for example that the defaultCenturyStart is 6/18/1903.
                // This means that two-digit years will be forced into the range
                // 6/18/1903 to 6/17/2003.  As a result, years 00, 01, and 02
                // correspond to 2000, 2001, and 2002.  Years 04, 05, etc. correspond
                // to 1904, 1905, etc.  If the year is 03, then it is 2003 if the
                // other fields specify a date before 6/18, or 1903 if they specify a
                // date afterwards.  As a result, 03 is an ambiguous year.  All other
                // two-digit years are unambiguous.
                if(fHaveDefaultCentury) { // check if this formatter even has a pivot year
                    int32_t ambiguousTwoDigitYear = fDefaultCenturyStartYear % 100;
                    ambiguousYear[0] = (value == ambiguousTwoDigitYear);
                    value += (fDefaultCenturyStartYear/100)*100 +
                            (value < ambiguousTwoDigitYear ? 100 : 0);
                }
            }
        }
        cal.set(UCAL_YEAR, value);

        // Delayed checking for adjustment of Hebrew month numbers in non-leap years.
        if (saveHebrewMonth >= 0) {
            HebrewCalendar *hc = (HebrewCalendar*)&cal;
            if (!hc->isLeapYear(value) && saveHebrewMonth >= 6) {
               cal.set(UCAL_MONTH,saveHebrewMonth);
            } else {
               cal.set(UCAL_MONTH,saveHebrewMonth-1);
            }
            saveHebrewMonth = -1;
        }
        return pos.getIndex();

    case UDAT_YEAR_WOY_FIELD:
        // Comment is the same as for UDAT_Year_FIELDs - look above
        if (fDateOverride.compare(hebr)==0 && value < 1000) {
            value += HEBREW_CAL_CUR_MILLENIUM_START_YEAR;
        } else if (text.moveIndex32(start, 2) == pos.getIndex()
            && u_isdigit(text.char32At(start))
            && u_isdigit(text.char32At(text.moveIndex32(start, 1)))
            && fHaveDefaultCentury )
        {
            int32_t ambiguousTwoDigitYear = fDefaultCenturyStartYear % 100;
            ambiguousYear[0] = (value == ambiguousTwoDigitYear);
            value += (fDefaultCenturyStartYear/100)*100 +
                (value < ambiguousTwoDigitYear ? 100 : 0);
        }
        cal.set(UCAL_YEAR_WOY, value);
        return pos.getIndex();

    case UDAT_YEAR_NAME_FIELD:
        if (fSymbols->fShortYearNames != nullptr) {
            int32_t newStart = matchString(text, start, UCAL_YEAR, fSymbols->fShortYearNames, fSymbols->fShortYearNamesCount, nullptr, cal);
            if (newStart > 0) {
                return newStart;
            }
        }
        if (gotNumber && (getBooleanAttribute(UDAT_PARSE_ALLOW_NUMERIC,status) || value > fSymbols->fShortYearNamesCount)) {
            cal.set(UCAL_YEAR, value);
            return pos.getIndex();
        }
        return -start;

    case UDAT_MONTH_FIELD:
    case UDAT_STANDALONE_MONTH_FIELD:
        if (gotNumber) // i.e., M or MM.
        {
            // When parsing month numbers from the Hebrew Calendar, we might need to adjust the month depending on whether
            // or not it was a leap year.  We may or may not yet know what year it is, so might have to delay checking until
            // the year is parsed.
            if (!strcmp(cal.getType(),"hebrew")) {
                HebrewCalendar *hc = (HebrewCalendar*)&cal;
                if (cal.isSet(UCAL_YEAR)) {
                   UErrorCode monthStatus = U_ZERO_ERROR;
                   if (!hc->isLeapYear(hc->get(UCAL_YEAR, monthStatus)) && value >= 6) {
                       cal.set(UCAL_MONTH, value);
                   } else {
                       cal.set(UCAL_MONTH, value - 1);
                   }
                } else {
                    saveHebrewMonth = value;
                }
            } else {
                // Don't want to parse the month if it is a string
                // while pattern uses numeric style: M/MM, L/LL
                // [We computed 'value' above.]
                cal.set(UCAL_MONTH, value - 1);
            }
            return pos.getIndex();
        } else {
            // count >= 3 // i.e., MMM/MMMM, LLL/LLLL
            // Want to be able to parse both short and long forms.
            // Try count == 4 first:
            UnicodeString * wideMonthPat = nullptr;
            UnicodeString * shortMonthPat = nullptr;
            if (fSymbols->fLeapMonthPatterns != nullptr && fSymbols->fLeapMonthPatternsCount >= DateFormatSymbols::kMonthPatternsCount) {
                if (patternCharIndex==UDAT_MONTH_FIELD) {
                    wideMonthPat = &fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternFormatWide];
                    shortMonthPat = &fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternFormatAbbrev];
                } else {
                    wideMonthPat = &fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternStandaloneWide];
                    shortMonthPat = &fSymbols->fLeapMonthPatterns[DateFormatSymbols::kLeapMonthPatternStandaloneAbbrev];
                }
            }
            int32_t newStart = 0;
            if (patternCharIndex==UDAT_MONTH_FIELD) {
                if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) && count>=3 && count <=4 &&
                        fSymbols->fLeapMonthPatterns==nullptr && fSymbols->fMonthsCount==fSymbols->fShortMonthsCount) {
                    // single function to check both wide and short, an experiment
                    newStart = matchAlphaMonthStrings(text, start, fSymbols->fMonths, fSymbols->fShortMonths, fSymbols->fMonthsCount, cal); // try MMMM,MMM
                    if (newStart > 0) {
                        return newStart;
                    }
                }
                if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 4) {
                    newStart = matchString(text, start, UCAL_MONTH, fSymbols->fMonths, fSymbols->fMonthsCount, wideMonthPat, cal); // try MMMM
                    if (newStart > 0) {
                        return newStart;
                    }
                }
                if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 3) {
                    newStart = matchString(text, start, UCAL_MONTH, fSymbols->fShortMonths, fSymbols->fShortMonthsCount, shortMonthPat, cal); // try MMM
                }
            } else {
                if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) && count>=3 && count <=4 &&
                        fSymbols->fLeapMonthPatterns==nullptr && fSymbols->fStandaloneMonthsCount==fSymbols->fStandaloneShortMonthsCount) {
                    // single function to check both wide and short, an experiment
                    newStart = matchAlphaMonthStrings(text, start, fSymbols->fStandaloneMonths, fSymbols->fStandaloneShortMonths, fSymbols->fStandaloneMonthsCount, cal); // try MMMM,MMM
                    if (newStart > 0) {
                        return newStart;
                    }
                }
                if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 4) {
                    newStart = matchString(text, start, UCAL_MONTH, fSymbols->fStandaloneMonths, fSymbols->fStandaloneMonthsCount, wideMonthPat, cal); // try LLLL
                    if (newStart > 0) {
                        return newStart;
                    }
                }
                if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 3) {
                    newStart = matchString(text, start, UCAL_MONTH, fSymbols->fStandaloneShortMonths, fSymbols->fStandaloneShortMonthsCount, shortMonthPat, cal); // try LLL
                }
            }
            if (newStart > 0 || !getBooleanAttribute(UDAT_PARSE_ALLOW_NUMERIC, status))  // currently we do not try to parse MMMMM/LLLLL: #8860
                return newStart;
            // else we allowing parsing as number, below
        }
        break;

    case UDAT_HOUR_OF_DAY1_FIELD:
        // [We computed 'value' above.]
        if (value == cal.getMaximum(UCAL_HOUR_OF_DAY) + 1)
            value = 0;

        // fall through to set field
        U_FALLTHROUGH;
    case UDAT_HOUR_OF_DAY0_FIELD:
        cal.set(UCAL_HOUR_OF_DAY, value);
        return pos.getIndex();

    case UDAT_FRACTIONAL_SECOND_FIELD:
        // Fractional seconds left-justify
        i = countDigits(text, start, pos.getIndex());
        if (i < 3) {
            while (i < 3) {
                value *= 10;
                i++;
            }
        } else {
            int32_t a = 1;
            while (i > 3) {
                a *= 10;
                i--;
            }
            value /= a;
        }
        cal.set(UCAL_MILLISECOND, value);
        return pos.getIndex();

    case UDAT_DOW_LOCAL_FIELD:
        if (gotNumber) // i.e., e or ee
        {
            // [We computed 'value' above.]
            cal.set(UCAL_DOW_LOCAL, value);
            return pos.getIndex();
        }
        // else for eee-eeeee fall through to handling of EEE-EEEEE
        // fall through, do not break here
        U_FALLTHROUGH;
    case UDAT_DAY_OF_WEEK_FIELD:
        {
            // Want to be able to parse both short and long forms.
            // Try count == 4 (EEEE) wide first:
            int32_t newStart = 0;
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 4) {
                if ((newStart = matchString(text, start, UCAL_DAY_OF_WEEK,
                                          fSymbols->fWeekdays, fSymbols->fWeekdaysCount, nullptr, cal)) > 0)
                    return newStart;
            }
            // EEEE wide failed, now try EEE abbreviated
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 3) {
                if ((newStart = matchString(text, start, UCAL_DAY_OF_WEEK,
                                       fSymbols->fShortWeekdays, fSymbols->fShortWeekdaysCount, nullptr, cal)) > 0)
                    return newStart;
            }
            // EEE abbreviated failed, now try EEEEEE short
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 6) {
                if ((newStart = matchString(text, start, UCAL_DAY_OF_WEEK,
                                       fSymbols->fShorterWeekdays, fSymbols->fShorterWeekdaysCount, nullptr, cal)) > 0)
                    return newStart;
            }
            // EEEEEE short failed, now try EEEEE narrow
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 5) {
                if ((newStart = matchString(text, start, UCAL_DAY_OF_WEEK,
                                       fSymbols->fNarrowWeekdays, fSymbols->fNarrowWeekdaysCount, nullptr, cal)) > 0)
                    return newStart;
            }
            if (!getBooleanAttribute(UDAT_PARSE_ALLOW_NUMERIC, status) || patternCharIndex == UDAT_DAY_OF_WEEK_FIELD)
                return newStart;
            // else we allowing parsing as number, below
        }
        break;

    case UDAT_STANDALONE_DAY_FIELD:
        {
            if (gotNumber) // c or cc
            {
                // [We computed 'value' above.]
                cal.set(UCAL_DOW_LOCAL, value);
                return pos.getIndex();
            }
            // Want to be able to parse both short and long forms.
            // Try count == 4 (cccc) first:
            int32_t newStart = 0;
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 4) {
                if ((newStart = matchString(text, start, UCAL_DAY_OF_WEEK,
                                      fSymbols->fStandaloneWeekdays, fSymbols->fStandaloneWeekdaysCount, nullptr, cal)) > 0)
                    return newStart;
            }
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 3) {
                if ((newStart = matchString(text, start, UCAL_DAY_OF_WEEK,
                                          fSymbols->fStandaloneShortWeekdays, fSymbols->fStandaloneShortWeekdaysCount, nullptr, cal)) > 0)
                    return newStart;
            }
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 6) {
                if ((newStart = matchString(text, start, UCAL_DAY_OF_WEEK,
                                          fSymbols->fStandaloneShorterWeekdays, fSymbols->fStandaloneShorterWeekdaysCount, nullptr, cal)) > 0)
                    return newStart;
            }
            if (!getBooleanAttribute(UDAT_PARSE_ALLOW_NUMERIC, status))
                return newStart;
            // else we allowing parsing as number, below
        }
        break;

    case UDAT_AM_PM_FIELD:
        {
            // optionally try both wide/abbrev and narrow forms
            int32_t newStart = 0;
            // try wide/abbrev
            if( getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count < 5 ) {
                if ((newStart = matchString(text, start, UCAL_AM_PM, fSymbols->fAmPms, fSymbols->fAmPmsCount, nullptr, cal)) > 0) {
                    return newStart;
                }
            }
            // try narrow
            if( getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count >= 5 ) {
                if ((newStart = matchString(text, start, UCAL_AM_PM, fSymbols->fNarrowAmPms, fSymbols->fNarrowAmPmsCount, nullptr, cal)) > 0) {
                    return newStart;
                }
            }
            // no matches for given options
            return -start;
        }

    case UDAT_HOUR1_FIELD:
        // [We computed 'value' above.]
        if (value == cal.getLeastMaximum(UCAL_HOUR)+1)
            value = 0;

        // fall through to set field
        U_FALLTHROUGH;
    case UDAT_HOUR0_FIELD:
        cal.set(UCAL_HOUR, value);
        return pos.getIndex();

    case UDAT_QUARTER_FIELD:
        if (gotNumber) // i.e., Q or QQ.
        {
            // Don't want to parse the month if it is a string
            // while pattern uses numeric style: Q or QQ.
            // [We computed 'value' above.]
            cal.set(UCAL_MONTH, (value - 1) * 3);
            return pos.getIndex();
        } else {
            // count >= 3 // i.e., QQQ or QQQQ
            // Want to be able to parse short, long, and narrow forms.
            // Try count == 4 first:
            int32_t newStart = 0;

            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 4) {
                if ((newStart = matchQuarterString(text, start, UCAL_MONTH,
                                      fSymbols->fQuarters, fSymbols->fQuartersCount, cal)) > 0)
                    return newStart;
            }
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 3) {
                if ((newStart = matchQuarterString(text, start, UCAL_MONTH,
                                          fSymbols->fShortQuarters, fSymbols->fShortQuartersCount, cal)) > 0)
                    return newStart;
            }
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 5) {
                if ((newStart = matchQuarterString(text, start, UCAL_MONTH,
                                      fSymbols->fNarrowQuarters, fSymbols->fNarrowQuartersCount, cal)) > 0)
                    return newStart;
            }
            if (!getBooleanAttribute(UDAT_PARSE_ALLOW_NUMERIC, status))
                return newStart;
            // else we allowing parsing as number, below
            if(!getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status))
                return -start;
        }
        break;

    case UDAT_STANDALONE_QUARTER_FIELD:
        if (gotNumber) // i.e., q or qq.
        {
            // Don't want to parse the month if it is a string
            // while pattern uses numeric style: q or q.
            // [We computed 'value' above.]
            cal.set(UCAL_MONTH, (value - 1) * 3);
            return pos.getIndex();
        } else {
            // count >= 3 // i.e., qqq or qqqq
            // Want to be able to parse both short and long forms.
            // Try count == 4 first:
            int32_t newStart = 0;

            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 4) {
                if ((newStart = matchQuarterString(text, start, UCAL_MONTH,
                                      fSymbols->fStandaloneQuarters, fSymbols->fStandaloneQuartersCount, cal)) > 0)
                    return newStart;
            }
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 3) {
                if ((newStart = matchQuarterString(text, start, UCAL_MONTH,
                                          fSymbols->fStandaloneShortQuarters, fSymbols->fStandaloneShortQuartersCount, cal)) > 0)
                    return newStart;
            }
            if(getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 5) {
                if ((newStart = matchQuarterString(text, start, UCAL_MONTH,
                                          fSymbols->fStandaloneNarrowQuarters, fSymbols->fStandaloneNarrowQuartersCount, cal)) > 0)
                    return newStart;
            }
            if (!getBooleanAttribute(UDAT_PARSE_ALLOW_NUMERIC, status))
                return newStart;
            // else we allowing parsing as number, below
            if(!getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status))
                return -start;
        }
        break;

    case UDAT_TIMEZONE_FIELD: // 'z'
        {
            UTimeZoneFormatStyle style = (count < 4) ? UTZFMT_STYLE_SPECIFIC_SHORT : UTZFMT_STYLE_SPECIFIC_LONG;
            const TimeZoneFormat *tzfmt = tzFormat(status);
            if (U_SUCCESS(status)) {
                TimeZone *tz = tzfmt->parse(style, text, pos, tzTimeType);
                if (tz != nullptr) {
                    cal.adoptTimeZone(tz);
                    return pos.getIndex();
                }
            }
            return -start;
    }
        break;
    case UDAT_TIMEZONE_RFC_FIELD: // 'Z'
        {
            UTimeZoneFormatStyle style = (count < 4) ?
                UTZFMT_STYLE_ISO_BASIC_LOCAL_FULL : ((count == 5) ? UTZFMT_STYLE_ISO_EXTENDED_FULL: UTZFMT_STYLE_LOCALIZED_GMT);
            const TimeZoneFormat *tzfmt = tzFormat(status);
            if (U_SUCCESS(status)) {
                TimeZone *tz = tzfmt->parse(style, text, pos, tzTimeType);
                if (tz != nullptr) {
                    cal.adoptTimeZone(tz);
                    return pos.getIndex();
                }
            }
            return -start;
        }
    case UDAT_TIMEZONE_GENERIC_FIELD: // 'v'
        {
            UTimeZoneFormatStyle style = (count < 4) ? UTZFMT_STYLE_GENERIC_SHORT : UTZFMT_STYLE_GENERIC_LONG;
            const TimeZoneFormat *tzfmt = tzFormat(status);
            if (U_SUCCESS(status)) {
                TimeZone *tz = tzfmt->parse(style, text, pos, tzTimeType);
                if (tz != nullptr) {
                    cal.adoptTimeZone(tz);
                    return pos.getIndex();
                }
            }
            return -start;
        }
    case UDAT_TIMEZONE_SPECIAL_FIELD: // 'V'
        {
            UTimeZoneFormatStyle style;
            switch (count) {
            case 1:
                style = UTZFMT_STYLE_ZONE_ID_SHORT;
                break;
            case 2:
                style = UTZFMT_STYLE_ZONE_ID;
                break;
            case 3:
                style = UTZFMT_STYLE_EXEMPLAR_LOCATION;
                break;
            default:
                style = UTZFMT_STYLE_GENERIC_LOCATION;
                break;
            }
            const TimeZoneFormat *tzfmt = tzFormat(status);
            if (U_SUCCESS(status)) {
                TimeZone *tz = tzfmt->parse(style, text, pos, tzTimeType);
                if (tz != nullptr) {
                    cal.adoptTimeZone(tz);
                    return pos.getIndex();
                }
            }
            return -start;
        }
    case UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD: // 'O'
        {
            UTimeZoneFormatStyle style = (count < 4) ? UTZFMT_STYLE_LOCALIZED_GMT_SHORT : UTZFMT_STYLE_LOCALIZED_GMT;
            const TimeZoneFormat *tzfmt = tzFormat(status);
            if (U_SUCCESS(status)) {
                TimeZone *tz = tzfmt->parse(style, text, pos, tzTimeType);
                if (tz != nullptr) {
                    cal.adoptTimeZone(tz);
                    return pos.getIndex();
                }
            }
            return -start;
        }
    case UDAT_TIMEZONE_ISO_FIELD: // 'X'
        {
            UTimeZoneFormatStyle style;
            switch (count) {
            case 1:
                style = UTZFMT_STYLE_ISO_BASIC_SHORT;
                break;
            case 2:
                style = UTZFMT_STYLE_ISO_BASIC_FIXED;
                break;
            case 3:
                style = UTZFMT_STYLE_ISO_EXTENDED_FIXED;
                break;
            case 4:
                style = UTZFMT_STYLE_ISO_BASIC_FULL;
                break;
            default:
                style = UTZFMT_STYLE_ISO_EXTENDED_FULL;
                break;
            }
            const TimeZoneFormat *tzfmt = tzFormat(status);
            if (U_SUCCESS(status)) {
                TimeZone *tz = tzfmt->parse(style, text, pos, tzTimeType);
                if (tz != nullptr) {
                    cal.adoptTimeZone(tz);
                    return pos.getIndex();
                }
            }
            return -start;
        }
    case UDAT_TIMEZONE_ISO_LOCAL_FIELD: // 'x'
        {
            UTimeZoneFormatStyle style;
            switch (count) {
            case 1:
                style = UTZFMT_STYLE_ISO_BASIC_LOCAL_SHORT;
                break;
            case 2:
                style = UTZFMT_STYLE_ISO_BASIC_LOCAL_FIXED;
                break;
            case 3:
                style = UTZFMT_STYLE_ISO_EXTENDED_LOCAL_FIXED;
                break;
            case 4:
                style = UTZFMT_STYLE_ISO_BASIC_LOCAL_FULL;
                break;
            default:
                style = UTZFMT_STYLE_ISO_EXTENDED_LOCAL_FULL;
                break;
            }
            const TimeZoneFormat *tzfmt = tzFormat(status);
            if (U_SUCCESS(status)) {
                TimeZone *tz = tzfmt->parse(style, text, pos, tzTimeType);
                if (tz != nullptr) {
                    cal.adoptTimeZone(tz);
                    return pos.getIndex();
                }
            }
            return -start;
        }
    // currently no pattern character is defined for UDAT_TIME_SEPARATOR_FIELD
    // so we should not get here. Leave support in for future definition.
    case UDAT_TIME_SEPARATOR_FIELD:
        {
            static const char16_t def_sep = DateFormatSymbols::DEFAULT_TIME_SEPARATOR;
            static const char16_t alt_sep = DateFormatSymbols::ALTERNATE_TIME_SEPARATOR;

            // Try matching a time separator.
            int32_t count_sep = 1;
            UnicodeString data[3];
            fSymbols->getTimeSeparatorString(data[0]);

            // Add the default, if different from the locale.
            if (data[0].compare(&def_sep, 1) != 0) {
                data[count_sep++].setTo(def_sep);
            }

            // If lenient, add also the alternate, if different from the locale.
            if (isLenient() && data[0].compare(&alt_sep, 1) != 0) {
                data[count_sep++].setTo(alt_sep);
            }

            return matchString(text, start, UCAL_FIELD_COUNT /* => nothing to set */, data, count_sep, nullptr, cal);
        }

    case UDAT_AM_PM_MIDNIGHT_NOON_FIELD:
    {
        U_ASSERT(dayPeriod != nullptr);
        int32_t ampmStart = subParse(text, start, 0x61, count,
                           obeyCount, allowNegative, ambiguousYear, saveHebrewMonth, cal,
                           patLoc, numericLeapMonthFormatter, tzTimeType);

        if (ampmStart > 0) {
            return ampmStart;
        } else {
            int32_t newStart = 0;

            // Only match the first two strings from the day period strings array.
            if (getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 3) {
                if ((newStart = matchDayPeriodStrings(text, start, fSymbols->fAbbreviatedDayPeriods,
                                                        2, *dayPeriod)) > 0) {
                    return newStart;
                }
            }
            if (getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 5) {
                if ((newStart = matchDayPeriodStrings(text, start, fSymbols->fNarrowDayPeriods,
                                                        2, *dayPeriod)) > 0) {
                    return newStart;
                }
            }
            // count == 4, but allow other counts
            if (getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status)) {
                if ((newStart = matchDayPeriodStrings(text, start, fSymbols->fWideDayPeriods,
                                                        2, *dayPeriod)) > 0) {
                    return newStart;
                }
            }

            return -start;
        }
    }

    case UDAT_FLEXIBLE_DAY_PERIOD_FIELD:
    {
        U_ASSERT(dayPeriod != nullptr);
        int32_t newStart = 0;

        if (getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 3) {
            if ((newStart = matchDayPeriodStrings(text, start, fSymbols->fAbbreviatedDayPeriods,
                                fSymbols->fAbbreviatedDayPeriodsCount, *dayPeriod)) > 0) {
                return newStart;
            }
        }
        if (getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 5) {
            if ((newStart = matchDayPeriodStrings(text, start, fSymbols->fNarrowDayPeriods,
                                fSymbols->fNarrowDayPeriodsCount, *dayPeriod)) > 0) {
                return newStart;
            }
        }
        if (getBooleanAttribute(UDAT_PARSE_MULTIPLE_PATTERNS_FOR_MATCH, status) || count == 4) {
            if ((newStart = matchDayPeriodStrings(text, start, fSymbols->fWideDayPeriods,
                                fSymbols->fWideDayPeriodsCount, *dayPeriod)) > 0) {
                return newStart;
            }
        }

        return -start;
    }

    default:
        // Handle "generic" fields
        // this is now handled below, outside the switch block
        break;
    }
    // Handle "generic" fields:
    // switch default case now handled here (outside switch block) to allow
    // parsing of some string fields as digits for lenient case

    int32_t parseStart = pos.getIndex();
    const UnicodeString* src;
    if (obeyCount) {
        if ((start+count) > text.length()) {
            return -start;
        }
        text.extractBetween(0, start + count, temp);
        src = &temp;
    } else {
        src = &text;
    }
    parseInt(*src, number, pos, allowNegative,currentNumberFormat);
    if (obeyCount && !isLenient() && pos.getIndex() < start + count) {
        return -start;
    }
    if (pos.getIndex() != parseStart) {
        int32_t val = number.getLong();

        // Don't need suffix processing here (as in number processing at the beginning of the function);
        // the new fields being handled as numeric values (month, weekdays, quarters) should not have suffixes.

        if (!getBooleanAttribute(UDAT_PARSE_ALLOW_NUMERIC, status)) {
            // Check the range of the value
            int32_t bias = gFieldRangeBias[patternCharIndex];
            if (bias >= 0 && (val > cal.getMaximum(field) + bias || val < cal.getMinimum(field) + bias)) {
                return -start;
            }
        }

        // For the following, need to repeat some of the "if (gotNumber)" code above:
        // UDAT_[STANDALONE_]MONTH_FIELD, UDAT_DOW_LOCAL_FIELD, UDAT_STANDALONE_DAY_FIELD,
        // UDAT_[STANDALONE_]QUARTER_FIELD
        switch (patternCharIndex) {
        case UDAT_MONTH_FIELD:
            // See notes under UDAT_MONTH_FIELD case above
            if (!strcmp(cal.getType(),"hebrew")) {
                HebrewCalendar *hc = (HebrewCalendar*)&cal;
                if (cal.isSet(UCAL_YEAR)) {
                   UErrorCode monthStatus = U_ZERO_ERROR;
                   if (!hc->isLeapYear(hc->get(UCAL_YEAR, monthStatus)) && val >= 6) {
                       cal.set(UCAL_MONTH, val);
                   } else {
                       cal.set(UCAL_MONTH, val - 1);
                   }
                } else {
                    saveHebrewMonth = val;
                }
            } else {
                cal.set(UCAL_MONTH, val - 1);
            }
            break;
        case UDAT_STANDALONE_MONTH_FIELD:
            cal.set(UCAL_MONTH, val - 1);
            break;
        case UDAT_DOW_LOCAL_FIELD:
        case UDAT_STANDALONE_DAY_FIELD:
            cal.set(UCAL_DOW_LOCAL, val);
            break;
        case UDAT_QUARTER_FIELD:
        case UDAT_STANDALONE_QUARTER_FIELD:
             cal.set(UCAL_MONTH, (val - 1) * 3);
             break;
        case UDAT_RELATED_YEAR_FIELD:
            cal.setRelatedYear(val);
            break;
        default:
            cal.set(field, val);
            break;
        }
        return pos.getIndex();
    }
    return -start;
}

/**
 * Parse an integer using fNumberFormat.  This method is semantically
 * const, but actually may modify fNumberFormat.
 */
void SimpleDateFormat::parseInt(const UnicodeString& text,
                                Formattable& number,
                                ParsePosition& pos,
                                UBool allowNegative,
                                const NumberFormat *fmt) const {
    parseInt(text, number, -1, pos, allowNegative,fmt);
}

/**
 * Parse an integer using fNumberFormat up to maxDigits.
 */
void SimpleDateFormat::parseInt(const UnicodeString& text,
                                Formattable& number,
                                int32_t maxDigits,
                                ParsePosition& pos,
                                UBool allowNegative,
                                const NumberFormat *fmt) const {
    UnicodeString oldPrefix;
    const auto* fmtAsDF = dynamic_cast<const DecimalFormat*>(fmt);
    LocalPointer<DecimalFormat> df;
    if (!allowNegative && fmtAsDF != nullptr) {
        df.adoptInstead(fmtAsDF->clone());
        if (df.isNull()) {
            // Memory allocation error
            return;
        }
        df->setNegativePrefix(UnicodeString(true, SUPPRESS_NEGATIVE_PREFIX, -1));
        fmt = df.getAlias();
    }
    int32_t oldPos = pos.getIndex();
    fmt->parse(text, number, pos);

    if (maxDigits > 0) {
        // adjust the result to fit into
        // the maxDigits and move the position back
        int32_t nDigits = pos.getIndex() - oldPos;
        if (nDigits > maxDigits) {
            int32_t val = number.getLong();
            nDigits -= maxDigits;
            while (nDigits > 0) {
                val /= 10;
                nDigits--;
            }
            pos.setIndex(oldPos + maxDigits);
            number.setLong(val);
        }
    }
}

int32_t SimpleDateFormat::countDigits(const UnicodeString& text, int32_t start, int32_t end) const {
    int32_t numDigits = 0;
    int32_t idx = start;
    while (idx < end) {
        UChar32 cp = text.char32At(idx);
        if (u_isdigit(cp)) {
            numDigits++;
        }
        idx += U16_LENGTH(cp);
    }
    return numDigits;
}

//----------------------------------------------------------------------

void SimpleDateFormat::translatePattern(const UnicodeString& originalPattern,
                                        UnicodeString& translatedPattern,
                                        const UnicodeString& from,
                                        const UnicodeString& to,
                                        UErrorCode& status)
{
    // run through the pattern and convert any pattern symbols from the version
    // in "from" to the corresponding character in "to".  This code takes
    // quoted strings into account (it doesn't try to translate them), and it signals
    // an error if a particular "pattern character" doesn't appear in "from".
    // Depending on the values of "from" and "to" this can convert from generic
    // to localized patterns or localized to generic.
    if (U_FAILURE(status)) {
        return;
    }

    translatedPattern.remove();
    UBool inQuote = false;
    for (int32_t i = 0; i < originalPattern.length(); ++i) {
        char16_t c = originalPattern[i];
        if (inQuote) {
            if (c == QUOTE) {
                inQuote = false;
            }
        } else {
            if (c == QUOTE) {
                inQuote = true;
            } else if (isSyntaxChar(c)) {
                int32_t ci = from.indexOf(c);
                if (ci == -1) {
                    status = U_INVALID_FORMAT_ERROR;
                    return;
                }
                c = to[ci];
            }
        }
        translatedPattern += c;
    }
    if (inQuote) {
        status = U_INVALID_FORMAT_ERROR;
        return;
    }
}

//----------------------------------------------------------------------

UnicodeString&
SimpleDateFormat::toPattern(UnicodeString& result) const
{
    result = fPattern;
    return result;
}

//----------------------------------------------------------------------

UnicodeString&
SimpleDateFormat::toLocalizedPattern(UnicodeString& result,
                                     UErrorCode& status) const
{
    translatePattern(fPattern, result,
                     UnicodeString(DateFormatSymbols::getPatternUChars()),
                     fSymbols->fLocalPatternChars, status);
    return result;
}

//----------------------------------------------------------------------

void
SimpleDateFormat::applyPattern(const UnicodeString& pattern)
{
    fPattern = pattern;
    parsePattern();

    // Hack to update use of Gannen year numbering for ja@calendar=japanese -
    // use only if format is non-numeric (includes 年) and no other fDateOverride.
    if (fCalendar != nullptr && uprv_strcmp(fCalendar->getType(),"japanese") == 0 &&
            uprv_strcmp(fLocale.getLanguage(),"ja") == 0) {
        if (fDateOverride==UnicodeString(u"y=jpanyear") && !fHasHanYearChar) {
            // Gannen numbering is set but new pattern should not use it, unset;
            // use procedure from adoptNumberFormat to clear overrides
            if (fSharedNumberFormatters) {
                freeSharedNumberFormatters(fSharedNumberFormatters);
                fSharedNumberFormatters = nullptr;
            }
            fDateOverride.setToBogus(); // record status
        } else if (fDateOverride.isBogus() && fHasHanYearChar) {
            // No current override (=> no Gannen numbering) but new pattern needs it;
            // use procedures from initNUmberFormatters / adoptNumberFormat
            umtx_lock(&LOCK);
            if (fSharedNumberFormatters == nullptr) {
                fSharedNumberFormatters = allocSharedNumberFormatters();
            }
            umtx_unlock(&LOCK);
            if (fSharedNumberFormatters != nullptr) {
                Locale ovrLoc(fLocale.getLanguage(),fLocale.getCountry(),fLocale.getVariant(),"numbers=jpanyear");
                UErrorCode status = U_ZERO_ERROR;
                const SharedNumberFormat *snf = createSharedNumberFormat(ovrLoc, status);
                if (U_SUCCESS(status)) {
                    // Now that we have an appropriate number formatter, fill in the
                    // appropriate slot in the number formatters table.
                    UDateFormatField patternCharIndex = DateFormatSymbols::getPatternCharIndex(u'y');
                    SharedObject::copyPtr(snf, fSharedNumberFormatters[patternCharIndex]);
                    snf->deleteIfZeroRefCount();
                    fDateOverride.setTo(u"y=jpanyear", -1); // record status
                }
            }
        }
    }
}

//----------------------------------------------------------------------

void
SimpleDateFormat::applyLocalizedPattern(const UnicodeString& pattern,
                                        UErrorCode &status)
{
    translatePattern(pattern, fPattern,
                     fSymbols->fLocalPatternChars,
                     UnicodeString(DateFormatSymbols::getPatternUChars()), status);
}

//----------------------------------------------------------------------

const DateFormatSymbols*
SimpleDateFormat::getDateFormatSymbols() const
{
    return fSymbols;
}

//----------------------------------------------------------------------

void
SimpleDateFormat::adoptDateFormatSymbols(DateFormatSymbols* newFormatSymbols)
{
    delete fSymbols;
    fSymbols = newFormatSymbols;
}

//----------------------------------------------------------------------
void
SimpleDateFormat::setDateFormatSymbols(const DateFormatSymbols& newFormatSymbols)
{
    delete fSymbols;
    fSymbols = new DateFormatSymbols(newFormatSymbols);
}

//----------------------------------------------------------------------
const TimeZoneFormat*
SimpleDateFormat::getTimeZoneFormat() const {
    // TimeZoneFormat initialization might fail when out of memory.
    // If we always initialize TimeZoneFormat instance, we can return
    // such status there. For now, this implementation lazily instantiates
    // a TimeZoneFormat for performance optimization reasons, but cannot
    // propagate such error (probably just out of memory case) to the caller.
    UErrorCode status = U_ZERO_ERROR;
    return (const TimeZoneFormat*)tzFormat(status);
}

//----------------------------------------------------------------------
void
SimpleDateFormat::adoptTimeZoneFormat(TimeZoneFormat* timeZoneFormatToAdopt)
{
    delete fTimeZoneFormat;
    fTimeZoneFormat = timeZoneFormatToAdopt;
}

//----------------------------------------------------------------------
void
SimpleDateFormat::setTimeZoneFormat(const TimeZoneFormat& newTimeZoneFormat)
{
    delete fTimeZoneFormat;
    fTimeZoneFormat = new TimeZoneFormat(newTimeZoneFormat);
}

//----------------------------------------------------------------------


void SimpleDateFormat::adoptCalendar(Calendar* calendarToAdopt)
{
  UErrorCode status = U_ZERO_ERROR;
  Locale calLocale(fLocale);
  calLocale.setKeywordValue("calendar", calendarToAdopt->getType(), status);
  DateFormatSymbols *newSymbols =
          DateFormatSymbols::createForLocale(calLocale, status);
  if (U_FAILURE(status)) {
      delete calendarToAdopt;
      return;
  }
  DateFormat::adoptCalendar(calendarToAdopt);
  delete fSymbols;
  fSymbols = newSymbols;
  initializeDefaultCentury();  // we need a new century (possibly)
}


//----------------------------------------------------------------------


// override the DateFormat implementation in order to
// lazily initialize fCapitalizationBrkIter
void
SimpleDateFormat::setContext(UDisplayContext value, UErrorCode& status)
{
    DateFormat::setContext(value, status);
#if !UCONFIG_NO_BREAK_ITERATION
    if (U_SUCCESS(status)) {
        if ( fCapitalizationBrkIter == nullptr && (value==UDISPCTX_CAPITALIZATION_FOR_BEGINNING_OF_SENTENCE ||
                value==UDISPCTX_CAPITALIZATION_FOR_UI_LIST_OR_MENU || value==UDISPCTX_CAPITALIZATION_FOR_STANDALONE) ) {
            status = U_ZERO_ERROR;
            fCapitalizationBrkIter = BreakIterator::createSentenceInstance(fLocale, status);
            if (U_FAILURE(status)) {
                delete fCapitalizationBrkIter;
                fCapitalizationBrkIter = nullptr;
            }
        }
    }
#endif
}


//----------------------------------------------------------------------


UBool
SimpleDateFormat::isFieldUnitIgnored(UCalendarDateFields field) const {
    return isFieldUnitIgnored(fPattern, field);
}


UBool
SimpleDateFormat::isFieldUnitIgnored(const UnicodeString& pattern,
                                     UCalendarDateFields field) {
    int32_t fieldLevel = fgCalendarFieldToLevel[field];
    int32_t level;
    char16_t ch;
    UBool inQuote = false;
    char16_t prevCh = 0;
    int32_t count = 0;

    for (int32_t i = 0; i < pattern.length(); ++i) {
        ch = pattern[i];
        if (ch != prevCh && count > 0) {
            level = getLevelFromChar(prevCh);
            // the larger the level, the smaller the field unit.
            if (fieldLevel <= level) {
                return false;
            }
            count = 0;
        }
        if (ch == QUOTE) {
            if ((i+1) < pattern.length() && pattern[i+1] == QUOTE) {
                ++i;
            } else {
                inQuote = ! inQuote;
            }
        }
        else if (!inQuote && isSyntaxChar(ch)) {
            prevCh = ch;
            ++count;
        }
    }
    if (count > 0) {
        // last item
        level = getLevelFromChar(prevCh);
        if (fieldLevel <= level) {
            return false;
        }
    }
    return true;
}

//----------------------------------------------------------------------

const Locale&
SimpleDateFormat::getSmpFmtLocale() const {
    return fLocale;
}

//----------------------------------------------------------------------

int32_t
SimpleDateFormat::checkIntSuffix(const UnicodeString& text, int32_t start,
                                 int32_t patLoc, UBool isNegative) const {
    // local variables
    UnicodeString suf;
    int32_t patternMatch;
    int32_t textPreMatch;
    int32_t textPostMatch;

    // check that we are still in range
    if ( (start > text.length()) ||
         (start < 0) ||
         (patLoc < 0) ||
         (patLoc > fPattern.length())) {
        // out of range, don't advance location in text
        return start;
    }

    // get the suffix
    DecimalFormat* decfmt = dynamic_cast<DecimalFormat*>(fNumberFormat);
    if (decfmt != nullptr) {
        if (isNegative) {
            suf = decfmt->getNegativeSuffix(suf);
        }
        else {
            suf = decfmt->getPositiveSuffix(suf);
        }
    }

    // check for suffix
    if (suf.length() <= 0) {
        return start;
    }

    // check suffix will be encountered in the pattern
    patternMatch = compareSimpleAffix(suf,fPattern,patLoc);

    // check if a suffix will be encountered in the text
    textPreMatch = compareSimpleAffix(suf,text,start);

    // check if a suffix was encountered in the text
    textPostMatch = compareSimpleAffix(suf,text,start-suf.length());

    // check for suffix match
    if ((textPreMatch >= 0) && (patternMatch >= 0) && (textPreMatch == patternMatch)) {
        return start;
    }
    else if ((textPostMatch >= 0) && (patternMatch >= 0) && (textPostMatch == patternMatch)) {
        return  start - suf.length();
    }

    // should not get here
    return start;
}

//----------------------------------------------------------------------

int32_t
SimpleDateFormat::compareSimpleAffix(const UnicodeString& affix,
                   const UnicodeString& input,
                   int32_t pos) const {
    int32_t start = pos;
    for (int32_t i=0; i<affix.length(); ) {
        UChar32 c = affix.char32At(i);
        int32_t len = U16_LENGTH(c);
        if (PatternProps::isWhiteSpace(c)) {
            // We may have a pattern like: \u200F \u0020
            //        and input text like: \u200F \u0020
            // Note that U+200F and U+0020 are Pattern_White_Space but only
            // U+0020 is UWhiteSpace.  So we have to first do a direct
            // match of the run of Pattern_White_Space in the pattern,
            // then match any extra characters.
            UBool literalMatch = false;
            while (pos < input.length() &&
                   input.char32At(pos) == c) {
                literalMatch = true;
                i += len;
                pos += len;
                if (i == affix.length()) {
                    break;
                }
                c = affix.char32At(i);
                len = U16_LENGTH(c);
                if (!PatternProps::isWhiteSpace(c)) {
                    break;
                }
            }

            // Advance over run in pattern
            i = skipPatternWhiteSpace(affix, i);

            // Advance over run in input text
            // Must see at least one white space char in input,
            // unless we've already matched some characters literally.
            int32_t s = pos;
            pos = skipUWhiteSpace(input, pos);
            if (pos == s && !literalMatch) {
                return -1;
            }

            // If we skip UWhiteSpace in the input text, we need to skip it in the pattern.
            // Otherwise, the previous lines may have skipped over text (such as U+00A0) that
            // is also in the affix.
            i = skipUWhiteSpace(affix, i);
        } else {
            if (pos < input.length() &&
                input.char32At(pos) == c) {
                i += len;
                pos += len;
            } else {
                return -1;
            }
        }
    }
    return pos - start;
}

//----------------------------------------------------------------------

int32_t
SimpleDateFormat::skipPatternWhiteSpace(const UnicodeString& text, int32_t pos) const {
    const char16_t* s = text.getBuffer();
    return static_cast<int32_t>(PatternProps::skipWhiteSpace(s + pos, text.length() - pos) - s);
}

//----------------------------------------------------------------------

int32_t
SimpleDateFormat::skipUWhiteSpace(const UnicodeString& text, int32_t pos) const {
    while (pos < text.length()) {
        UChar32 c = text.char32At(pos);
        if (!u_isUWhiteSpace(c)) {
            break;
        }
        pos += U16_LENGTH(c);
    }
    return pos;
}

//----------------------------------------------------------------------

// Lazy TimeZoneFormat instantiation, semantically const.
TimeZoneFormat *
SimpleDateFormat::tzFormat(UErrorCode &status) const {
    Mutex m(&LOCK);
    if (fTimeZoneFormat == nullptr && U_SUCCESS(status)) {
        const_cast<SimpleDateFormat *>(this)->fTimeZoneFormat =
                TimeZoneFormat::createInstance(fLocale, status);
    }
    return fTimeZoneFormat;
}

void SimpleDateFormat::parsePattern() {
    fHasMinute = false;
    fHasSecond = false;
    fHasHanYearChar = false;

    int len = fPattern.length();
    UBool inQuote = false;
    for (int32_t i = 0; i < len; ++i) {
        char16_t ch = fPattern[i];
        if (ch == QUOTE) {
            inQuote = !inQuote;
        }
        if (ch == 0x5E74) { // don't care whether this is inside quotes
            fHasHanYearChar = true;
        }
        if (!inQuote) {
            if (ch == 0x6D) {  // 0x6D == 'm'
                fHasMinute = true;
            }
            if (ch == 0x73) {  // 0x73 == 's'
                fHasSecond = true;
            }
        }
    }
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
