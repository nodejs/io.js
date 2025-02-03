// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 1997-2016, International Business Machines Corporation and    *
* others. All Rights Reserved.                                                *
*******************************************************************************
*
* File DTFMTSYM.CPP
*
* Modification History:
*
*   Date        Name        Description
*   02/19/97    aliu        Converted from java.
*   07/21/98    stephen     Added getZoneIndex
*                            Changed weekdays/short weekdays to be one-based
*   06/14/99    stephen     Removed SimpleDateFormat::fgTimeZoneDataSuffix
*   11/16/99    weiv        Added 'Y' and 'e' to fgPatternChars
*   03/27/00    weiv        Keeping resource bundle around!
*   06/30/05    emmons      Added eraNames, narrow month/day, standalone context
*   10/12/05    emmons      Added setters for eraNames, month/day by width/context
*******************************************************************************
*/

#include <utility>

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#include "unicode/ustring.h"
#include "unicode/localpointer.h"
#include "unicode/dtfmtsym.h"
#include "unicode/smpdtfmt.h"
#include "unicode/msgfmt.h"
#include "unicode/numsys.h"
#include "unicode/tznames.h"
#include "cpputils.h"
#include "umutex.h"
#include "cmemory.h"
#include "cstring.h"
#include "charstr.h"
#include "dt_impl.h"
#include "locbased.h"
#include "gregoimp.h"
#include "hash.h"
#include "uassert.h"
#include "uresimp.h"
#include "ureslocs.h"
#include "uvector.h"
#include "shareddateformatsymbols.h"
#include "unicode/calendar.h"
#include "unifiedcache.h"

// *****************************************************************************
// class DateFormatSymbols
// *****************************************************************************

/**
 * These are static arrays we use only in the case where we have no
 * resource data.
 */

#if UDAT_HAS_PATTERN_CHAR_FOR_TIME_SEPARATOR
#define PATTERN_CHARS_LEN 38
#else
#define PATTERN_CHARS_LEN 37
#endif

/**
 * Unlocalized date-time pattern characters. For example: 'y', 'd', etc. All
 * locales use the same these unlocalized pattern characters.
 */
static const char16_t gPatternChars[] = {
    // if UDAT_HAS_PATTERN_CHAR_FOR_TIME_SEPARATOR:
    //   GyMdkHmsSEDFwWahKzYeugAZvcLQqVUOXxrbB:
    // else:
    //   GyMdkHmsSEDFwWahKzYeugAZvcLQqVUOXxrbB

    0x47, 0x79, 0x4D, 0x64, 0x6B, 0x48, 0x6D, 0x73, 0x53, 0x45,
    0x44, 0x46, 0x77, 0x57, 0x61, 0x68, 0x4B, 0x7A, 0x59, 0x65,
    0x75, 0x67, 0x41, 0x5A, 0x76, 0x63, 0x4c, 0x51, 0x71, 0x56,
    0x55, 0x4F, 0x58, 0x78, 0x72, 0x62, 0x42,
#if UDAT_HAS_PATTERN_CHAR_FOR_TIME_SEPARATOR
    0x3a,
#endif
    0
};

/**
 * Map of each ASCII character to its corresponding index in the table above if
 * it is a pattern character and -1 otherwise.
 */
static const int8_t gLookupPatternChars[] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    //
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    //       !   "   #   $   %   &   '   (   )   *   +   ,   -   .   /
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
#if UDAT_HAS_PATTERN_CHAR_FOR_TIME_SEPARATOR
    //   0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ?
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 37, -1, -1, -1, -1, -1,
#else
    //   0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ?
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
#endif
    //   @   A   B   C   D   E   F   G   H   I   J   K   L   M   N   O
        -1, 22, 36, -1, 10,  9, 11,  0,  5, -1, -1, 16, 26,  2, -1, 31,
    //   P   Q   R   S   T   U   V   W   X   Y   Z   [   \   ]   ^   _
        -1, 27, -1,  8, -1, 30, 29, 13, 32, 18, 23, -1, -1, -1, -1, -1,
    //   `   a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
        -1, 14, 35, 25,  3, 19, -1, 21, 15, -1, -1,  4, -1,  6, -1, -1,
    //   p   q   r   s   t   u   v   w   x   y   z   {   |   }   ~
        -1, 28, 34,  7, -1, 20, 24, 12, 33,  1, 17, -1, -1, -1, -1, -1
};

//------------------------------------------------------
// Strings of last resort.  These are only used if we have no resource
// files.  They aren't designed for actual use, just for backup.

// These are the month names and abbreviations of last resort.
static const char16_t gLastResortMonthNames[13][3] =
{
    {0x0030, 0x0031, 0x0000}, /* "01" */
    {0x0030, 0x0032, 0x0000}, /* "02" */
    {0x0030, 0x0033, 0x0000}, /* "03" */
    {0x0030, 0x0034, 0x0000}, /* "04" */
    {0x0030, 0x0035, 0x0000}, /* "05" */
    {0x0030, 0x0036, 0x0000}, /* "06" */
    {0x0030, 0x0037, 0x0000}, /* "07" */
    {0x0030, 0x0038, 0x0000}, /* "08" */
    {0x0030, 0x0039, 0x0000}, /* "09" */
    {0x0031, 0x0030, 0x0000}, /* "10" */
    {0x0031, 0x0031, 0x0000}, /* "11" */
    {0x0031, 0x0032, 0x0000}, /* "12" */
    {0x0031, 0x0033, 0x0000}  /* "13" */
};

// These are the weekday names and abbreviations of last resort.
static const char16_t gLastResortDayNames[8][2] =
{
    {0x0030, 0x0000}, /* "0" */
    {0x0031, 0x0000}, /* "1" */
    {0x0032, 0x0000}, /* "2" */
    {0x0033, 0x0000}, /* "3" */
    {0x0034, 0x0000}, /* "4" */
    {0x0035, 0x0000}, /* "5" */
    {0x0036, 0x0000}, /* "6" */
    {0x0037, 0x0000}  /* "7" */
};

// These are the quarter names and abbreviations of last resort.
static const char16_t gLastResortQuarters[4][2] =
{
    {0x0031, 0x0000}, /* "1" */
    {0x0032, 0x0000}, /* "2" */
    {0x0033, 0x0000}, /* "3" */
    {0x0034, 0x0000}, /* "4" */
};

// These are the am/pm and BC/AD markers of last resort.
static const char16_t gLastResortAmPmMarkers[2][3] =
{
    {0x0041, 0x004D, 0x0000}, /* "AM" */
    {0x0050, 0x004D, 0x0000}  /* "PM" */
};

static const char16_t gLastResortEras[2][3] =
{
    {0x0042, 0x0043, 0x0000}, /* "BC" */
    {0x0041, 0x0044, 0x0000}  /* "AD" */
};

/* Sizes for the last resort string arrays */
typedef enum LastResortSize {
    kMonthNum = 13,
    kMonthLen = 3,

    kDayNum = 8,
    kDayLen = 2,

    kAmPmNum = 2,
    kAmPmLen = 3,

    kQuarterNum = 4,
    kQuarterLen = 2,

    kEraNum = 2,
    kEraLen = 3,

    kZoneNum = 5,
    kZoneLen = 4,

    kGmtHourNum = 4,
    kGmtHourLen = 10
} LastResortSize;

U_NAMESPACE_BEGIN

SharedDateFormatSymbols::~SharedDateFormatSymbols() {
}

template<> U_I18N_API
const SharedDateFormatSymbols *
        LocaleCacheKey<SharedDateFormatSymbols>::createObject(
                const void * /*unusedContext*/, UErrorCode &status) const {
    char type[256];
    Calendar::getCalendarTypeFromLocale(fLoc, type, UPRV_LENGTHOF(type), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    SharedDateFormatSymbols *shared
            = new SharedDateFormatSymbols(fLoc, type, status);
    if (shared == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    if (U_FAILURE(status)) {
        delete shared;
        return nullptr;
    }
    shared->addRef();
    return shared;
}

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(DateFormatSymbols)

#define kSUPPLEMENTAL "supplementalData"

/**
 * These are the tags we expect to see in normal resource bundle files associated
 * with a locale and calendar
 */
static const char gCalendarTag[]="calendar";
static const char gGregorianTag[]="gregorian";
static const char gErasTag[]="eras";
static const char gCyclicNameSetsTag[]="cyclicNameSets";
static const char gNameSetYearsTag[]="years";
static const char gNameSetZodiacsTag[]="zodiacs";
static const char gMonthNamesTag[]="monthNames";
static const char gMonthPatternsTag[]="monthPatterns";
static const char gDayNamesTag[]="dayNames";
static const char gNamesWideTag[]="wide";
static const char gNamesAbbrTag[]="abbreviated";
static const char gNamesShortTag[]="short";
static const char gNamesNarrowTag[]="narrow";
static const char gNamesAllTag[]="all";
static const char gNamesFormatTag[]="format";
static const char gNamesStandaloneTag[]="stand-alone";
static const char gNamesNumericTag[]="numeric";
static const char gAmPmMarkersTag[]="AmPmMarkers";
static const char gAmPmMarkersAbbrTag[]="AmPmMarkersAbbr";
static const char gAmPmMarkersNarrowTag[]="AmPmMarkersNarrow";
static const char gQuartersTag[]="quarters";
static const char gNumberElementsTag[]="NumberElements";
static const char gSymbolsTag[]="symbols";
static const char gTimeSeparatorTag[]="timeSeparator";
static const char gDayPeriodTag[]="dayPeriod";

// static const char gZoneStringsTag[]="zoneStrings";

// static const char gLocalPatternCharsTag[]="localPatternChars";

static const char gContextTransformsTag[]="contextTransforms";

/**
 * Jitterbug 2974: MSVC has a bug whereby new X[0] behaves badly.
 * Work around this.
 */
static inline UnicodeString* newUnicodeStringArray(size_t count) {
    return new UnicodeString[count ? count : 1];
}

//------------------------------------------------------

DateFormatSymbols * U_EXPORT2
DateFormatSymbols::createForLocale(
        const Locale& locale, UErrorCode &status) {
    const SharedDateFormatSymbols *shared = nullptr;
    UnifiedCache::getByLocale(locale, shared, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    DateFormatSymbols *result = new DateFormatSymbols(shared->get());
    shared->removeRef();
    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    return result;
}

DateFormatSymbols::DateFormatSymbols(const Locale& locale,
                                     UErrorCode& status)
    : UObject()
{
  initializeData(locale, nullptr,  status);
}

DateFormatSymbols::DateFormatSymbols(UErrorCode& status)
    : UObject()
{
  initializeData(Locale::getDefault(), nullptr, status, true);
}


DateFormatSymbols::DateFormatSymbols(const Locale& locale,
                                     const char *type,
                                     UErrorCode& status)
    : UObject()
{
  initializeData(locale, type,  status);
}

DateFormatSymbols::DateFormatSymbols(const char *type, UErrorCode& status)
    : UObject()
{
  initializeData(Locale::getDefault(), type, status, true);
}

DateFormatSymbols::DateFormatSymbols(const DateFormatSymbols& other)
    : UObject(other)
{
    copyData(other);
}

void
DateFormatSymbols::assignArray(UnicodeString*& dstArray,
                               int32_t& dstCount,
                               const UnicodeString* srcArray,
                               int32_t srcCount)
{
    // assignArray() is only called by copyData() and initializeData(), which in turn
    // implements the copy constructor and the assignment operator.
    // All strings in a DateFormatSymbols object are created in one of the following
    // three ways that all allow to safely use UnicodeString::fastCopyFrom():
    // - readonly-aliases from resource bundles
    // - readonly-aliases or allocated strings from constants
    // - safely cloned strings (with owned buffers) from setXYZ() functions
    //
    // Note that this is true for as long as DateFormatSymbols can be constructed
    // only from a locale bundle or set via the cloning API,
    // *and* for as long as all the strings are in *private* fields, preventing
    // a subclass from creating these strings in an "unsafe" way (with respect to fastCopyFrom()).
    if(srcArray == nullptr) {
        // Do not attempt to copy bogus input (which will crash).
        // Note that this assignArray method already had the potential to return a null dstArray;
        // see handling below for "if(dstArray != nullptr)".
        dstCount = 0;
        dstArray = nullptr;
        return;
    }
    dstCount = srcCount;
    dstArray = newUnicodeStringArray(srcCount);
    if(dstArray != nullptr) {
        int32_t i;
        for(i=0; i<srcCount; ++i) {
            dstArray[i].fastCopyFrom(srcArray[i]);
        }
    }
}

/**
 * Create a copy, in fZoneStrings, of the given zone strings array.  The
 * member variables fZoneStringsRowCount and fZoneStringsColCount should
 * be set already by the caller.
 */
void
DateFormatSymbols::createZoneStrings(const UnicodeString *const * otherStrings)
{
    int32_t row, col;
    UBool failed = false;

    fZoneStrings = static_cast<UnicodeString**>(uprv_malloc(fZoneStringsRowCount * sizeof(UnicodeString*)));
    if (fZoneStrings != nullptr) {
        for (row=0; row<fZoneStringsRowCount; ++row)
        {
            fZoneStrings[row] = newUnicodeStringArray(fZoneStringsColCount);
            if (fZoneStrings[row] == nullptr) {
                failed = true;
                break;
            }
            for (col=0; col<fZoneStringsColCount; ++col) {
                // fastCopyFrom() - see assignArray comments
                fZoneStrings[row][col].fastCopyFrom(otherStrings[row][col]);
            }
        }
    }
    // If memory allocation failed, roll back and delete fZoneStrings
    if (failed) {
        for (int i = row; i >= 0; i--) {
            delete[] fZoneStrings[i];
        }
        uprv_free(fZoneStrings);
        fZoneStrings = nullptr;
    }
}

/**
 * Copy all of the other's data to this.
 */
void
DateFormatSymbols::copyData(const DateFormatSymbols& other) {
    UErrorCode status = U_ZERO_ERROR;
    U_LOCALE_BASED(locBased, *this);
    locBased.setLocaleIDs(
        other.getLocale(ULOC_VALID_LOCALE, status),
        other.getLocale(ULOC_ACTUAL_LOCALE, status));
    assignArray(fEras, fErasCount, other.fEras, other.fErasCount);
    assignArray(fEraNames, fEraNamesCount, other.fEraNames, other.fEraNamesCount);
    assignArray(fNarrowEras, fNarrowErasCount, other.fNarrowEras, other.fNarrowErasCount);
    assignArray(fMonths, fMonthsCount, other.fMonths, other.fMonthsCount);
    assignArray(fShortMonths, fShortMonthsCount, other.fShortMonths, other.fShortMonthsCount);
    assignArray(fNarrowMonths, fNarrowMonthsCount, other.fNarrowMonths, other.fNarrowMonthsCount);
    assignArray(fStandaloneMonths, fStandaloneMonthsCount, other.fStandaloneMonths, other.fStandaloneMonthsCount);
    assignArray(fStandaloneShortMonths, fStandaloneShortMonthsCount, other.fStandaloneShortMonths, other.fStandaloneShortMonthsCount);
    assignArray(fStandaloneNarrowMonths, fStandaloneNarrowMonthsCount, other.fStandaloneNarrowMonths, other.fStandaloneNarrowMonthsCount);
    assignArray(fWeekdays, fWeekdaysCount, other.fWeekdays, other.fWeekdaysCount);
    assignArray(fShortWeekdays, fShortWeekdaysCount, other.fShortWeekdays, other.fShortWeekdaysCount);
    assignArray(fShorterWeekdays, fShorterWeekdaysCount, other.fShorterWeekdays, other.fShorterWeekdaysCount);
    assignArray(fNarrowWeekdays, fNarrowWeekdaysCount, other.fNarrowWeekdays, other.fNarrowWeekdaysCount);
    assignArray(fStandaloneWeekdays, fStandaloneWeekdaysCount, other.fStandaloneWeekdays, other.fStandaloneWeekdaysCount);
    assignArray(fStandaloneShortWeekdays, fStandaloneShortWeekdaysCount, other.fStandaloneShortWeekdays, other.fStandaloneShortWeekdaysCount);
    assignArray(fStandaloneShorterWeekdays, fStandaloneShorterWeekdaysCount, other.fStandaloneShorterWeekdays, other.fStandaloneShorterWeekdaysCount);
    assignArray(fStandaloneNarrowWeekdays, fStandaloneNarrowWeekdaysCount, other.fStandaloneNarrowWeekdays, other.fStandaloneNarrowWeekdaysCount);
    assignArray(fAmPms, fAmPmsCount, other.fAmPms, other.fAmPmsCount);
    assignArray(fNarrowAmPms, fNarrowAmPmsCount, other.fNarrowAmPms, other.fNarrowAmPmsCount );
    fTimeSeparator.fastCopyFrom(other.fTimeSeparator);  // fastCopyFrom() - see assignArray comments
    assignArray(fQuarters, fQuartersCount, other.fQuarters, other.fQuartersCount);
    assignArray(fShortQuarters, fShortQuartersCount, other.fShortQuarters, other.fShortQuartersCount);
    assignArray(fNarrowQuarters, fNarrowQuartersCount, other.fNarrowQuarters, other.fNarrowQuartersCount);
    assignArray(fStandaloneQuarters, fStandaloneQuartersCount, other.fStandaloneQuarters, other.fStandaloneQuartersCount);
    assignArray(fStandaloneShortQuarters, fStandaloneShortQuartersCount, other.fStandaloneShortQuarters, other.fStandaloneShortQuartersCount);
    assignArray(fStandaloneNarrowQuarters, fStandaloneNarrowQuartersCount, other.fStandaloneNarrowQuarters, other.fStandaloneNarrowQuartersCount);
    assignArray(fWideDayPeriods, fWideDayPeriodsCount,
                other.fWideDayPeriods, other.fWideDayPeriodsCount);
    assignArray(fNarrowDayPeriods, fNarrowDayPeriodsCount,
                other.fNarrowDayPeriods, other.fNarrowDayPeriodsCount);
    assignArray(fAbbreviatedDayPeriods, fAbbreviatedDayPeriodsCount,
                other.fAbbreviatedDayPeriods, other.fAbbreviatedDayPeriodsCount);
    assignArray(fStandaloneWideDayPeriods, fStandaloneWideDayPeriodsCount,
                other.fStandaloneWideDayPeriods, other.fStandaloneWideDayPeriodsCount);
    assignArray(fStandaloneNarrowDayPeriods, fStandaloneNarrowDayPeriodsCount,
                other.fStandaloneNarrowDayPeriods, other.fStandaloneNarrowDayPeriodsCount);
    assignArray(fStandaloneAbbreviatedDayPeriods, fStandaloneAbbreviatedDayPeriodsCount,
                other.fStandaloneAbbreviatedDayPeriods, other.fStandaloneAbbreviatedDayPeriodsCount);
    if (other.fLeapMonthPatterns != nullptr) {
        assignArray(fLeapMonthPatterns, fLeapMonthPatternsCount, other.fLeapMonthPatterns, other.fLeapMonthPatternsCount);
    } else {
        fLeapMonthPatterns = nullptr;
        fLeapMonthPatternsCount = 0;
    }
    if (other.fShortYearNames != nullptr) {
        assignArray(fShortYearNames, fShortYearNamesCount, other.fShortYearNames, other.fShortYearNamesCount);
    } else {
        fShortYearNames = nullptr;
        fShortYearNamesCount = 0;
    }
    if (other.fShortZodiacNames != nullptr) {
        assignArray(fShortZodiacNames, fShortZodiacNamesCount, other.fShortZodiacNames, other.fShortZodiacNamesCount);
    } else {
        fShortZodiacNames = nullptr;
        fShortZodiacNamesCount = 0;
    }

    if (other.fZoneStrings != nullptr) {
        fZoneStringsColCount = other.fZoneStringsColCount;
        fZoneStringsRowCount = other.fZoneStringsRowCount;
        createZoneStrings((const UnicodeString**)other.fZoneStrings);

    } else {
        fZoneStrings = nullptr;
        fZoneStringsColCount = 0;
        fZoneStringsRowCount = 0;
    }
    fZSFLocale = other.fZSFLocale;
    // Other zone strings data is created on demand
    fLocaleZoneStrings = nullptr;

    // fastCopyFrom() - see assignArray comments
    fLocalPatternChars.fastCopyFrom(other.fLocalPatternChars);

    uprv_memcpy(fCapitalization, other.fCapitalization, sizeof(fCapitalization));
}

/**
 * Assignment operator.
 */
DateFormatSymbols& DateFormatSymbols::operator=(const DateFormatSymbols& other)
{
    if (this == &other) { return *this; }  // self-assignment: no-op
    dispose();
    copyData(other);

    return *this;
}

DateFormatSymbols::~DateFormatSymbols()
{
    dispose();
}

void DateFormatSymbols::dispose()
{
    delete[] fEras;
    delete[] fEraNames;
    delete[] fNarrowEras;
    delete[] fMonths;
    delete[] fShortMonths;
    delete[] fNarrowMonths;
    delete[] fStandaloneMonths;
    delete[] fStandaloneShortMonths;
    delete[] fStandaloneNarrowMonths;
    delete[] fWeekdays;
    delete[] fShortWeekdays;
    delete[] fShorterWeekdays;
    delete[] fNarrowWeekdays;
    delete[] fStandaloneWeekdays;
    delete[] fStandaloneShortWeekdays;
    delete[] fStandaloneShorterWeekdays;
    delete[] fStandaloneNarrowWeekdays;
    delete[] fAmPms;
    delete[] fNarrowAmPms;
    delete[] fQuarters;
    delete[] fShortQuarters;
    delete[] fNarrowQuarters;
    delete[] fStandaloneQuarters;
    delete[] fStandaloneShortQuarters;
    delete[] fStandaloneNarrowQuarters;
    delete[] fLeapMonthPatterns;
    delete[] fShortYearNames;
    delete[] fShortZodiacNames;
    delete[] fAbbreviatedDayPeriods;
    delete[] fWideDayPeriods;
    delete[] fNarrowDayPeriods;
    delete[] fStandaloneAbbreviatedDayPeriods;
    delete[] fStandaloneWideDayPeriods;
    delete[] fStandaloneNarrowDayPeriods;

    disposeZoneStrings();
}

void DateFormatSymbols::disposeZoneStrings()
{
    if (fZoneStrings) {
        for (int32_t row = 0; row < fZoneStringsRowCount; ++row) {
            delete[] fZoneStrings[row];
        }
        uprv_free(fZoneStrings);
    }
    if (fLocaleZoneStrings) {
        for (int32_t row = 0; row < fZoneStringsRowCount; ++row) {
            delete[] fLocaleZoneStrings[row];
        }
        uprv_free(fLocaleZoneStrings);
    }

    fZoneStrings = nullptr;
    fLocaleZoneStrings = nullptr;
    fZoneStringsRowCount = 0;
    fZoneStringsColCount = 0;
}

UBool
DateFormatSymbols::arrayCompare(const UnicodeString* array1,
                                const UnicodeString* array2,
                                int32_t count)
{
    if (array1 == array2) return true;
    while (count>0)
    {
        --count;
        if (array1[count] != array2[count]) return false;
    }
    return true;
}

bool
DateFormatSymbols::operator==(const DateFormatSymbols& other) const
{
    // First do cheap comparisons
    if (this == &other) {
        return true;
    }
    if (fErasCount == other.fErasCount &&
        fEraNamesCount == other.fEraNamesCount &&
        fNarrowErasCount == other.fNarrowErasCount &&
        fMonthsCount == other.fMonthsCount &&
        fShortMonthsCount == other.fShortMonthsCount &&
        fNarrowMonthsCount == other.fNarrowMonthsCount &&
        fStandaloneMonthsCount == other.fStandaloneMonthsCount &&
        fStandaloneShortMonthsCount == other.fStandaloneShortMonthsCount &&
        fStandaloneNarrowMonthsCount == other.fStandaloneNarrowMonthsCount &&
        fWeekdaysCount == other.fWeekdaysCount &&
        fShortWeekdaysCount == other.fShortWeekdaysCount &&
        fShorterWeekdaysCount == other.fShorterWeekdaysCount &&
        fNarrowWeekdaysCount == other.fNarrowWeekdaysCount &&
        fStandaloneWeekdaysCount == other.fStandaloneWeekdaysCount &&
        fStandaloneShortWeekdaysCount == other.fStandaloneShortWeekdaysCount &&
        fStandaloneShorterWeekdaysCount == other.fStandaloneShorterWeekdaysCount &&
        fStandaloneNarrowWeekdaysCount == other.fStandaloneNarrowWeekdaysCount &&
        fAmPmsCount == other.fAmPmsCount &&
        fNarrowAmPmsCount == other.fNarrowAmPmsCount &&
        fQuartersCount == other.fQuartersCount &&
        fShortQuartersCount == other.fShortQuartersCount &&
        fNarrowQuartersCount == other.fNarrowQuartersCount &&
        fStandaloneQuartersCount == other.fStandaloneQuartersCount &&
        fStandaloneShortQuartersCount == other.fStandaloneShortQuartersCount &&
        fStandaloneNarrowQuartersCount == other.fStandaloneNarrowQuartersCount &&
        fLeapMonthPatternsCount == other.fLeapMonthPatternsCount &&
        fShortYearNamesCount == other.fShortYearNamesCount &&
        fShortZodiacNamesCount == other.fShortZodiacNamesCount &&
        fAbbreviatedDayPeriodsCount == other.fAbbreviatedDayPeriodsCount &&
        fWideDayPeriodsCount == other.fWideDayPeriodsCount &&
        fNarrowDayPeriodsCount == other.fNarrowDayPeriodsCount &&
        fStandaloneAbbreviatedDayPeriodsCount == other.fStandaloneAbbreviatedDayPeriodsCount &&
        fStandaloneWideDayPeriodsCount == other.fStandaloneWideDayPeriodsCount &&
        fStandaloneNarrowDayPeriodsCount == other.fStandaloneNarrowDayPeriodsCount &&
        (uprv_memcmp(fCapitalization, other.fCapitalization, sizeof(fCapitalization))==0))
    {
        // Now compare the arrays themselves
        if (arrayCompare(fEras, other.fEras, fErasCount) &&
            arrayCompare(fEraNames, other.fEraNames, fEraNamesCount) &&
            arrayCompare(fNarrowEras, other.fNarrowEras, fNarrowErasCount) &&
            arrayCompare(fMonths, other.fMonths, fMonthsCount) &&
            arrayCompare(fShortMonths, other.fShortMonths, fShortMonthsCount) &&
            arrayCompare(fNarrowMonths, other.fNarrowMonths, fNarrowMonthsCount) &&
            arrayCompare(fStandaloneMonths, other.fStandaloneMonths, fStandaloneMonthsCount) &&
            arrayCompare(fStandaloneShortMonths, other.fStandaloneShortMonths, fStandaloneShortMonthsCount) &&
            arrayCompare(fStandaloneNarrowMonths, other.fStandaloneNarrowMonths, fStandaloneNarrowMonthsCount) &&
            arrayCompare(fWeekdays, other.fWeekdays, fWeekdaysCount) &&
            arrayCompare(fShortWeekdays, other.fShortWeekdays, fShortWeekdaysCount) &&
            arrayCompare(fShorterWeekdays, other.fShorterWeekdays, fShorterWeekdaysCount) &&
            arrayCompare(fNarrowWeekdays, other.fNarrowWeekdays, fNarrowWeekdaysCount) &&
            arrayCompare(fStandaloneWeekdays, other.fStandaloneWeekdays, fStandaloneWeekdaysCount) &&
            arrayCompare(fStandaloneShortWeekdays, other.fStandaloneShortWeekdays, fStandaloneShortWeekdaysCount) &&
            arrayCompare(fStandaloneShorterWeekdays, other.fStandaloneShorterWeekdays, fStandaloneShorterWeekdaysCount) &&
            arrayCompare(fStandaloneNarrowWeekdays, other.fStandaloneNarrowWeekdays, fStandaloneNarrowWeekdaysCount) &&
            arrayCompare(fAmPms, other.fAmPms, fAmPmsCount) &&
            arrayCompare(fNarrowAmPms, other.fNarrowAmPms, fNarrowAmPmsCount) &&
            fTimeSeparator == other.fTimeSeparator &&
            arrayCompare(fQuarters, other.fQuarters, fQuartersCount) &&
            arrayCompare(fShortQuarters, other.fShortQuarters, fShortQuartersCount) &&
            arrayCompare(fNarrowQuarters, other.fNarrowQuarters, fNarrowQuartersCount) &&
            arrayCompare(fStandaloneQuarters, other.fStandaloneQuarters, fStandaloneQuartersCount) &&
            arrayCompare(fStandaloneShortQuarters, other.fStandaloneShortQuarters, fStandaloneShortQuartersCount) &&
            arrayCompare(fStandaloneNarrowQuarters, other.fStandaloneNarrowQuarters, fStandaloneNarrowQuartersCount) &&
            arrayCompare(fLeapMonthPatterns, other.fLeapMonthPatterns, fLeapMonthPatternsCount) &&
            arrayCompare(fShortYearNames, other.fShortYearNames, fShortYearNamesCount) &&
            arrayCompare(fShortZodiacNames, other.fShortZodiacNames, fShortZodiacNamesCount) &&
            arrayCompare(fAbbreviatedDayPeriods, other.fAbbreviatedDayPeriods, fAbbreviatedDayPeriodsCount) &&
            arrayCompare(fWideDayPeriods, other.fWideDayPeriods, fWideDayPeriodsCount) &&
            arrayCompare(fNarrowDayPeriods, other.fNarrowDayPeriods, fNarrowDayPeriodsCount) &&
            arrayCompare(fStandaloneAbbreviatedDayPeriods, other.fStandaloneAbbreviatedDayPeriods,
                         fStandaloneAbbreviatedDayPeriodsCount) &&
            arrayCompare(fStandaloneWideDayPeriods, other.fStandaloneWideDayPeriods,
                         fStandaloneWideDayPeriodsCount) &&
            arrayCompare(fStandaloneNarrowDayPeriods, other.fStandaloneNarrowDayPeriods,
                         fStandaloneWideDayPeriodsCount))
        {
            // Compare the contents of fZoneStrings
            if (fZoneStrings == nullptr && other.fZoneStrings == nullptr) {
                if (fZSFLocale == other.fZSFLocale) {
                    return true;
                }
            } else if (fZoneStrings != nullptr && other.fZoneStrings != nullptr) {
                if (fZoneStringsRowCount == other.fZoneStringsRowCount
                    && fZoneStringsColCount == other.fZoneStringsColCount) {
                    bool cmpres = true;
                    for (int32_t i = 0; (i < fZoneStringsRowCount) && cmpres; i++) {
                        cmpres = arrayCompare(fZoneStrings[i], other.fZoneStrings[i], fZoneStringsColCount);
                    }
                    return cmpres;
                }
            }
            return false;
        }
    }
    return false;
}

//------------------------------------------------------

const UnicodeString*
DateFormatSymbols::getEras(int32_t &count) const
{
    count = fErasCount;
    return fEras;
}

const UnicodeString*
DateFormatSymbols::getEraNames(int32_t &count) const
{
    count = fEraNamesCount;
    return fEraNames;
}

const UnicodeString*
DateFormatSymbols::getNarrowEras(int32_t &count) const
{
    count = fNarrowErasCount;
    return fNarrowEras;
}

const UnicodeString*
DateFormatSymbols::getMonths(int32_t &count) const
{
    count = fMonthsCount;
    return fMonths;
}

const UnicodeString*
DateFormatSymbols::getShortMonths(int32_t &count) const
{
    count = fShortMonthsCount;
    return fShortMonths;
}

const UnicodeString*
DateFormatSymbols::getMonths(int32_t &count, DtContextType context, DtWidthType width ) const
{
    UnicodeString *returnValue = nullptr;

    switch (context) {
    case FORMAT :
        switch(width) {
        case WIDE :
            count = fMonthsCount;
            returnValue = fMonths;
            break;
        case ABBREVIATED :
        case SHORT : // no month data for this, defaults to ABBREVIATED
            count = fShortMonthsCount;
            returnValue = fShortMonths;
            break;
        case NARROW :
            count = fNarrowMonthsCount;
            returnValue = fNarrowMonths;
            break;
        case DT_WIDTH_COUNT :
            break;
        }
        break;
    case STANDALONE :
        switch(width) {
        case WIDE :
            count = fStandaloneMonthsCount;
            returnValue = fStandaloneMonths;
            break;
        case ABBREVIATED :
        case SHORT : // no month data for this, defaults to ABBREVIATED
            count = fStandaloneShortMonthsCount;
            returnValue = fStandaloneShortMonths;
            break;
        case NARROW :
            count = fStandaloneNarrowMonthsCount;
            returnValue = fStandaloneNarrowMonths;
            break;
        case DT_WIDTH_COUNT :
            break;
        }
        break;
    case DT_CONTEXT_COUNT :
        break;
    }
    return returnValue;
}

const UnicodeString*
DateFormatSymbols::getWeekdays(int32_t &count) const
{
    count = fWeekdaysCount;
    return fWeekdays;
}

const UnicodeString*
DateFormatSymbols::getShortWeekdays(int32_t &count) const
{
    count = fShortWeekdaysCount;
    return fShortWeekdays;
}

const UnicodeString*
DateFormatSymbols::getWeekdays(int32_t &count, DtContextType context, DtWidthType width) const
{
    UnicodeString *returnValue = nullptr;
    switch (context) {
    case FORMAT :
        switch(width) {
            case WIDE :
                count = fWeekdaysCount;
                returnValue = fWeekdays;
                break;
            case ABBREVIATED :
                count = fShortWeekdaysCount;
                returnValue = fShortWeekdays;
                break;
            case SHORT :
                count = fShorterWeekdaysCount;
                returnValue = fShorterWeekdays;
                break;
            case NARROW :
                count = fNarrowWeekdaysCount;
                returnValue = fNarrowWeekdays;
                break;
            case DT_WIDTH_COUNT :
                break;
        }
        break;
    case STANDALONE :
        switch(width) {
            case WIDE :
                count = fStandaloneWeekdaysCount;
                returnValue = fStandaloneWeekdays;
                break;
            case ABBREVIATED :
                count = fStandaloneShortWeekdaysCount;
                returnValue = fStandaloneShortWeekdays;
                break;
            case SHORT :
                count = fStandaloneShorterWeekdaysCount;
                returnValue = fStandaloneShorterWeekdays;
                break;
            case NARROW :
                count = fStandaloneNarrowWeekdaysCount;
                returnValue = fStandaloneNarrowWeekdays;
                break;
            case DT_WIDTH_COUNT :
                break;
        }
        break;
    case DT_CONTEXT_COUNT :
        break;
    }
    return returnValue;
}

const UnicodeString*
DateFormatSymbols::getQuarters(int32_t &count, DtContextType context, DtWidthType width ) const
{
    UnicodeString *returnValue = nullptr;

    switch (context) {
    case FORMAT :
        switch(width) {
        case WIDE :
            count = fQuartersCount;
            returnValue = fQuarters;
            break;
        case ABBREVIATED :
        case SHORT : // no quarter data for this, defaults to ABBREVIATED
            count = fShortQuartersCount;
            returnValue = fShortQuarters;
            break;
        case NARROW :
            count = fNarrowQuartersCount;
            returnValue = fNarrowQuarters;
            break;
        case DT_WIDTH_COUNT :
            break;
        }
        break;
    case STANDALONE :
        switch(width) {
        case WIDE :
            count = fStandaloneQuartersCount;
            returnValue = fStandaloneQuarters;
            break;
        case ABBREVIATED :
        case SHORT : // no quarter data for this, defaults to ABBREVIATED
            count = fStandaloneShortQuartersCount;
            returnValue = fStandaloneShortQuarters;
            break;
        case NARROW :
            count = fStandaloneNarrowQuartersCount;
            returnValue = fStandaloneNarrowQuarters;
            break;
        case DT_WIDTH_COUNT :
            break;
        }
        break;
    case DT_CONTEXT_COUNT :
        break;
    }
    return returnValue;
}

UnicodeString&
DateFormatSymbols::getTimeSeparatorString(UnicodeString& result) const
{
    // fastCopyFrom() - see assignArray comments
    return result.fastCopyFrom(fTimeSeparator);
}

const UnicodeString*
DateFormatSymbols::getAmPmStrings(int32_t &count) const
{
    count = fAmPmsCount;
    return fAmPms;
}

const UnicodeString*
DateFormatSymbols::getLeapMonthPatterns(int32_t &count) const
{
    count = fLeapMonthPatternsCount;
    return fLeapMonthPatterns;
}

const UnicodeString*
DateFormatSymbols::getYearNames(int32_t& count,
                                DtContextType /*ignored*/, DtWidthType /*ignored*/) const
{
    count = fShortYearNamesCount;
    return fShortYearNames;
}

void
DateFormatSymbols::setYearNames(const UnicodeString* yearNames, int32_t count,
                                DtContextType context, DtWidthType width)
{
    if (context == FORMAT && width == ABBREVIATED) {
        delete[] fShortYearNames;
        fShortYearNames = newUnicodeStringArray(count);
        uprv_arrayCopy(yearNames, fShortYearNames, count);
        fShortYearNamesCount = count;
    }
}

const UnicodeString*
DateFormatSymbols::getZodiacNames(int32_t& count,
                                DtContextType /*ignored*/, DtWidthType /*ignored*/) const
{
    count = fShortZodiacNamesCount;
    return fShortZodiacNames;
}

void
DateFormatSymbols::setZodiacNames(const UnicodeString* zodiacNames, int32_t count,
                                DtContextType context, DtWidthType width)
{
    if (context == FORMAT && width == ABBREVIATED) {
        delete[] fShortZodiacNames;
        fShortZodiacNames = newUnicodeStringArray(count);
        uprv_arrayCopy(zodiacNames, fShortZodiacNames, count);
        fShortZodiacNamesCount = count;
    }
}

//------------------------------------------------------

void
DateFormatSymbols::setEras(const UnicodeString* erasArray, int32_t count)
{
    // delete the old list if we own it
    delete[] fEras;

    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)
    fEras = newUnicodeStringArray(count);
    uprv_arrayCopy(erasArray,fEras,  count);
    fErasCount = count;
}

void
DateFormatSymbols::setEraNames(const UnicodeString* eraNamesArray, int32_t count)
{
    // delete the old list if we own it
    delete[] fEraNames;

    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)
    fEraNames = newUnicodeStringArray(count);
    uprv_arrayCopy(eraNamesArray,fEraNames,  count);
    fEraNamesCount = count;
}

void
DateFormatSymbols::setNarrowEras(const UnicodeString* narrowErasArray, int32_t count)
{
    // delete the old list if we own it
    delete[] fNarrowEras;

    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)
    fNarrowEras = newUnicodeStringArray(count);
    uprv_arrayCopy(narrowErasArray,fNarrowEras,  count);
    fNarrowErasCount = count;
}

void
DateFormatSymbols::setMonths(const UnicodeString* monthsArray, int32_t count)
{
    // delete the old list if we own it
    delete[] fMonths;

    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)
    fMonths = newUnicodeStringArray(count);
    uprv_arrayCopy( monthsArray,fMonths,count);
    fMonthsCount = count;
}

void
DateFormatSymbols::setShortMonths(const UnicodeString* shortMonthsArray, int32_t count)
{
    // delete the old list if we own it
    delete[] fShortMonths;

    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)
    fShortMonths = newUnicodeStringArray(count);
    uprv_arrayCopy(shortMonthsArray,fShortMonths,  count);
    fShortMonthsCount = count;
}

void
DateFormatSymbols::setMonths(const UnicodeString* monthsArray, int32_t count, DtContextType context, DtWidthType width)
{
    // delete the old list if we own it
    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)

    switch (context) {
    case FORMAT :
        switch (width) {
        case WIDE :
            delete[] fMonths;
            fMonths = newUnicodeStringArray(count);
            uprv_arrayCopy( monthsArray,fMonths,count);
            fMonthsCount = count;
            break;
        case ABBREVIATED :
            delete[] fShortMonths;
            fShortMonths = newUnicodeStringArray(count);
            uprv_arrayCopy( monthsArray,fShortMonths,count);
            fShortMonthsCount = count;
            break;
        case NARROW :
            delete[] fNarrowMonths;
            fNarrowMonths = newUnicodeStringArray(count);
            uprv_arrayCopy( monthsArray,fNarrowMonths,count);
            fNarrowMonthsCount = count;
            break;
        default :
            break;
        }
        break;
    case STANDALONE :
        switch (width) {
        case WIDE :
            delete[] fStandaloneMonths;
            fStandaloneMonths = newUnicodeStringArray(count);
            uprv_arrayCopy( monthsArray,fStandaloneMonths,count);
            fStandaloneMonthsCount = count;
            break;
        case ABBREVIATED :
            delete[] fStandaloneShortMonths;
            fStandaloneShortMonths = newUnicodeStringArray(count);
            uprv_arrayCopy( monthsArray,fStandaloneShortMonths,count);
            fStandaloneShortMonthsCount = count;
            break;
        case NARROW :
            delete[] fStandaloneNarrowMonths;
            fStandaloneNarrowMonths = newUnicodeStringArray(count);
            uprv_arrayCopy( monthsArray,fStandaloneNarrowMonths,count);
            fStandaloneNarrowMonthsCount = count;
            break;
        default :
            break;
        }
        break;
    case DT_CONTEXT_COUNT :
        break;
    }
}

void DateFormatSymbols::setWeekdays(const UnicodeString* weekdaysArray, int32_t count)
{
    // delete the old list if we own it
    delete[] fWeekdays;

    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)
    fWeekdays = newUnicodeStringArray(count);
    uprv_arrayCopy(weekdaysArray,fWeekdays,count);
    fWeekdaysCount = count;
}

void
DateFormatSymbols::setShortWeekdays(const UnicodeString* shortWeekdaysArray, int32_t count)
{
    // delete the old list if we own it
    delete[] fShortWeekdays;

    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)
    fShortWeekdays = newUnicodeStringArray(count);
    uprv_arrayCopy(shortWeekdaysArray, fShortWeekdays, count);
    fShortWeekdaysCount = count;
}

void
DateFormatSymbols::setWeekdays(const UnicodeString* weekdaysArray, int32_t count, DtContextType context, DtWidthType width)
{
    // delete the old list if we own it
    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)

    switch (context) {
    case FORMAT :
        switch (width) {
        case WIDE :
            delete[] fWeekdays;
            fWeekdays = newUnicodeStringArray(count);
            uprv_arrayCopy(weekdaysArray, fWeekdays, count);
            fWeekdaysCount = count;
            break;
        case ABBREVIATED :
            delete[] fShortWeekdays;
            fShortWeekdays = newUnicodeStringArray(count);
            uprv_arrayCopy(weekdaysArray, fShortWeekdays, count);
            fShortWeekdaysCount = count;
            break;
        case SHORT :
            delete[] fShorterWeekdays;
            fShorterWeekdays = newUnicodeStringArray(count);
            uprv_arrayCopy(weekdaysArray, fShorterWeekdays, count);
            fShorterWeekdaysCount = count;
            break;
        case NARROW :
            delete[] fNarrowWeekdays;
            fNarrowWeekdays = newUnicodeStringArray(count);
            uprv_arrayCopy(weekdaysArray, fNarrowWeekdays, count);
            fNarrowWeekdaysCount = count;
            break;
        case DT_WIDTH_COUNT :
            break;
        }
        break;
    case STANDALONE :
        switch (width) {
        case WIDE :
            delete[] fStandaloneWeekdays;
            fStandaloneWeekdays = newUnicodeStringArray(count);
            uprv_arrayCopy(weekdaysArray, fStandaloneWeekdays, count);
            fStandaloneWeekdaysCount = count;
            break;
        case ABBREVIATED :
            delete[] fStandaloneShortWeekdays;
            fStandaloneShortWeekdays = newUnicodeStringArray(count);
            uprv_arrayCopy(weekdaysArray, fStandaloneShortWeekdays, count);
            fStandaloneShortWeekdaysCount = count;
            break;
        case SHORT :
            delete[] fStandaloneShorterWeekdays;
            fStandaloneShorterWeekdays = newUnicodeStringArray(count);
            uprv_arrayCopy(weekdaysArray, fStandaloneShorterWeekdays, count);
            fStandaloneShorterWeekdaysCount = count;
            break;
        case NARROW :
            delete[] fStandaloneNarrowWeekdays;
            fStandaloneNarrowWeekdays = newUnicodeStringArray(count);
            uprv_arrayCopy(weekdaysArray, fStandaloneNarrowWeekdays, count);
            fStandaloneNarrowWeekdaysCount = count;
            break;
        case DT_WIDTH_COUNT :
            break;
        }
        break;
    case DT_CONTEXT_COUNT :
        break;
    }
}

void
DateFormatSymbols::setQuarters(const UnicodeString* quartersArray, int32_t count, DtContextType context, DtWidthType width)
{
    // delete the old list if we own it
    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)

    switch (context) {
    case FORMAT :
        switch (width) {
        case WIDE :
            delete[] fQuarters;
            fQuarters = newUnicodeStringArray(count);
            uprv_arrayCopy( quartersArray,fQuarters,count);
            fQuartersCount = count;
            break;
        case ABBREVIATED :
            delete[] fShortQuarters;
            fShortQuarters = newUnicodeStringArray(count);
            uprv_arrayCopy( quartersArray,fShortQuarters,count);
            fShortQuartersCount = count;
            break;
        case NARROW :
            delete[] fNarrowQuarters;
            fNarrowQuarters = newUnicodeStringArray(count);
            uprv_arrayCopy( quartersArray,fNarrowQuarters,count);
            fNarrowQuartersCount = count;
            break;
        default :
            break;
        }
        break;
    case STANDALONE :
        switch (width) {
        case WIDE :
            delete[] fStandaloneQuarters;
            fStandaloneQuarters = newUnicodeStringArray(count);
            uprv_arrayCopy( quartersArray,fStandaloneQuarters,count);
            fStandaloneQuartersCount = count;
            break;
        case ABBREVIATED :
            delete[] fStandaloneShortQuarters;
            fStandaloneShortQuarters = newUnicodeStringArray(count);
            uprv_arrayCopy( quartersArray,fStandaloneShortQuarters,count);
            fStandaloneShortQuartersCount = count;
            break;
        case NARROW :
            delete[] fStandaloneNarrowQuarters;
            fStandaloneNarrowQuarters = newUnicodeStringArray(count);
            uprv_arrayCopy( quartersArray,fStandaloneNarrowQuarters,count);
            fStandaloneNarrowQuartersCount = count;
            break;
        default :
            break;
        }
        break;
    case DT_CONTEXT_COUNT :
        break;
    }
}

void
DateFormatSymbols::setAmPmStrings(const UnicodeString* amPmsArray, int32_t count)
{
    // delete the old list if we own it
    delete[] fAmPms;

    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)
    fAmPms = newUnicodeStringArray(count);
    uprv_arrayCopy(amPmsArray,fAmPms,count);
    fAmPmsCount = count;
}

void
DateFormatSymbols::setTimeSeparatorString(const UnicodeString& newTimeSeparator)
{
    fTimeSeparator = newTimeSeparator;
}

const UnicodeString**
DateFormatSymbols::getZoneStrings(int32_t& rowCount, int32_t& columnCount) const
{
    const UnicodeString **result = nullptr;
    static UMutex LOCK;

    umtx_lock(&LOCK);
    if (fZoneStrings == nullptr) {
        if (fLocaleZoneStrings == nullptr) {
            const_cast<DateFormatSymbols*>(this)->initZoneStringsArray();
        }
        result = (const UnicodeString**)fLocaleZoneStrings;
    } else {
        result = (const UnicodeString**)fZoneStrings;
    }
    rowCount = fZoneStringsRowCount;
    columnCount = fZoneStringsColCount;
    umtx_unlock(&LOCK);

    return result;
}

// For now, we include all zones
#define ZONE_SET UCAL_ZONE_TYPE_ANY

// This code must be called within a synchronized block
void
DateFormatSymbols::initZoneStringsArray() {
    if (fZoneStrings != nullptr || fLocaleZoneStrings != nullptr) {
        return;
    }

    UErrorCode status = U_ZERO_ERROR;

    StringEnumeration *tzids = nullptr;
    UnicodeString ** zarray = nullptr;
    TimeZoneNames *tzNames = nullptr;
    int32_t rows = 0;

    static const UTimeZoneNameType TYPES[] = {
        UTZNM_LONG_STANDARD, UTZNM_SHORT_STANDARD,
        UTZNM_LONG_DAYLIGHT, UTZNM_SHORT_DAYLIGHT
    };
    static const int32_t NUM_TYPES = 4;

    do { // dummy do-while

        tzids = TimeZone::createTimeZoneIDEnumeration(ZONE_SET, nullptr, nullptr, status);
        rows = tzids->count(status);
        if (U_FAILURE(status)) {
            break;
        }

        // Allocate array
        int32_t size = rows * sizeof(UnicodeString*);
        zarray = static_cast<UnicodeString**>(uprv_malloc(size));
        if (zarray == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            break;
        }
        uprv_memset(zarray, 0, size);

        tzNames = TimeZoneNames::createInstance(fZSFLocale, status);
        tzNames->loadAllDisplayNames(status);
        if (U_FAILURE(status)) { break; }

        const UnicodeString *tzid;
        int32_t i = 0;
        UDate now = Calendar::getNow();
        UnicodeString tzDispName;

        while ((tzid = tzids->snext(status)) != nullptr) {
            if (U_FAILURE(status)) {
                break;
            }

            zarray[i] = new UnicodeString[5];
            if (zarray[i] == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                break;
            }

            zarray[i][0].setTo(*tzid);
            tzNames->getDisplayNames(*tzid, TYPES, NUM_TYPES, now, zarray[i]+1, status);
            i++;
        }

    } while (false);

    if (U_FAILURE(status)) {
        if (zarray) {
            for (int32_t i = 0; i < rows; i++) {
                if (zarray[i]) {
                    delete[] zarray[i];
                }
            }
            uprv_free(zarray);
            zarray = nullptr;
        }
    }

    delete tzNames;
    delete tzids;

    fLocaleZoneStrings = zarray;
    fZoneStringsRowCount = rows;
    fZoneStringsColCount = 1 + NUM_TYPES;
}

void
DateFormatSymbols::setZoneStrings(const UnicodeString* const *strings, int32_t rowCount, int32_t columnCount)
{
    // since deleting a 2-d array is a pain in the butt, we offload that task to
    // a separate function
    disposeZoneStrings();
    // we always own the new list, which we create here (we duplicate rather
    // than adopting the list passed in)
    fZoneStringsRowCount = rowCount;
    fZoneStringsColCount = columnCount;
    createZoneStrings(const_cast<const UnicodeString**>(strings));
}

//------------------------------------------------------

const char16_t * U_EXPORT2
DateFormatSymbols::getPatternUChars()
{
    return gPatternChars;
}

UDateFormatField U_EXPORT2
DateFormatSymbols::getPatternCharIndex(char16_t c) {
    if (c >= UPRV_LENGTHOF(gLookupPatternChars)) {
        return UDAT_FIELD_COUNT;
    }
    const auto idx = gLookupPatternChars[c];
    return idx == -1 ? UDAT_FIELD_COUNT : static_cast<UDateFormatField>(idx);
}

static const uint64_t kNumericFieldsAlways =
    (static_cast<uint64_t>(1) << UDAT_YEAR_FIELD) |                 // y
    (static_cast<uint64_t>(1) << UDAT_DATE_FIELD) |                 // d
    (static_cast<uint64_t>(1) << UDAT_HOUR_OF_DAY1_FIELD) |         // k
    (static_cast<uint64_t>(1) << UDAT_HOUR_OF_DAY0_FIELD) |         // H
    (static_cast<uint64_t>(1) << UDAT_MINUTE_FIELD) |               // m
    (static_cast<uint64_t>(1) << UDAT_SECOND_FIELD) |               // s
    (static_cast<uint64_t>(1) << UDAT_FRACTIONAL_SECOND_FIELD) |    // S
    (static_cast<uint64_t>(1) << UDAT_DAY_OF_YEAR_FIELD) |          // D
    (static_cast<uint64_t>(1) << UDAT_DAY_OF_WEEK_IN_MONTH_FIELD) | // F
    (static_cast<uint64_t>(1) << UDAT_WEEK_OF_YEAR_FIELD) |         // w
    (static_cast<uint64_t>(1) << UDAT_WEEK_OF_MONTH_FIELD) |        // W
    (static_cast<uint64_t>(1) << UDAT_HOUR1_FIELD) |                // h
    (static_cast<uint64_t>(1) << UDAT_HOUR0_FIELD) |                // K
    (static_cast<uint64_t>(1) << UDAT_YEAR_WOY_FIELD) |             // Y
    (static_cast<uint64_t>(1) << UDAT_EXTENDED_YEAR_FIELD) |        // u
    (static_cast<uint64_t>(1) << UDAT_JULIAN_DAY_FIELD) |           // g
    (static_cast<uint64_t>(1) << UDAT_MILLISECONDS_IN_DAY_FIELD) |  // A
    (static_cast<uint64_t>(1) << UDAT_RELATED_YEAR_FIELD);          // r

static const uint64_t kNumericFieldsForCount12 =
    (static_cast<uint64_t>(1) << UDAT_MONTH_FIELD) |             // M or MM
    (static_cast<uint64_t>(1) << UDAT_DOW_LOCAL_FIELD) |         // e or ee
    (static_cast<uint64_t>(1) << UDAT_STANDALONE_DAY_FIELD) |    // c or cc
    (static_cast<uint64_t>(1) << UDAT_STANDALONE_MONTH_FIELD) |  // L or LL
    (static_cast<uint64_t>(1) << UDAT_QUARTER_FIELD) |           // Q or QQ
    (static_cast<uint64_t>(1) << UDAT_STANDALONE_QUARTER_FIELD); // q or qq

UBool U_EXPORT2
DateFormatSymbols::isNumericField(UDateFormatField f, int32_t count) {
    if (f == UDAT_FIELD_COUNT) {
        return false;
    }
    uint64_t flag = static_cast<uint64_t>(1) << f;
    return ((kNumericFieldsAlways & flag) != 0 || ((kNumericFieldsForCount12 & flag) != 0 && count < 3));
}

UBool U_EXPORT2
DateFormatSymbols::isNumericPatternChar(char16_t c, int32_t count) {
    return isNumericField(getPatternCharIndex(c), count);
}

//------------------------------------------------------

UnicodeString&
DateFormatSymbols::getLocalPatternChars(UnicodeString& result) const
{
    // fastCopyFrom() - see assignArray comments
    return result.fastCopyFrom(fLocalPatternChars);
}

//------------------------------------------------------

void
DateFormatSymbols::setLocalPatternChars(const UnicodeString& newLocalPatternChars)
{
    fLocalPatternChars = newLocalPatternChars;
}

//------------------------------------------------------

namespace {

// Constants declarations
const char16_t kCalendarAliasPrefixUChar[] = {
    SOLIDUS, CAP_L, CAP_O, CAP_C, CAP_A, CAP_L, CAP_E, SOLIDUS,
    LOW_C, LOW_A, LOW_L, LOW_E, LOW_N, LOW_D, LOW_A, LOW_R, SOLIDUS
};
const char16_t kGregorianTagUChar[] = {
    LOW_G, LOW_R, LOW_E, LOW_G, LOW_O, LOW_R, LOW_I, LOW_A, LOW_N
};
const char16_t kVariantTagUChar[] = {
    PERCENT, LOW_V, LOW_A, LOW_R, LOW_I, LOW_A, LOW_N, LOW_T
};
const char16_t kLeapTagUChar[] = {
    LOW_L, LOW_E, LOW_A, LOW_P
};
const char16_t kCyclicNameSetsTagUChar[] = {
    LOW_C, LOW_Y, LOW_C, LOW_L, LOW_I, LOW_C, CAP_N, LOW_A, LOW_M, LOW_E, CAP_S, LOW_E, LOW_T, LOW_S
};
const char16_t kYearsTagUChar[] = {
    SOLIDUS, LOW_Y, LOW_E, LOW_A, LOW_R, LOW_S
};
const char16_t kZodiacsUChar[] = {
    SOLIDUS, LOW_Z, LOW_O, LOW_D, LOW_I, LOW_A, LOW_C, LOW_S
};
const char16_t kDayPartsTagUChar[] = {
    SOLIDUS, LOW_D, LOW_A, LOW_Y, CAP_P, LOW_A, LOW_R, LOW_T, LOW_S
};
const char16_t kFormatTagUChar[] = {
    SOLIDUS, LOW_F, LOW_O, LOW_R, LOW_M, LOW_A, LOW_T
};
const char16_t kAbbrTagUChar[] = {
    SOLIDUS, LOW_A, LOW_B, LOW_B, LOW_R, LOW_E, LOW_V, LOW_I, LOW_A, LOW_T, LOW_E, LOW_D
};

// ResourceSink to enumerate all calendar resources
struct CalendarDataSink : public ResourceSink {

    // Enum which specifies the type of alias received, or no alias
    enum AliasType {
        SAME_CALENDAR,
        DIFFERENT_CALENDAR,
        GREGORIAN,
        NONE
    };

    // Data structures to store resources from the current resource bundle
    Hashtable arrays;
    Hashtable arraySizes;
    Hashtable maps;
    /** 
     * Whenever there are aliases, the same object will be added twice to 'map'.
     * To avoid double deletion, 'maps' won't take ownership of the objects. Instead,
     * 'mapRefs' will own them and will delete them when CalendarDataSink is deleted.
     */
    MemoryPool<Hashtable> mapRefs;

    // Paths and the aliases they point to
    UVector aliasPathPairs;

    // Current and next calendar resource table which should be loaded
    UnicodeString currentCalendarType;
    UnicodeString nextCalendarType;

    // Resources to visit when enumerating fallback calendars
    LocalPointer<UVector> resourcesToVisit;

    // Alias' relative path populated whenever an alias is read
    UnicodeString aliasRelativePath;

    // Initializes CalendarDataSink with default values
    CalendarDataSink(UErrorCode& status)
    :   arrays(false, status), arraySizes(false, status), maps(false, status),
        mapRefs(),
        aliasPathPairs(uprv_deleteUObject, uhash_compareUnicodeString, status),
        currentCalendarType(), nextCalendarType(),
        resourcesToVisit(nullptr), aliasRelativePath() {
        if (U_FAILURE(status)) { return; }
    }
    virtual ~CalendarDataSink();

    // Configure the CalendarSink to visit all the resources
    void visitAllResources() {
        resourcesToVisit.adoptInstead(nullptr);
    }

    // Actions to be done before enumerating
    void preEnumerate(const UnicodeString &calendarType) {
        currentCalendarType = calendarType;
        nextCalendarType.setToBogus();
        aliasPathPairs.removeAllElements();
    }

    virtual void put(const char *key, ResourceValue &value, UBool, UErrorCode &errorCode) override {
        if (U_FAILURE(errorCode)) { return; }
        U_ASSERT(!currentCalendarType.isEmpty());

        // Stores the resources to visit on the next calendar.
        LocalPointer<UVector> resourcesToVisitNext(nullptr);
        ResourceTable calendarData = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }

        // Enumerate all resources for this calendar
        for (int i = 0; calendarData.getKeyAndValue(i, key, value); i++) {
            UnicodeString keyUString(key, -1, US_INV);

            // == Handle aliases ==
            AliasType aliasType = processAliasFromValue(keyUString, value, errorCode);
            if (U_FAILURE(errorCode)) { return; }
            if (aliasType == GREGORIAN) {
                // Ignore aliases to the gregorian calendar, all of its resources will be loaded anyway.
                continue;

            } else if (aliasType == DIFFERENT_CALENDAR) {
                // Whenever an alias to the next calendar (except gregorian) is encountered, register the
                // calendar type it's pointing to
                if (resourcesToVisitNext.isNull()) {
                    resourcesToVisitNext
                        .adoptInsteadAndCheckErrorCode(new UVector(uprv_deleteUObject, uhash_compareUnicodeString, errorCode),
                                                       errorCode);
                    if (U_FAILURE(errorCode)) { return; }
                }
                LocalPointer<UnicodeString> aliasRelativePathCopy(aliasRelativePath.clone(), errorCode);
                resourcesToVisitNext->adoptElement(aliasRelativePathCopy.orphan(), errorCode);
                if (U_FAILURE(errorCode)) { return; }
                continue;

            } else if (aliasType == SAME_CALENDAR) {
                // Register same-calendar alias
                if (arrays.get(aliasRelativePath) == nullptr && maps.get(aliasRelativePath) == nullptr) {
                    LocalPointer<UnicodeString> aliasRelativePathCopy(aliasRelativePath.clone(), errorCode);
                    aliasPathPairs.adoptElement(aliasRelativePathCopy.orphan(), errorCode);
                    if (U_FAILURE(errorCode)) { return; }
                    LocalPointer<UnicodeString> keyUStringCopy(keyUString.clone(), errorCode);
                    aliasPathPairs.adoptElement(keyUStringCopy.orphan(), errorCode);
                    if (U_FAILURE(errorCode)) { return; }
                }
                continue;
            }

            // Only visit the resources that were referenced by an alias on the previous calendar
            // (AmPmMarkersAbbr is an exception).
            if (!resourcesToVisit.isNull() && !resourcesToVisit->isEmpty() && !resourcesToVisit->contains(&keyUString)
                && uprv_strcmp(key, gAmPmMarkersAbbrTag) != 0) { continue; }

            // == Handle data ==
            if (uprv_strcmp(key, gAmPmMarkersTag) == 0
                || uprv_strcmp(key, gAmPmMarkersAbbrTag) == 0
                || uprv_strcmp(key, gAmPmMarkersNarrowTag) == 0) {
                if (arrays.get(keyUString) == nullptr) {
                    ResourceArray resourceArray = value.getArray(errorCode);
                    int32_t arraySize = resourceArray.getSize();
                    LocalArray<UnicodeString> stringArray(new UnicodeString[arraySize], errorCode);
                    value.getStringArray(stringArray.getAlias(), arraySize, errorCode);
                    arrays.put(keyUString, stringArray.orphan(), errorCode);
                    arraySizes.puti(keyUString, arraySize, errorCode);
                    if (U_FAILURE(errorCode)) { return; }
                }
            } else if (uprv_strcmp(key, gErasTag) == 0
                       || uprv_strcmp(key, gDayNamesTag) == 0
                       || uprv_strcmp(key, gMonthNamesTag) == 0
                       || uprv_strcmp(key, gQuartersTag) == 0
                       || uprv_strcmp(key, gDayPeriodTag) == 0
                       || uprv_strcmp(key, gMonthPatternsTag) == 0
                       || uprv_strcmp(key, gCyclicNameSetsTag) == 0) {
                processResource(keyUString, key, value, errorCode);
            }
        }

        // Apply same-calendar aliases
        UBool modified;
        do {
            modified = false;
            for (int32_t i = 0; i < aliasPathPairs.size();) {
                UBool mod = false;
                UnicodeString* alias = static_cast<UnicodeString*>(aliasPathPairs[i]);
                UnicodeString *aliasArray;
                Hashtable *aliasMap;
                if ((aliasArray = static_cast<UnicodeString*>(arrays.get(*alias))) != nullptr) {
                    UnicodeString* path = static_cast<UnicodeString*>(aliasPathPairs[i + 1]);
                    if (arrays.get(*path) == nullptr) {
                        // Clone the array
                        int32_t aliasArraySize = arraySizes.geti(*alias);
                        LocalArray<UnicodeString> aliasArrayCopy(new UnicodeString[aliasArraySize], errorCode);
                        if (U_FAILURE(errorCode)) { return; }
                        uprv_arrayCopy(aliasArray, aliasArrayCopy.getAlias(), aliasArraySize);
                        // Put the array on the 'arrays' map
                        arrays.put(*path, aliasArrayCopy.orphan(), errorCode);
                        arraySizes.puti(*path, aliasArraySize, errorCode);
                    }
                    if (U_FAILURE(errorCode)) { return; }
                    mod = true;
                } else if ((aliasMap = static_cast<Hashtable*>(maps.get(*alias))) != nullptr) {
                    UnicodeString* path = static_cast<UnicodeString*>(aliasPathPairs[i + 1]);
                    if (maps.get(*path) == nullptr) {
                        maps.put(*path, aliasMap, errorCode);
                    }
                    if (U_FAILURE(errorCode)) { return; }
                    mod = true;
                }
                if (mod) {
                    aliasPathPairs.removeElementAt(i + 1);
                    aliasPathPairs.removeElementAt(i);
                    modified = true;
                } else {
                    i += 2;
                }
            }
        } while (modified && !aliasPathPairs.isEmpty());

        // Set the resources to visit on the next calendar
        if (!resourcesToVisitNext.isNull()) {
            resourcesToVisit = std::move(resourcesToVisitNext);
        }
    }

    // Process the nested resource bundle tables
    void processResource(UnicodeString &path, const char *key, ResourceValue &value, UErrorCode &errorCode) {
        if (U_FAILURE(errorCode)) return;

        ResourceTable table = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) return;
        Hashtable* stringMap = nullptr;

        // Iterate over all the elements of the table and add them to the map
        for (int i = 0; table.getKeyAndValue(i, key, value); i++) {
            UnicodeString keyUString(key, -1, US_INV);

            // Ignore '%variant' keys
            if (keyUString.endsWith(kVariantTagUChar, UPRV_LENGTHOF(kVariantTagUChar))) {
                continue;
            }

            // == Handle String elements ==
            if (value.getType() == URES_STRING) {
                // We are on a leaf, store the map elements into the stringMap
                if (i == 0) {
                    // mapRefs will keep ownership of 'stringMap':
                    stringMap = mapRefs.create(false, errorCode);
                    if (stringMap == nullptr) {
                        errorCode = U_MEMORY_ALLOCATION_ERROR;
                        return;
                    }
                    maps.put(path, stringMap, errorCode);
                    if (U_FAILURE(errorCode)) { return; }
                    stringMap->setValueDeleter(uprv_deleteUObject);
                }
                U_ASSERT(stringMap != nullptr);
                int32_t valueStringSize;
                const char16_t *valueString = value.getString(valueStringSize, errorCode);
                if (U_FAILURE(errorCode)) { return; }
                LocalPointer<UnicodeString> valueUString(new UnicodeString(true, valueString, valueStringSize), errorCode);
                stringMap->put(keyUString, valueUString.orphan(), errorCode);
                if (U_FAILURE(errorCode)) { return; }
                continue;
            }
            U_ASSERT(stringMap == nullptr);

            // Store the current path's length and append the current key to the path.
            int32_t pathLength = path.length();
            path.append(SOLIDUS).append(keyUString);

            // In cyclicNameSets ignore everything but years/format/abbreviated
            // and zodiacs/format/abbreviated
            if (path.startsWith(kCyclicNameSetsTagUChar, UPRV_LENGTHOF(kCyclicNameSetsTagUChar))) {
                UBool skip = true;
                int32_t startIndex = UPRV_LENGTHOF(kCyclicNameSetsTagUChar);
                int32_t length = 0;
                if (startIndex == path.length()
                    || path.compare(startIndex, (length = UPRV_LENGTHOF(kZodiacsUChar)), kZodiacsUChar, 0, UPRV_LENGTHOF(kZodiacsUChar)) == 0
                    || path.compare(startIndex, (length = UPRV_LENGTHOF(kYearsTagUChar)), kYearsTagUChar, 0, UPRV_LENGTHOF(kYearsTagUChar)) == 0
                    || path.compare(startIndex, (length = UPRV_LENGTHOF(kDayPartsTagUChar)), kDayPartsTagUChar, 0, UPRV_LENGTHOF(kDayPartsTagUChar)) == 0) {
                    startIndex += length;
                    length = 0;
                    if (startIndex == path.length()
                        || path.compare(startIndex, (length = UPRV_LENGTHOF(kFormatTagUChar)), kFormatTagUChar, 0, UPRV_LENGTHOF(kFormatTagUChar)) == 0) {
                        startIndex += length;
                        length = 0;
                        if (startIndex == path.length()
                            || path.compare(startIndex, (length = UPRV_LENGTHOF(kAbbrTagUChar)), kAbbrTagUChar, 0, UPRV_LENGTHOF(kAbbrTagUChar)) == 0) {
                            skip = false;
                        }
                    }
                }
                if (skip) {
                    // Drop the latest key on the path and continue
                    path.retainBetween(0, pathLength);
                    continue;
                }
            }

            // == Handle aliases ==
            if (arrays.get(path) != nullptr || maps.get(path) != nullptr) {
                // Drop the latest key on the path and continue
                path.retainBetween(0, pathLength);
                continue;
            }

            AliasType aliasType = processAliasFromValue(path, value, errorCode);
            if (U_FAILURE(errorCode)) { return; }
            if (aliasType == SAME_CALENDAR) {
                // Store the alias path and the current path on aliasPathPairs
                LocalPointer<UnicodeString> aliasRelativePathCopy(aliasRelativePath.clone(), errorCode);
                aliasPathPairs.adoptElement(aliasRelativePathCopy.orphan(), errorCode);
                if (U_FAILURE(errorCode)) { return; }
                LocalPointer<UnicodeString> pathCopy(path.clone(), errorCode);
                aliasPathPairs.adoptElement(pathCopy.orphan(), errorCode);
                if (U_FAILURE(errorCode)) { return; }

                // Drop the latest key on the path and continue
                path.retainBetween(0, pathLength);
                continue;
            }
            U_ASSERT(aliasType == NONE);

            // == Handle data ==
            if (value.getType() == URES_ARRAY) {
                // We are on a leaf, store the array
                ResourceArray rDataArray = value.getArray(errorCode);
                int32_t dataArraySize = rDataArray.getSize();
                LocalArray<UnicodeString> dataArray(new UnicodeString[dataArraySize], errorCode);
                value.getStringArray(dataArray.getAlias(), dataArraySize, errorCode);
                arrays.put(path, dataArray.orphan(), errorCode);
                arraySizes.puti(path, dataArraySize, errorCode);
                if (U_FAILURE(errorCode)) { return; }
            } else if (value.getType() == URES_TABLE) {
                // We are not on a leaf, recursively process the subtable.
                processResource(path, key, value, errorCode);
                if (U_FAILURE(errorCode)) { return; }
            }

            // Drop the latest key on the path
            path.retainBetween(0, pathLength);
        }
    }

    // Populates an AliasIdentifier with the alias information contained on the UResource.Value.
    AliasType processAliasFromValue(UnicodeString &currentRelativePath, ResourceValue &value,
                                    UErrorCode &errorCode) {
        if (U_FAILURE(errorCode)) { return NONE; }

        if (value.getType() == URES_ALIAS) {
            int32_t aliasPathSize;
            const char16_t* aliasPathUChar = value.getAliasString(aliasPathSize, errorCode);
            if (U_FAILURE(errorCode)) { return NONE; }
            UnicodeString aliasPath(aliasPathUChar, aliasPathSize);
            const int32_t aliasPrefixLength = UPRV_LENGTHOF(kCalendarAliasPrefixUChar);
            if (aliasPath.startsWith(kCalendarAliasPrefixUChar, aliasPrefixLength)
                && aliasPath.length() > aliasPrefixLength) {
                int32_t typeLimit = aliasPath.indexOf(SOLIDUS, aliasPrefixLength);
                if (typeLimit > aliasPrefixLength) {
                    const UnicodeString aliasCalendarType =
                        aliasPath.tempSubStringBetween(aliasPrefixLength, typeLimit);
                    aliasRelativePath.setTo(aliasPath, typeLimit + 1, aliasPath.length());

                    if (currentCalendarType == aliasCalendarType
                        && currentRelativePath != aliasRelativePath) {
                        // If we have an alias to the same calendar, the path to the resource must be different
                        return SAME_CALENDAR;

                    } else if (currentCalendarType != aliasCalendarType
                               && currentRelativePath == aliasRelativePath) {
                        // If we have an alias to a different calendar, the path to the resource must be the same
                        if (aliasCalendarType.compare(kGregorianTagUChar, UPRV_LENGTHOF(kGregorianTagUChar)) == 0) {
                            return GREGORIAN;
                        } else if (nextCalendarType.isBogus()) {
                            nextCalendarType = aliasCalendarType;
                            return DIFFERENT_CALENDAR;
                        } else if (nextCalendarType == aliasCalendarType) {
                            return DIFFERENT_CALENDAR;
                        }
                    }
                }
            }
            errorCode = U_INTERNAL_PROGRAM_ERROR;
            return NONE;
        }
        return NONE;
    }

    // Deleter function to be used by 'arrays'
    static void U_CALLCONV deleteUnicodeStringArray(void *uArray) {
        delete[] static_cast<UnicodeString *>(uArray);
    }
};
// Virtual destructors have to be defined out of line
CalendarDataSink::~CalendarDataSink() {
    arrays.setValueDeleter(deleteUnicodeStringArray);
}
}

//------------------------------------------------------

static void
initField(UnicodeString **field, int32_t& length, const char16_t *data, LastResortSize numStr, LastResortSize strLen, UErrorCode &status) {
    if (U_SUCCESS(status)) {
        length = numStr;
        *field = newUnicodeStringArray(static_cast<size_t>(numStr));
        if (*field) {
            for(int32_t i = 0; i<length; i++) {
                // readonly aliases - all "data" strings are constant
                // -1 as length for variable-length strings (gLastResortDayNames[0] is empty)
                (*(field) + i)->setTo(true, data + (i * (static_cast<int32_t>(strLen))), -1);
            }
        }
        else {
            length = 0;
            status = U_MEMORY_ALLOCATION_ERROR;
        }
    }
}

static void
initField(UnicodeString **field, int32_t& length, CalendarDataSink &sink, CharString &key, UErrorCode &status) {
    if (U_SUCCESS(status)) {
        UnicodeString keyUString(key.data(), -1, US_INV);
        UnicodeString* array = static_cast<UnicodeString*>(sink.arrays.get(keyUString));

        if (array != nullptr) {
            length = sink.arraySizes.geti(keyUString);
            *field = array;
            // DateFormatSymbols takes ownership of the array:
            sink.arrays.remove(keyUString);
        } else {
            length = 0;
            status = U_MISSING_RESOURCE_ERROR;
        }
    }
}

static void
initField(UnicodeString **field, int32_t& length, CalendarDataSink &sink, CharString &key, int32_t arrayOffset, UErrorCode &status) {
    if (U_SUCCESS(status)) {
        UnicodeString keyUString(key.data(), -1, US_INV);
        UnicodeString* array = static_cast<UnicodeString*>(sink.arrays.get(keyUString));

        if (array != nullptr) {
            int32_t arrayLength = sink.arraySizes.geti(keyUString);
            length = arrayLength + arrayOffset;
            *field = new UnicodeString[length];
            if (*field == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return;
            }
            uprv_arrayCopy(array, 0, *field, arrayOffset, arrayLength);
        } else {
            length = 0;
            status = U_MISSING_RESOURCE_ERROR;
        }
    }
}

static void
initLeapMonthPattern(UnicodeString *field, int32_t index, CalendarDataSink &sink, CharString &path, UErrorCode &status) {
    field[index].remove();
    if (U_SUCCESS(status)) {
        UnicodeString pathUString(path.data(), -1, US_INV);
        Hashtable *leapMonthTable = static_cast<Hashtable*>(sink.maps.get(pathUString));
        if (leapMonthTable != nullptr) {
            UnicodeString leapLabel(false, kLeapTagUChar, UPRV_LENGTHOF(kLeapTagUChar));
            UnicodeString *leapMonthPattern = static_cast<UnicodeString*>(leapMonthTable->get(leapLabel));
            if (leapMonthPattern != nullptr) {
                field[index].fastCopyFrom(*leapMonthPattern);
            } else {
                field[index].setToBogus();
            }
            return;
        }
        status = U_MISSING_RESOURCE_ERROR;
    }
}

static CharString
&buildResourcePath(CharString &path, const char* segment1, UErrorCode &errorCode) {
    return path.clear().append(segment1, -1, errorCode);
}

static CharString
&buildResourcePath(CharString &path, const char* segment1, const char* segment2,
                   UErrorCode &errorCode) {
    return buildResourcePath(path, segment1, errorCode).append('/', errorCode)
                                                       .append(segment2, -1, errorCode);
}

static CharString
&buildResourcePath(CharString &path, const char* segment1, const char* segment2,
                   const char* segment3, UErrorCode &errorCode) {
    return buildResourcePath(path, segment1, segment2, errorCode).append('/', errorCode)
                                                                 .append(segment3, -1, errorCode);
}

static CharString
&buildResourcePath(CharString &path, const char* segment1, const char* segment2,
                   const char* segment3, const char* segment4, UErrorCode &errorCode) {
    return buildResourcePath(path, segment1, segment2, segment3, errorCode).append('/', errorCode)
                                                                           .append(segment4, -1, errorCode);
}

typedef struct {
    const char * usageTypeName;
    DateFormatSymbols::ECapitalizationContextUsageType usageTypeEnumValue;
} ContextUsageTypeNameToEnumValue;

static const ContextUsageTypeNameToEnumValue contextUsageTypeMap[] = {
   // Entries must be sorted by usageTypeName; entry with nullptr name terminates list.
    { "day-format-except-narrow", DateFormatSymbols::kCapContextUsageDayFormat },
    { "day-narrow",     DateFormatSymbols::kCapContextUsageDayNarrow },
    { "day-standalone-except-narrow", DateFormatSymbols::kCapContextUsageDayStandalone },
    { "era-abbr",       DateFormatSymbols::kCapContextUsageEraAbbrev },
    { "era-name",       DateFormatSymbols::kCapContextUsageEraWide },
    { "era-narrow",     DateFormatSymbols::kCapContextUsageEraNarrow },
    { "metazone-long",  DateFormatSymbols::kCapContextUsageMetazoneLong },
    { "metazone-short", DateFormatSymbols::kCapContextUsageMetazoneShort },
    { "month-format-except-narrow", DateFormatSymbols::kCapContextUsageMonthFormat },
    { "month-narrow",   DateFormatSymbols::kCapContextUsageMonthNarrow },
    { "month-standalone-except-narrow", DateFormatSymbols::kCapContextUsageMonthStandalone },
    { "zone-long",      DateFormatSymbols::kCapContextUsageZoneLong },
    { "zone-short",     DateFormatSymbols::kCapContextUsageZoneShort },
    { nullptr, static_cast<DateFormatSymbols::ECapitalizationContextUsageType>(0) },
};

// Resource keys to look up localized strings for day periods.
// The first one must be midnight and the second must be noon, so that their indices coincide
// with the am/pm field. Formatting and parsing code for day periods relies on this coincidence.
static const char *dayPeriodKeys[] = {"midnight", "noon",
                         "morning1", "afternoon1", "evening1", "night1",
                         "morning2", "afternoon2", "evening2", "night2"};

UnicodeString* loadDayPeriodStrings(CalendarDataSink &sink, CharString &path,
                                    int32_t &stringCount,  UErrorCode &status) {
    if (U_FAILURE(status)) { return nullptr; }

    UnicodeString pathUString(path.data(), -1, US_INV);
    Hashtable* map = static_cast<Hashtable*>(sink.maps.get(pathUString));

    stringCount = UPRV_LENGTHOF(dayPeriodKeys);
    UnicodeString *strings = new UnicodeString[stringCount];
    if (strings == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    if (map != nullptr) {
        for (int32_t i = 0; i < stringCount; ++i) {
            UnicodeString dayPeriodKey(dayPeriodKeys[i], -1, US_INV);
            UnicodeString *dayPeriod = static_cast<UnicodeString*>(map->get(dayPeriodKey));
            if (dayPeriod != nullptr) {
                strings[i].fastCopyFrom(*dayPeriod);
            } else {
                strings[i].setToBogus();
            }
        }
    } else {
        for (int32_t i = 0; i < stringCount; i++) {
            strings[i].setToBogus();
        }
    }
    return strings;
}


void
DateFormatSymbols::initializeData(const Locale& locale, const char *type, UErrorCode& status, UBool useLastResortData)
{
    int32_t len = 0;
    /* In case something goes wrong, initialize all of the data to nullptr. */
    fEras = nullptr;
    fErasCount = 0;
    fEraNames = nullptr;
    fEraNamesCount = 0;
    fNarrowEras = nullptr;
    fNarrowErasCount = 0;
    fMonths = nullptr;
    fMonthsCount=0;
    fShortMonths = nullptr;
    fShortMonthsCount=0;
    fNarrowMonths = nullptr;
    fNarrowMonthsCount=0;
    fStandaloneMonths = nullptr;
    fStandaloneMonthsCount=0;
    fStandaloneShortMonths = nullptr;
    fStandaloneShortMonthsCount=0;
    fStandaloneNarrowMonths = nullptr;
    fStandaloneNarrowMonthsCount=0;
    fWeekdays = nullptr;
    fWeekdaysCount=0;
    fShortWeekdays = nullptr;
    fShortWeekdaysCount=0;
    fShorterWeekdays = nullptr;
    fShorterWeekdaysCount=0;
    fNarrowWeekdays = nullptr;
    fNarrowWeekdaysCount=0;
    fStandaloneWeekdays = nullptr;
    fStandaloneWeekdaysCount=0;
    fStandaloneShortWeekdays = nullptr;
    fStandaloneShortWeekdaysCount=0;
    fStandaloneShorterWeekdays = nullptr;
    fStandaloneShorterWeekdaysCount=0;
    fStandaloneNarrowWeekdays = nullptr;
    fStandaloneNarrowWeekdaysCount=0;
    fAmPms = nullptr;
    fAmPmsCount=0;
    fNarrowAmPms = nullptr;
    fNarrowAmPmsCount=0;
    fTimeSeparator.setToBogus();
    fQuarters = nullptr;
    fQuartersCount = 0;
    fShortQuarters = nullptr;
    fShortQuartersCount = 0;
    fNarrowQuarters = nullptr;
    fNarrowQuartersCount = 0;
    fStandaloneQuarters = nullptr;
    fStandaloneQuartersCount = 0;
    fStandaloneShortQuarters = nullptr;
    fStandaloneShortQuartersCount = 0;
    fStandaloneNarrowQuarters = nullptr;
    fStandaloneNarrowQuartersCount = 0;
    fLeapMonthPatterns = nullptr;
    fLeapMonthPatternsCount = 0;
    fShortYearNames = nullptr;
    fShortYearNamesCount = 0;
    fShortZodiacNames = nullptr;
    fShortZodiacNamesCount = 0;
    fZoneStringsRowCount = 0;
    fZoneStringsColCount = 0;
    fZoneStrings = nullptr;
    fLocaleZoneStrings = nullptr;
    fAbbreviatedDayPeriods = nullptr;
    fAbbreviatedDayPeriodsCount = 0;
    fWideDayPeriods = nullptr;
    fWideDayPeriodsCount = 0;
    fNarrowDayPeriods = nullptr;
    fNarrowDayPeriodsCount = 0;
    fStandaloneAbbreviatedDayPeriods = nullptr;
    fStandaloneAbbreviatedDayPeriodsCount = 0;
    fStandaloneWideDayPeriods = nullptr;
    fStandaloneWideDayPeriodsCount = 0;
    fStandaloneNarrowDayPeriods = nullptr;
    fStandaloneNarrowDayPeriodsCount = 0;
    uprv_memset(fCapitalization, 0, sizeof(fCapitalization));

    // We need to preserve the requested locale for
    // lazy ZoneStringFormat instantiation.  ZoneStringFormat
    // is region sensitive, thus, bundle locale bundle's locale
    // is not sufficient.
    fZSFLocale = locale;

    if (U_FAILURE(status)) return;

    // Create a CalendarDataSink to process this data and the resource bundles
    CalendarDataSink calendarSink(status);
    LocalUResourceBundlePointer rb(ures_open(nullptr, locale.getBaseName(), &status));
    LocalUResourceBundlePointer cb(ures_getByKey(rb.getAlias(), gCalendarTag, nullptr, &status));

    if (U_FAILURE(status)) return;

    // Iterate over the resource bundle data following the fallbacks through different calendar types
    UnicodeString calendarType((type != nullptr && *type != '\0')? type : gGregorianTag, -1, US_INV);
    while (!calendarType.isBogus()) {
        CharString calendarTypeBuffer;
        calendarTypeBuffer.appendInvariantChars(calendarType, status);
        if (U_FAILURE(status)) { return; }
        const char *calendarTypeCArray = calendarTypeBuffer.data();

        // Enumerate this calendar type. If the calendar is not found fallback to gregorian
        UErrorCode oldStatus = status;
        LocalUResourceBundlePointer ctb(ures_getByKeyWithFallback(cb.getAlias(), calendarTypeCArray, nullptr, &status));
        if (status == U_MISSING_RESOURCE_ERROR) {
            if (uprv_strcmp(calendarTypeCArray, gGregorianTag) != 0) {
                calendarType.setTo(false, kGregorianTagUChar, UPRV_LENGTHOF(kGregorianTagUChar));
                calendarSink.visitAllResources();
                status = oldStatus;
                continue;
            }
            return;
        }

        calendarSink.preEnumerate(calendarType);
        ures_getAllItemsWithFallback(ctb.getAlias(), "", calendarSink, status);
        if (U_FAILURE(status)) break;

        // Stop loading when gregorian was loaded
        if (uprv_strcmp(calendarTypeCArray, gGregorianTag) == 0) {
            break;
        }

        // Get the next calendar type to process from the sink
        calendarType = calendarSink.nextCalendarType;

        // Gregorian is always the last fallback
        if (calendarType.isBogus()) {
            calendarType.setTo(false, kGregorianTagUChar, UPRV_LENGTHOF(kGregorianTagUChar));
            calendarSink.visitAllResources();
        }
    }

    // CharString object to build paths
    CharString path;

    // Load Leap Month Patterns
    UErrorCode tempStatus = status;
    fLeapMonthPatterns = newUnicodeStringArray(kMonthPatternsCount);
    if (fLeapMonthPatterns) {
        initLeapMonthPattern(fLeapMonthPatterns, kLeapMonthPatternFormatWide, calendarSink,
                             buildResourcePath(path, gMonthPatternsTag, gNamesFormatTag, gNamesWideTag, tempStatus), tempStatus);
        initLeapMonthPattern(fLeapMonthPatterns, kLeapMonthPatternFormatAbbrev, calendarSink,
                             buildResourcePath(path, gMonthPatternsTag, gNamesFormatTag, gNamesAbbrTag, tempStatus), tempStatus);
        initLeapMonthPattern(fLeapMonthPatterns, kLeapMonthPatternFormatNarrow, calendarSink,
                             buildResourcePath(path, gMonthPatternsTag, gNamesFormatTag, gNamesNarrowTag, tempStatus), tempStatus);
        initLeapMonthPattern(fLeapMonthPatterns, kLeapMonthPatternStandaloneWide, calendarSink,
                             buildResourcePath(path, gMonthPatternsTag, gNamesStandaloneTag, gNamesWideTag, tempStatus), tempStatus);
        initLeapMonthPattern(fLeapMonthPatterns, kLeapMonthPatternStandaloneAbbrev, calendarSink,
                             buildResourcePath(path, gMonthPatternsTag, gNamesStandaloneTag, gNamesAbbrTag, tempStatus), tempStatus);
        initLeapMonthPattern(fLeapMonthPatterns, kLeapMonthPatternStandaloneNarrow, calendarSink,
                             buildResourcePath(path, gMonthPatternsTag, gNamesStandaloneTag, gNamesNarrowTag, tempStatus), tempStatus);
        initLeapMonthPattern(fLeapMonthPatterns, kLeapMonthPatternNumeric, calendarSink,
                             buildResourcePath(path, gMonthPatternsTag, gNamesNumericTag, gNamesAllTag, tempStatus), tempStatus);
        if (U_SUCCESS(tempStatus)) {
            // Hack to fix bad C inheritance for dangi monthPatterns (OK in J); this should be handled by aliases in root, but isn't.
            // The ordering of the following statements is important.
            if (fLeapMonthPatterns[kLeapMonthPatternFormatAbbrev].isEmpty()) {
                fLeapMonthPatterns[kLeapMonthPatternFormatAbbrev].setTo(fLeapMonthPatterns[kLeapMonthPatternFormatWide]);
            }
            if (fLeapMonthPatterns[kLeapMonthPatternFormatNarrow].isEmpty()) {
                fLeapMonthPatterns[kLeapMonthPatternFormatNarrow].setTo(fLeapMonthPatterns[kLeapMonthPatternStandaloneNarrow]);
            }
            if (fLeapMonthPatterns[kLeapMonthPatternStandaloneWide].isEmpty()) {
                fLeapMonthPatterns[kLeapMonthPatternStandaloneWide].setTo(fLeapMonthPatterns[kLeapMonthPatternFormatWide]);
            }
            if (fLeapMonthPatterns[kLeapMonthPatternStandaloneAbbrev].isEmpty()) {
                fLeapMonthPatterns[kLeapMonthPatternStandaloneAbbrev].setTo(fLeapMonthPatterns[kLeapMonthPatternFormatAbbrev]);
            }
            // end of hack
            fLeapMonthPatternsCount = kMonthPatternsCount;
        } else {
            delete[] fLeapMonthPatterns;
            fLeapMonthPatterns = nullptr;
        }
    }

    // Load cyclic names sets
    tempStatus = status;
    initField(&fShortYearNames, fShortYearNamesCount, calendarSink,
              buildResourcePath(path, gCyclicNameSetsTag, gNameSetYearsTag, gNamesFormatTag, gNamesAbbrTag, tempStatus), tempStatus);
    initField(&fShortZodiacNames, fShortZodiacNamesCount, calendarSink,
              buildResourcePath(path, gCyclicNameSetsTag, gNameSetZodiacsTag, gNamesFormatTag, gNamesAbbrTag, tempStatus), tempStatus);

    // Load context transforms and capitalization
    tempStatus = U_ZERO_ERROR;
    LocalUResourceBundlePointer localeBundle(ures_open(nullptr, locale.getName(), &tempStatus));
    if (U_SUCCESS(tempStatus)) {
        LocalUResourceBundlePointer contextTransforms(ures_getByKeyWithFallback(localeBundle.getAlias(), gContextTransformsTag, nullptr, &tempStatus));
        if (U_SUCCESS(tempStatus)) {
            for (LocalUResourceBundlePointer contextTransformUsage;
                 contextTransformUsage.adoptInstead(ures_getNextResource(contextTransforms.getAlias(), nullptr, &tempStatus)),
                 contextTransformUsage.isValid();) {
                const int32_t * intVector = ures_getIntVector(contextTransformUsage.getAlias(), &len, &status);
                if (U_SUCCESS(tempStatus) && intVector != nullptr && len >= 2) {
                    const char* usageType = ures_getKey(contextTransformUsage.getAlias());
                    if (usageType != nullptr) {
                        const ContextUsageTypeNameToEnumValue * typeMapPtr = contextUsageTypeMap;
                        int32_t compResult = 0;
                        // linear search; list is short and we cannot be sure that bsearch is available
                        while ( typeMapPtr->usageTypeName != nullptr && (compResult = uprv_strcmp(usageType, typeMapPtr->usageTypeName)) > 0 ) {
                            ++typeMapPtr;
                        }
                        if (typeMapPtr->usageTypeName != nullptr && compResult == 0) {
                            fCapitalization[typeMapPtr->usageTypeEnumValue][0] = static_cast<UBool>(intVector[0]);
                            fCapitalization[typeMapPtr->usageTypeEnumValue][1] = static_cast<UBool>(intVector[1]);
                        }
                    }
                }
                tempStatus = U_ZERO_ERROR;
            }
        }

        tempStatus = U_ZERO_ERROR;
        const LocalPointer<NumberingSystem> numberingSystem(
                NumberingSystem::createInstance(locale, tempStatus), tempStatus);
        if (U_SUCCESS(tempStatus)) {
            // These functions all fail gracefully if passed nullptr pointers and
            // do nothing unless U_SUCCESS(tempStatus), so it's only necessary
            // to check for errors once after all calls are made.
            const LocalUResourceBundlePointer numberElementsData(ures_getByKeyWithFallback(
                    localeBundle.getAlias(), gNumberElementsTag, nullptr, &tempStatus));
            const LocalUResourceBundlePointer nsNameData(ures_getByKeyWithFallback(
                    numberElementsData.getAlias(), numberingSystem->getName(), nullptr, &tempStatus));
            const LocalUResourceBundlePointer symbolsData(ures_getByKeyWithFallback(
                    nsNameData.getAlias(), gSymbolsTag, nullptr, &tempStatus));
            fTimeSeparator = ures_getUnicodeStringByKey(
                    symbolsData.getAlias(), gTimeSeparatorTag, &tempStatus);
            if (U_FAILURE(tempStatus)) {
                fTimeSeparator.setToBogus();
            }
        }

    }

    if (fTimeSeparator.isBogus()) {
        fTimeSeparator.setTo(DateFormatSymbols::DEFAULT_TIME_SEPARATOR);
    }

    // Load day periods
    fAbbreviatedDayPeriods = loadDayPeriodStrings(calendarSink,
                            buildResourcePath(path, gDayPeriodTag, gNamesFormatTag, gNamesAbbrTag, status),
                            fAbbreviatedDayPeriodsCount, status);

    fWideDayPeriods = loadDayPeriodStrings(calendarSink,
                            buildResourcePath(path, gDayPeriodTag, gNamesFormatTag, gNamesWideTag, status),
                            fWideDayPeriodsCount, status);
    fNarrowDayPeriods = loadDayPeriodStrings(calendarSink,
                            buildResourcePath(path, gDayPeriodTag, gNamesFormatTag, gNamesNarrowTag, status),
                            fNarrowDayPeriodsCount, status);

    fStandaloneAbbreviatedDayPeriods = loadDayPeriodStrings(calendarSink,
                            buildResourcePath(path, gDayPeriodTag, gNamesStandaloneTag, gNamesAbbrTag, status),
                            fStandaloneAbbreviatedDayPeriodsCount, status);

    fStandaloneWideDayPeriods = loadDayPeriodStrings(calendarSink,
                            buildResourcePath(path, gDayPeriodTag, gNamesStandaloneTag, gNamesWideTag, status),
                            fStandaloneWideDayPeriodsCount, status);
    fStandaloneNarrowDayPeriods = loadDayPeriodStrings(calendarSink,
                            buildResourcePath(path, gDayPeriodTag, gNamesStandaloneTag, gNamesNarrowTag, status),
                            fStandaloneNarrowDayPeriodsCount, status);

    // Fill in for missing/bogus items (dayPeriods are a map so single items might be missing)
    if (U_SUCCESS(status)) {
        for (int32_t dpidx = 0; dpidx < fAbbreviatedDayPeriodsCount; ++dpidx) {
            if (dpidx < fWideDayPeriodsCount && fWideDayPeriods != nullptr && fWideDayPeriods[dpidx].isBogus()) {
                fWideDayPeriods[dpidx].fastCopyFrom(fAbbreviatedDayPeriods[dpidx]);
            }
            if (dpidx < fNarrowDayPeriodsCount && fNarrowDayPeriods != nullptr && fNarrowDayPeriods[dpidx].isBogus()) {
                fNarrowDayPeriods[dpidx].fastCopyFrom(fAbbreviatedDayPeriods[dpidx]);
            }
            if (dpidx < fStandaloneAbbreviatedDayPeriodsCount && fStandaloneAbbreviatedDayPeriods != nullptr && fStandaloneAbbreviatedDayPeriods[dpidx].isBogus()) {
                fStandaloneAbbreviatedDayPeriods[dpidx].fastCopyFrom(fAbbreviatedDayPeriods[dpidx]);
            }
            if (dpidx < fStandaloneWideDayPeriodsCount && fStandaloneWideDayPeriods != nullptr && fStandaloneWideDayPeriods[dpidx].isBogus()) {
                fStandaloneWideDayPeriods[dpidx].fastCopyFrom(fStandaloneAbbreviatedDayPeriods[dpidx]);
            }
            if (dpidx < fStandaloneNarrowDayPeriodsCount && fStandaloneNarrowDayPeriods != nullptr && fStandaloneNarrowDayPeriods[dpidx].isBogus()) {
                fStandaloneNarrowDayPeriods[dpidx].fastCopyFrom(fStandaloneAbbreviatedDayPeriods[dpidx]);
            }
        }
    }

    U_LOCALE_BASED(locBased, *this);
    // if we make it to here, the resource data is cool, and we can get everything out
    // of it that we need except for the time-zone and localized-pattern data, which
    // are stored in a separate file
    locBased.setLocaleIDs(ures_getLocaleByType(cb.getAlias(), ULOC_VALID_LOCALE, &status),
                          ures_getLocaleByType(cb.getAlias(), ULOC_ACTUAL_LOCALE, &status));

    // Load eras
    initField(&fEras, fErasCount, calendarSink, buildResourcePath(path, gErasTag, gNamesAbbrTag, status), status);
    UErrorCode oldStatus = status;
    initField(&fEraNames, fEraNamesCount, calendarSink, buildResourcePath(path, gErasTag, gNamesWideTag, status), status);
    if (status == U_MISSING_RESOURCE_ERROR) { // Workaround because eras/wide was omitted from CLDR 1.3
        status = oldStatus;
        assignArray(fEraNames, fEraNamesCount, fEras, fErasCount);
    }
    // current ICU4J falls back to abbreviated if narrow eras are missing, so we will too
    oldStatus = status;
    initField(&fNarrowEras, fNarrowErasCount, calendarSink, buildResourcePath(path, gErasTag, gNamesNarrowTag, status), status);
    if (status == U_MISSING_RESOURCE_ERROR) { // Workaround because eras/wide was omitted from CLDR 1.3
        status = oldStatus;
        assignArray(fNarrowEras, fNarrowErasCount, fEras, fErasCount);
    }

    // Load month names
    initField(&fMonths, fMonthsCount, calendarSink,
              buildResourcePath(path, gMonthNamesTag, gNamesFormatTag, gNamesWideTag, status), status);
    initField(&fShortMonths, fShortMonthsCount, calendarSink,
              buildResourcePath(path, gMonthNamesTag, gNamesFormatTag, gNamesAbbrTag, status), status);
    initField(&fStandaloneMonths, fStandaloneMonthsCount, calendarSink,
              buildResourcePath(path, gMonthNamesTag, gNamesStandaloneTag, gNamesWideTag, status), status);
    if (status == U_MISSING_RESOURCE_ERROR) { /* If standalone/wide not available, use format/wide */
        status = U_ZERO_ERROR;
        assignArray(fStandaloneMonths, fStandaloneMonthsCount, fMonths, fMonthsCount);
    }
    initField(&fStandaloneShortMonths, fStandaloneShortMonthsCount, calendarSink,
              buildResourcePath(path, gMonthNamesTag, gNamesStandaloneTag, gNamesAbbrTag, status), status);
    if (status == U_MISSING_RESOURCE_ERROR) { /* If standalone/abbreviated not available, use format/abbreviated */
        status = U_ZERO_ERROR;
        assignArray(fStandaloneShortMonths, fStandaloneShortMonthsCount, fShortMonths, fShortMonthsCount);
    }

    UErrorCode narrowMonthsEC = status;
    UErrorCode standaloneNarrowMonthsEC = status;
    initField(&fNarrowMonths, fNarrowMonthsCount, calendarSink,
              buildResourcePath(path, gMonthNamesTag, gNamesFormatTag, gNamesNarrowTag, narrowMonthsEC), narrowMonthsEC);
    initField(&fStandaloneNarrowMonths, fStandaloneNarrowMonthsCount, calendarSink,
              buildResourcePath(path, gMonthNamesTag, gNamesStandaloneTag, gNamesNarrowTag, narrowMonthsEC), standaloneNarrowMonthsEC);
    if (narrowMonthsEC == U_MISSING_RESOURCE_ERROR && standaloneNarrowMonthsEC != U_MISSING_RESOURCE_ERROR) {
        // If format/narrow not available, use standalone/narrow
        assignArray(fNarrowMonths, fNarrowMonthsCount, fStandaloneNarrowMonths, fStandaloneNarrowMonthsCount);
    } else if (narrowMonthsEC != U_MISSING_RESOURCE_ERROR && standaloneNarrowMonthsEC == U_MISSING_RESOURCE_ERROR) {
        // If standalone/narrow not available, use format/narrow
        assignArray(fStandaloneNarrowMonths, fStandaloneNarrowMonthsCount, fNarrowMonths, fNarrowMonthsCount);
    } else if (narrowMonthsEC == U_MISSING_RESOURCE_ERROR && standaloneNarrowMonthsEC == U_MISSING_RESOURCE_ERROR) {
        // If neither is available, use format/abbreviated
        assignArray(fNarrowMonths, fNarrowMonthsCount, fShortMonths, fShortMonthsCount);
        assignArray(fStandaloneNarrowMonths, fStandaloneNarrowMonthsCount, fShortMonths, fShortMonthsCount);
    }

    // Load AM/PM markers; if wide or narrow not available, use short
    UErrorCode ampmStatus = U_ZERO_ERROR;
    initField(&fAmPms, fAmPmsCount, calendarSink,
              buildResourcePath(path, gAmPmMarkersTag, ampmStatus), ampmStatus);
    if (U_FAILURE(ampmStatus)) {
        initField(&fAmPms, fAmPmsCount, calendarSink,
                  buildResourcePath(path, gAmPmMarkersAbbrTag, status), status);
    }
    ampmStatus = U_ZERO_ERROR;
    initField(&fNarrowAmPms, fNarrowAmPmsCount, calendarSink,
              buildResourcePath(path, gAmPmMarkersNarrowTag, ampmStatus), ampmStatus);
    if (U_FAILURE(ampmStatus)) {
        initField(&fNarrowAmPms, fNarrowAmPmsCount, calendarSink,
                  buildResourcePath(path, gAmPmMarkersAbbrTag, status), status);
    }
    if(status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        assignArray(fNarrowAmPms, fNarrowAmPmsCount, fAmPms, fAmPmsCount);
    }

    // Load quarters
    initField(&fQuarters, fQuartersCount, calendarSink,
              buildResourcePath(path, gQuartersTag, gNamesFormatTag, gNamesWideTag, status), status);
    initField(&fShortQuarters, fShortQuartersCount, calendarSink,
              buildResourcePath(path, gQuartersTag, gNamesFormatTag, gNamesAbbrTag, status), status);
    if(status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        assignArray(fShortQuarters, fShortQuartersCount, fQuarters, fQuartersCount);
    }

    initField(&fStandaloneQuarters, fStandaloneQuartersCount, calendarSink,
              buildResourcePath(path, gQuartersTag, gNamesStandaloneTag, gNamesWideTag, status), status);
    if(status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        assignArray(fStandaloneQuarters, fStandaloneQuartersCount, fQuarters, fQuartersCount);
    }
    initField(&fStandaloneShortQuarters, fStandaloneShortQuartersCount, calendarSink,
              buildResourcePath(path, gQuartersTag, gNamesStandaloneTag, gNamesAbbrTag, status), status);
    if(status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        assignArray(fStandaloneShortQuarters, fStandaloneShortQuartersCount, fShortQuarters, fShortQuartersCount);
    }

    // unlike the fields above, narrow format quarters fall back on narrow standalone quarters
    initField(&fStandaloneNarrowQuarters, fStandaloneNarrowQuartersCount, calendarSink,
              buildResourcePath(path, gQuartersTag, gNamesStandaloneTag, gNamesNarrowTag, status), status);
    initField(&fNarrowQuarters, fNarrowQuartersCount, calendarSink,
              buildResourcePath(path, gQuartersTag, gNamesFormatTag, gNamesNarrowTag, status), status);
    if(status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        assignArray(fNarrowQuarters, fNarrowQuartersCount, fStandaloneNarrowQuarters, fStandaloneNarrowQuartersCount);
    }
    
    // ICU 3.8 or later version no longer uses localized date-time pattern characters by default (ticket#5597)
    /*
    // fastCopyFrom()/setTo() - see assignArray comments
    resStr = ures_getStringByKey(fResourceBundle, gLocalPatternCharsTag, &len, &status);
    fLocalPatternChars.setTo(true, resStr, len);
    // If the locale data does not include new pattern chars, use the defaults
    // TODO: Consider making this an error, since this may add conflicting characters.
    if (len < PATTERN_CHARS_LEN) {
        fLocalPatternChars.append(UnicodeString(true, &gPatternChars[len], PATTERN_CHARS_LEN-len));
    }
    */
    fLocalPatternChars.setTo(true, gPatternChars, PATTERN_CHARS_LEN);

    // Format wide weekdays -> fWeekdays
    // {sfb} fixed to handle 1-based weekdays
    initField(&fWeekdays, fWeekdaysCount, calendarSink,
              buildResourcePath(path, gDayNamesTag, gNamesFormatTag, gNamesWideTag, status), 1, status);

    // Format abbreviated weekdays -> fShortWeekdays
    initField(&fShortWeekdays, fShortWeekdaysCount, calendarSink,
              buildResourcePath(path, gDayNamesTag, gNamesFormatTag, gNamesAbbrTag, status), 1, status);

    // Format short weekdays -> fShorterWeekdays (fall back to abbreviated)
    initField(&fShorterWeekdays, fShorterWeekdaysCount, calendarSink,
              buildResourcePath(path, gDayNamesTag, gNamesFormatTag, gNamesShortTag, status), 1, status);
    if (status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        assignArray(fShorterWeekdays, fShorterWeekdaysCount, fShortWeekdays, fShortWeekdaysCount);
    }

    // Stand-alone wide weekdays -> fStandaloneWeekdays
    initField(&fStandaloneWeekdays, fStandaloneWeekdaysCount, calendarSink,
              buildResourcePath(path, gDayNamesTag, gNamesStandaloneTag, gNamesWideTag, status), 1, status);
    if (status == U_MISSING_RESOURCE_ERROR) { /* If standalone/wide is not available, use format/wide */
        status = U_ZERO_ERROR;
        assignArray(fStandaloneWeekdays, fStandaloneWeekdaysCount, fWeekdays, fWeekdaysCount);
    }

    // Stand-alone abbreviated weekdays -> fStandaloneShortWeekdays
    initField(&fStandaloneShortWeekdays, fStandaloneShortWeekdaysCount, calendarSink,
              buildResourcePath(path, gDayNamesTag, gNamesStandaloneTag, gNamesAbbrTag, status), 1, status);
    if (status == U_MISSING_RESOURCE_ERROR) { /* If standalone/abbreviated is not available, use format/abbreviated */
        status = U_ZERO_ERROR;
        assignArray(fStandaloneShortWeekdays, fStandaloneShortWeekdaysCount, fShortWeekdays, fShortWeekdaysCount);
    }

    // Stand-alone short weekdays -> fStandaloneShorterWeekdays (fall back to format abbreviated)
    initField(&fStandaloneShorterWeekdays, fStandaloneShorterWeekdaysCount, calendarSink,
              buildResourcePath(path, gDayNamesTag, gNamesStandaloneTag, gNamesShortTag, status), 1, status);
    if (status == U_MISSING_RESOURCE_ERROR) { /* If standalone/short is not available, use format/short */
        status = U_ZERO_ERROR;
        assignArray(fStandaloneShorterWeekdays, fStandaloneShorterWeekdaysCount, fShorterWeekdays, fShorterWeekdaysCount);
    }

    // Format narrow weekdays -> fNarrowWeekdays
    UErrorCode narrowWeeksEC = status;
    initField(&fNarrowWeekdays, fNarrowWeekdaysCount, calendarSink,
              buildResourcePath(path, gDayNamesTag, gNamesFormatTag, gNamesNarrowTag, status), 1, narrowWeeksEC);
    // Stand-alone narrow weekdays -> fStandaloneNarrowWeekdays
    UErrorCode standaloneNarrowWeeksEC = status;
    initField(&fStandaloneNarrowWeekdays, fStandaloneNarrowWeekdaysCount, calendarSink,
              buildResourcePath(path, gDayNamesTag, gNamesStandaloneTag, gNamesNarrowTag, status), 1, standaloneNarrowWeeksEC);

    if (narrowWeeksEC == U_MISSING_RESOURCE_ERROR && standaloneNarrowWeeksEC != U_MISSING_RESOURCE_ERROR) {
        // If format/narrow not available, use standalone/narrow
        assignArray(fNarrowWeekdays, fNarrowWeekdaysCount, fStandaloneNarrowWeekdays, fStandaloneNarrowWeekdaysCount);
    } else if (narrowWeeksEC != U_MISSING_RESOURCE_ERROR && standaloneNarrowWeeksEC == U_MISSING_RESOURCE_ERROR) {
        // If standalone/narrow not available, use format/narrow
        assignArray(fStandaloneNarrowWeekdays, fStandaloneNarrowWeekdaysCount, fNarrowWeekdays, fNarrowWeekdaysCount);
    } else if (narrowWeeksEC == U_MISSING_RESOURCE_ERROR && standaloneNarrowWeeksEC == U_MISSING_RESOURCE_ERROR ) {
        // If neither is available, use format/abbreviated
        assignArray(fNarrowWeekdays, fNarrowWeekdaysCount, fShortWeekdays, fShortWeekdaysCount);
        assignArray(fStandaloneNarrowWeekdays, fStandaloneNarrowWeekdaysCount, fShortWeekdays, fShortWeekdaysCount);
    }

    // Last resort fallback in case previous data wasn't loaded
    if (U_FAILURE(status))
    {
        if (useLastResortData)
        {
            // Handle the case in which there is no resource data present.
            // We don't have to generate usable patterns in this situation;
            // we just need to produce something that will be semi-intelligible
            // in most locales.

            status = U_USING_FALLBACK_WARNING;
            //TODO(fabalbon): make sure we are storing las resort data for all fields in here.
            initField(&fEras, fErasCount, reinterpret_cast<const char16_t*>(gLastResortEras), kEraNum, kEraLen, status);
            initField(&fEraNames, fEraNamesCount, reinterpret_cast<const char16_t*>(gLastResortEras), kEraNum, kEraLen, status);
            initField(&fNarrowEras, fNarrowErasCount, reinterpret_cast<const char16_t*>(gLastResortEras), kEraNum, kEraLen, status);
            initField(&fMonths, fMonthsCount, reinterpret_cast<const char16_t*>(gLastResortMonthNames), kMonthNum, kMonthLen, status);
            initField(&fShortMonths, fShortMonthsCount, reinterpret_cast<const char16_t*>(gLastResortMonthNames), kMonthNum, kMonthLen, status);
            initField(&fNarrowMonths, fNarrowMonthsCount, reinterpret_cast<const char16_t*>(gLastResortMonthNames), kMonthNum, kMonthLen, status);
            initField(&fStandaloneMonths, fStandaloneMonthsCount, reinterpret_cast<const char16_t*>(gLastResortMonthNames), kMonthNum, kMonthLen, status);
            initField(&fStandaloneShortMonths, fStandaloneShortMonthsCount, reinterpret_cast<const char16_t*>(gLastResortMonthNames), kMonthNum, kMonthLen, status);
            initField(&fStandaloneNarrowMonths, fStandaloneNarrowMonthsCount, reinterpret_cast<const char16_t*>(gLastResortMonthNames), kMonthNum, kMonthLen, status);
            initField(&fWeekdays, fWeekdaysCount, reinterpret_cast<const char16_t*>(gLastResortDayNames), kDayNum, kDayLen, status);
            initField(&fShortWeekdays, fShortWeekdaysCount, reinterpret_cast<const char16_t*>(gLastResortDayNames), kDayNum, kDayLen, status);
            initField(&fShorterWeekdays, fShorterWeekdaysCount, reinterpret_cast<const char16_t*>(gLastResortDayNames), kDayNum, kDayLen, status);
            initField(&fNarrowWeekdays, fNarrowWeekdaysCount, reinterpret_cast<const char16_t*>(gLastResortDayNames), kDayNum, kDayLen, status);
            initField(&fStandaloneWeekdays, fStandaloneWeekdaysCount, reinterpret_cast<const char16_t*>(gLastResortDayNames), kDayNum, kDayLen, status);
            initField(&fStandaloneShortWeekdays, fStandaloneShortWeekdaysCount, reinterpret_cast<const char16_t*>(gLastResortDayNames), kDayNum, kDayLen, status);
            initField(&fStandaloneShorterWeekdays, fStandaloneShorterWeekdaysCount, reinterpret_cast<const char16_t*>(gLastResortDayNames), kDayNum, kDayLen, status);
            initField(&fStandaloneNarrowWeekdays, fStandaloneNarrowWeekdaysCount, reinterpret_cast<const char16_t*>(gLastResortDayNames), kDayNum, kDayLen, status);
            initField(&fAmPms, fAmPmsCount, reinterpret_cast<const char16_t*>(gLastResortAmPmMarkers), kAmPmNum, kAmPmLen, status);
            initField(&fNarrowAmPms, fNarrowAmPmsCount, reinterpret_cast<const char16_t*>(gLastResortAmPmMarkers), kAmPmNum, kAmPmLen, status);
            initField(&fQuarters, fQuartersCount, reinterpret_cast<const char16_t*>(gLastResortQuarters), kQuarterNum, kQuarterLen, status);
            initField(&fShortQuarters, fShortQuartersCount, reinterpret_cast<const char16_t*>(gLastResortQuarters), kQuarterNum, kQuarterLen, status);
            initField(&fNarrowQuarters, fNarrowQuartersCount, reinterpret_cast<const char16_t*>(gLastResortQuarters), kQuarterNum, kQuarterLen, status);
            initField(&fStandaloneQuarters, fStandaloneQuartersCount, reinterpret_cast<const char16_t*>(gLastResortQuarters), kQuarterNum, kQuarterLen, status);
            initField(&fStandaloneShortQuarters, fStandaloneShortQuartersCount, reinterpret_cast<const char16_t*>(gLastResortQuarters), kQuarterNum, kQuarterLen, status);
            initField(&fStandaloneNarrowQuarters, fStandaloneNarrowQuartersCount, reinterpret_cast<const char16_t*>(gLastResortQuarters), kQuarterNum, kQuarterLen, status);
            fLocalPatternChars.setTo(true, gPatternChars, PATTERN_CHARS_LEN);
        }
    }
}

Locale
DateFormatSymbols::getLocale(ULocDataLocaleType type, UErrorCode& status) const {
    U_LOCALE_BASED(locBased, *this);
    return locBased.getLocale(type, status);
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
