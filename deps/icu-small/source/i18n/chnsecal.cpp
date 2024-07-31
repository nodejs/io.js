// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 ******************************************************************************
 * Copyright (C) 2007-2014, International Business Machines Corporation
 * and others. All Rights Reserved.
 ******************************************************************************
 *
 * File CHNSECAL.CPP
 *
 * Modification History:
 *
 *   Date        Name        Description
 *   9/18/2007  ajmacher         ported from java ChineseCalendar
 *****************************************************************************
 */

#include "chnsecal.h"

#include <cstdint>

#if !UCONFIG_NO_FORMATTING

#include "umutex.h"
#include <float.h>
#include "gregoimp.h" // Math
#include "astro.h" // CalendarAstronomer and CalendarCache
#include "unicode/simpletz.h"
#include "uhash.h"
#include "ucln_in.h"
#include "cstring.h"

// Debugging
#ifdef U_DEBUG_CHNSECAL
# include <stdio.h>
# include <stdarg.h>
static void debug_chnsecal_loc(const char *f, int32_t l)
{
    fprintf(stderr, "%s:%d: ", f, l);
}

static void debug_chnsecal_msg(const char *pat, ...)
{
    va_list ap;
    va_start(ap, pat);
    vfprintf(stderr, pat, ap);
    fflush(stderr);
}
// must use double parens, i.e.:  U_DEBUG_CHNSECAL_MSG(("four is: %d",4));
#define U_DEBUG_CHNSECAL_MSG(x) {debug_chnsecal_loc(__FILE__,__LINE__);debug_chnsecal_msg x;}
#else
#define U_DEBUG_CHNSECAL_MSG(x)
#endif


// Lazy Creation & Access synchronized by class CalendarCache with a mutex.
static icu::CalendarCache *gWinterSolsticeCache = nullptr;
static icu::CalendarCache *gNewYearCache = nullptr;

static icu::TimeZone *gAstronomerTimeZone = nullptr;
static icu::UInitOnce gAstronomerTimeZoneInitOnce {};

/**
 * The start year of the Chinese calendar, the 61st year of the reign
 * of Huang Di.  Some sources use the first year of his reign,
 * resulting in EXTENDED_YEAR values 60 years greater and ERA (cycle)
 * values one greater.
 */
static const int32_t CHINESE_EPOCH_YEAR = -2636; // Gregorian year

/**
 * The offset from GMT in milliseconds at which we perform astronomical
 * computations.  Some sources use a different historically accurate
 * offset of GMT+7:45:40 for years before 1929; we do not do this.
 */
static const int32_t CHINA_OFFSET = 8 * kOneHour;

/**
 * Value to be added or subtracted from the local days of a new moon to
 * get close to the next or prior new moon, but not cross it.  Must be
 * >= 1 and < CalendarAstronomer.SYNODIC_MONTH.
 */
static const int32_t SYNODIC_GAP = 25;


U_CDECL_BEGIN
static UBool calendar_chinese_cleanup() {
    if (gWinterSolsticeCache) {
        delete gWinterSolsticeCache;
        gWinterSolsticeCache = nullptr;
    }
    if (gNewYearCache) {
        delete gNewYearCache;
        gNewYearCache = nullptr;
    }
    if (gAstronomerTimeZone) {
        delete gAstronomerTimeZone;
        gAstronomerTimeZone = nullptr;
    }
    gAstronomerTimeZoneInitOnce.reset();
    return true;
}
U_CDECL_END

U_NAMESPACE_BEGIN


// Implementation of the ChineseCalendar class


//-------------------------------------------------------------------------
// Constructors...
//-------------------------------------------------------------------------


namespace {

const TimeZone* getAstronomerTimeZone();
int32_t newMoonNear(const TimeZone*, double, UBool);
int32_t newYear(const icu::ChineseCalendar::Setting&, int32_t);
UBool isLeapMonthBetween(const TimeZone*, int32_t, int32_t);

} // namespace

ChineseCalendar* ChineseCalendar::clone() const {
    return new ChineseCalendar(*this);
}

ChineseCalendar::ChineseCalendar(const Locale& aLocale, UErrorCode& success)
:   Calendar(TimeZone::forLocaleOrDefault(aLocale), aLocale, success),
    hasLeapMonthBetweenWinterSolstices(false)
{
    setTimeInMillis(getNow(), success); // Call this again now that the vtable is set up properly.
}

ChineseCalendar::ChineseCalendar(const ChineseCalendar& other) : Calendar(other) {
    hasLeapMonthBetweenWinterSolstices = other.hasLeapMonthBetweenWinterSolstices;
}

ChineseCalendar::~ChineseCalendar()
{
}

const char *ChineseCalendar::getType() const { 
    return "chinese";
}

namespace { // anonymous

static void U_CALLCONV initAstronomerTimeZone() {
    gAstronomerTimeZone = new SimpleTimeZone(CHINA_OFFSET, UNICODE_STRING_SIMPLE("CHINA_ZONE") );
    ucln_i18n_registerCleanup(UCLN_I18N_CHINESE_CALENDAR, calendar_chinese_cleanup);
}

const TimeZone* getAstronomerTimeZone() {
    umtx_initOnce(gAstronomerTimeZoneInitOnce, &initAstronomerTimeZone);
    return gAstronomerTimeZone;
}

} // namespace anonymous

//-------------------------------------------------------------------------
// Minimum / Maximum access functions
//-------------------------------------------------------------------------


static const int32_t LIMITS[UCAL_FIELD_COUNT][4] = {
    // Minimum  Greatest     Least    Maximum
    //           Minimum   Maximum
    {        1,        1,    83333,    83333}, // ERA
    {        1,        1,       60,       60}, // YEAR
    {        0,        0,       11,       11}, // MONTH
    {        1,        1,       50,       55}, // WEEK_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // WEEK_OF_MONTH
    {        1,        1,       29,       30}, // DAY_OF_MONTH
    {        1,        1,      353,      385}, // DAY_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DAY_OF_WEEK
    {       -1,       -1,        5,        5}, // DAY_OF_WEEK_IN_MONTH
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // AM_PM
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // HOUR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // HOUR_OF_DAY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MINUTE
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // SECOND
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MILLISECOND
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // ZONE_OFFSET
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DST_OFFSET
    { -5000000, -5000000,  5000000,  5000000}, // YEAR_WOY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DOW_LOCAL
    { -5000000, -5000000,  5000000,  5000000}, // EXTENDED_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // JULIAN_DAY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MILLISECONDS_IN_DAY
    {        0,        0,        1,        1}, // IS_LEAP_MONTH
    {        0,        0,       11,       12}, // ORDINAL_MONTH
};


/**
* @draft ICU 2.4
*/
int32_t ChineseCalendar::handleGetLimit(UCalendarDateFields field, ELimitType limitType) const {
    return LIMITS[field][limitType];
}


//----------------------------------------------------------------------
// Calendar framework
//----------------------------------------------------------------------

/**
 * Implement abstract Calendar method to return the extended year
 * defined by the current fields.  This will use either the ERA and
 * YEAR field as the cycle and year-of-cycle, or the EXTENDED_YEAR
 * field as the continuous year count, depending on which is newer.
 * @stable ICU 2.8
 */
int32_t ChineseCalendar::handleGetExtendedYear(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return 0;
    }

    int32_t year;
    if (newestStamp(UCAL_ERA, UCAL_YEAR, kUnset) <= fStamp[UCAL_EXTENDED_YEAR]) {
        year = internalGet(UCAL_EXTENDED_YEAR, 1); // Default to year 1
    } else {
        // adjust to the instance specific epoch
        int32_t cycle = internalGet(UCAL_ERA, 1);
        year = internalGet(UCAL_YEAR, 1);
        const Setting setting = getSetting(status);
        if (U_FAILURE(status)) {
            return 0;
        }
        // Handle int32 overflow calculation for
        // year = year + (cycle-1) * 60 -(fEpochYear - CHINESE_EPOCH_YEAR)
        if (uprv_add32_overflow(cycle, -1, &cycle) || // 0-based cycle
            uprv_mul32_overflow(cycle, 60, &cycle) ||
            uprv_add32_overflow(year, cycle, &year) ||
            uprv_add32_overflow(year, -(setting.epochYear-CHINESE_EPOCH_YEAR),
                                &year)) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return 0;
        }
    }
    return year;
}

/**
 * Override Calendar method to return the number of days in the given
 * extended year and month.
 *
 * <p>Note: This method also reads the IS_LEAP_MONTH field to determine
 * whether or not the given month is a leap month.
 * @stable ICU 2.8
 */
int32_t ChineseCalendar::handleGetMonthLength(int32_t extendedYear, int32_t month, UErrorCode& status) const {
    const Setting setting = getSetting(status);
    int32_t thisStart = handleComputeMonthStart(extendedYear, month, true, status);
    if (U_FAILURE(status)) {
        return 0;
    }
    thisStart = thisStart -
        kEpochStartAsJulianDay + 1; // Julian day -> local days
    int32_t nextStart = newMoonNear(setting.zoneAstroCalc, thisStart + SYNODIC_GAP, true);
    return nextStart - thisStart;
}

/**
 * Field resolution table that incorporates IS_LEAP_MONTH.
 */
const UFieldResolutionTable ChineseCalendar::CHINESE_DATE_PRECEDENCE[] =
{
    {
        { UCAL_DAY_OF_MONTH, kResolveSTOP },
        { UCAL_WEEK_OF_YEAR, UCAL_DAY_OF_WEEK, kResolveSTOP },
        { UCAL_WEEK_OF_MONTH, UCAL_DAY_OF_WEEK, kResolveSTOP },
        { UCAL_DAY_OF_WEEK_IN_MONTH, UCAL_DAY_OF_WEEK, kResolveSTOP },
        { UCAL_WEEK_OF_YEAR, UCAL_DOW_LOCAL, kResolveSTOP },
        { UCAL_WEEK_OF_MONTH, UCAL_DOW_LOCAL, kResolveSTOP },
        { UCAL_DAY_OF_WEEK_IN_MONTH, UCAL_DOW_LOCAL, kResolveSTOP },
        { UCAL_DAY_OF_YEAR, kResolveSTOP },
        { kResolveRemap | UCAL_DAY_OF_MONTH, UCAL_IS_LEAP_MONTH, kResolveSTOP },
        { kResolveSTOP }
    },
    {
        { UCAL_WEEK_OF_YEAR, kResolveSTOP },
        { UCAL_WEEK_OF_MONTH, kResolveSTOP },
        { UCAL_DAY_OF_WEEK_IN_MONTH, kResolveSTOP },
        { kResolveRemap | UCAL_DAY_OF_WEEK_IN_MONTH, UCAL_DAY_OF_WEEK, kResolveSTOP },
        { kResolveRemap | UCAL_DAY_OF_WEEK_IN_MONTH, UCAL_DOW_LOCAL, kResolveSTOP },
        { kResolveSTOP }
    },
    {{kResolveSTOP}}
};

/**
 * Override Calendar to add IS_LEAP_MONTH to the field resolution
 * table.
 * @stable ICU 2.8
 */
const UFieldResolutionTable* ChineseCalendar::getFieldResolutionTable() const {
    return CHINESE_DATE_PRECEDENCE;
}

namespace {

struct MonthInfo {
  int32_t month;
  int32_t ordinalMonth;
  int32_t thisMoon;
  bool isLeapMonth;
  bool hasLeapMonthBetweenWinterSolstices;
};
struct MonthInfo computeMonthInfo(
    const icu::ChineseCalendar::Setting& setting,
    int32_t gyear, int32_t days);

}  // namespace

/**
 * Return the Julian day number of day before the first day of the
 * given month in the given extended year.
 * 
 * <p>Note: This method reads the IS_LEAP_MONTH field to determine
 * whether the given month is a leap month.
 * @param eyear the extended year
 * @param month the zero-based month.  The month is also determined
 * by reading the IS_LEAP_MONTH field.
 * @return the Julian day number of the day before the first
 * day of the given month and year
 * @stable ICU 2.8
 */
int64_t ChineseCalendar::handleComputeMonthStart(int32_t eyear, int32_t month, UBool useMonth, UErrorCode& status) const {
    if (U_FAILURE(status)) {
       return 0;
    }
    // If the month is out of range, adjust it into range, and
    // modify the extended year value accordingly.
    if (month < 0 || month > 11) {
        double m = month;
        if (uprv_add32_overflow(eyear, ClockMath::floorDivide(m, 12.0, &m), &eyear)) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return 0;
        }
        month = (int32_t)m;
    }

    const Setting setting = getSetting(status);
    if (U_FAILURE(status)) {
       return 0;
    }
    int32_t gyear;
    if (uprv_add32_overflow(eyear, setting.epochYear - 1, &gyear)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    int32_t theNewYear = newYear(setting, gyear);
    int32_t newMoon = newMoonNear(setting.zoneAstroCalc, theNewYear + month * 29, true);

    // Ignore IS_LEAP_MONTH field if useMonth is false
    bool isLeapMonth = false;
    if (useMonth) {
        isLeapMonth = internalGet(UCAL_IS_LEAP_MONTH) != 0;
    }

    int32_t unusedMonth;
    int32_t unusedDayOfWeek;
    int32_t unusedDayOfMonth;
    int32_t unusedDayOfYear;
    Grego::dayToFields(newMoon, gyear, unusedMonth, unusedDayOfWeek, unusedDayOfMonth, unusedDayOfYear);

    struct MonthInfo monthInfo = computeMonthInfo(setting, gyear, newMoon);
    if (month != monthInfo.month-1 || isLeapMonth != monthInfo.isLeapMonth) {
        newMoon = newMoonNear(setting.zoneAstroCalc, newMoon + SYNODIC_GAP, true);
    }
    int32_t julianDay;
    if (uprv_add32_overflow(newMoon-1, kEpochStartAsJulianDay, &julianDay)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    return julianDay;
}


/**
 * Override Calendar to handle leap months properly.
 * @stable ICU 2.8
 */
void ChineseCalendar::add(UCalendarDateFields field, int32_t amount, UErrorCode& status) {
    switch (field) {
    case UCAL_MONTH:
    case UCAL_ORDINAL_MONTH:
        if (amount != 0) {
            int32_t dom = get(UCAL_DAY_OF_MONTH, status);
            if (U_FAILURE(status)) break;
            int32_t day = get(UCAL_JULIAN_DAY, status) - kEpochStartAsJulianDay; // Get local day
            if (U_FAILURE(status)) break;
            int32_t moon = day - dom + 1; // New moon 
            offsetMonth(moon, dom, amount, status);
        }
        break;
    default:
        Calendar::add(field, amount, status);
        break;
    }
}

/**
 * Override Calendar to handle leap months properly.
 * @stable ICU 2.8
 */
void ChineseCalendar::add(EDateFields field, int32_t amount, UErrorCode& status) {
    add((UCalendarDateFields)field, amount, status);
}

namespace {

struct RollMonthInfo {
    int32_t month;
    int32_t newMoon;
    int32_t thisMoon;
};

struct RollMonthInfo rollMonth(const TimeZone* timeZone, int32_t amount, int32_t day, int32_t month, int32_t dayOfMonth,
                               bool isLeapMonth, bool hasLeapMonthBetweenWinterSolstices,
                               UErrorCode& status) {
    struct RollMonthInfo output = {0, 0, 0};
    if (U_FAILURE(status)) {
        return output;
    }

    output.thisMoon = day - dayOfMonth + 1; // New moon (start of this month)

    // Note throughout the following:  Months 12 and 1 are never
    // followed by a leap month (D&R p. 185).

    // Compute the adjusted month number m.  This is zero-based
    // value from 0..11 in a non-leap year, and from 0..12 in a
    // leap year.
    if (hasLeapMonthBetweenWinterSolstices) { // (member variable)
        if (isLeapMonth) {
            ++month;
        } else {
            // Check for a prior leap month.  (In the
            // following, month 0 is the first month of the
            // year.)  Month 0 is never followed by a leap
            // month, and we know month m is not a leap month.
            // moon1 will be the start of month 0 if there is
            // no leap month between month 0 and month m;
            // otherwise it will be the start of month 1.
            int prevMoon = output.thisMoon -
                (int) (CalendarAstronomer::SYNODIC_MONTH * (month - 0.5));
            prevMoon = newMoonNear(timeZone, prevMoon, true);
            if (isLeapMonthBetween(timeZone, prevMoon, output.thisMoon)) {
                ++month;
            }
        }
    }
    // Now do the standard roll computation on month, with the
    // allowed range of 0..n-1, where n is 12 or 13.
    int32_t numberOfMonths = hasLeapMonthBetweenWinterSolstices ? 13 : 12; // Months in this year
    if (uprv_add32_overflow(amount, month, &amount)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return output;
    }
    output.newMoon = amount % numberOfMonths;
    if (output.newMoon < 0) {
        output.newMoon += numberOfMonths;
    }
    output.month = month;
    return output;
}

}  // namespace

/**
 * Override Calendar to handle leap months properly.
 * @stable ICU 2.8
 */
void ChineseCalendar::roll(UCalendarDateFields field, int32_t amount, UErrorCode& status) {
    switch (field) {
    case UCAL_MONTH:
    case UCAL_ORDINAL_MONTH:
        if (amount != 0) {
            const Setting setting = getSetting(status);
            int32_t day = get(UCAL_JULIAN_DAY, status) - kEpochStartAsJulianDay; // Get local day
            int32_t month = get(UCAL_MONTH, status); // 0-based month
            int32_t dayOfMonth = get(UCAL_DAY_OF_MONTH, status);
            bool isLeapMonth = get(UCAL_IS_LEAP_MONTH, status) == 1;
            if (U_FAILURE(status)) break;
            struct RollMonthInfo r = rollMonth(setting.zoneAstroCalc, amount,
                                               day, month, dayOfMonth, isLeapMonth, hasLeapMonthBetweenWinterSolstices, status);
            if (U_FAILURE(status)) break;
            if (r.newMoon != r.month) {
                offsetMonth(r.thisMoon, dayOfMonth, r.newMoon - r.month, status);
            }
        }
        break;
    default:
        Calendar::roll(field, amount, status);
        break;
    }
}

void ChineseCalendar::roll(EDateFields field, int32_t amount, UErrorCode& status) {
    roll((UCalendarDateFields)field, amount, status);
}


//------------------------------------------------------------------
// Support methods and constants
//------------------------------------------------------------------

namespace {
/**
 * Convert local days to UTC epoch milliseconds.
 * This is not an accurate conversion in that getTimezoneOffset
 * takes the milliseconds in GMT (not local time). In theory, more
 * accurate algorithm can be implemented but practically we do not need
 * to go through that complication as long as the historical timezone
 * changes did not happen around the 'tricky' new moon (new moon around
 * midnight).
 *
 * @param timeZone time zone for the Astro calculation.
 * @param days days after January 1, 1970 0:00 in the astronomical base zone
 * @return milliseconds after January 1, 1970 0:00 GMT
 */
double daysToMillis(const TimeZone* timeZone, double days) {
    double millis = days * (double)kOneDay;
    if (timeZone != nullptr) {
        int32_t rawOffset, dstOffset;
        UErrorCode status = U_ZERO_ERROR;
        timeZone->getOffset(millis, false, rawOffset, dstOffset, status);
        if (U_SUCCESS(status)) {
            return millis - (double)(rawOffset + dstOffset);
        }
    }
    return millis - (double)CHINA_OFFSET;
}

/**
 * Convert UTC epoch milliseconds to local days.
 * @param timeZone time zone for the Astro calculation.
 * @param millis milliseconds after January 1, 1970 0:00 GMT
 * @return days after January 1, 1970 0:00 in the astronomical base zone
 */
double millisToDays(const TimeZone* timeZone, double millis) {
    if (timeZone != nullptr) {
        int32_t rawOffset, dstOffset;
        UErrorCode status = U_ZERO_ERROR;
        timeZone->getOffset(millis, false, rawOffset, dstOffset, status);
        if (U_SUCCESS(status)) {
            return ClockMath::floorDivide(millis + (double)(rawOffset + dstOffset), kOneDay);
        }
    }
    return ClockMath::floorDivide(millis + (double)CHINA_OFFSET, kOneDay);
}

//------------------------------------------------------------------
// Astronomical computations
//------------------------------------------------------------------


/**
 * Return the major solar term on or after December 15 of the given
 * Gregorian year, that is, the winter solstice of the given year.
 * Computations are relative to Asia/Shanghai time zone.
 * @param setting setting (time zone and caches) for the Astro calculation.
 * @param gyear a Gregorian year
 * @return days after January 1, 1970 0:00 Asia/Shanghai of the
 * winter solstice of the given year
 */
int32_t winterSolstice(const icu::ChineseCalendar::Setting& setting,
                       int32_t gyear) {
    const TimeZone* timeZone = setting.zoneAstroCalc;

    UErrorCode status = U_ZERO_ERROR;
    int32_t cacheValue = CalendarCache::get(setting.winterSolsticeCache, gyear, status);

    if (cacheValue == 0) {
        // In books December 15 is used, but it fails for some years
        // using our algorithms, e.g.: 1298 1391 1492 1553 1560.  That
        // is, winterSolstice(1298) starts search at Dec 14 08:00:00
        // PST 1298 with a final result of Dec 14 10:31:59 PST 1299.
        double ms = daysToMillis(timeZone, Grego::fieldsToDay(gyear, UCAL_DECEMBER, 1));

        // Winter solstice is 270 degrees solar longitude aka Dongzhi
        double days = millisToDays(timeZone,
                                   CalendarAstronomer(ms)
                                       .getSunTime(CalendarAstronomer::WINTER_SOLSTICE(), true));
        if (days < INT32_MIN || days > INT32_MAX) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return 0;
        }
        cacheValue = (int32_t) days;
        CalendarCache::put(setting.winterSolsticeCache, gyear, cacheValue, status);
    }
    if(U_FAILURE(status)) {
        cacheValue = 0;
    }
    return cacheValue;
}

/**
 * Return the closest new moon to the given date, searching either
 * forward or backward in time.
 * @param timeZone time zone for the Astro calculation.
 * @param days days after January 1, 1970 0:00 Asia/Shanghai
 * @param after if true, search for a new moon on or after the given
 * date; otherwise, search for a new moon before it
 * @return days after January 1, 1970 0:00 Asia/Shanghai of the nearest
 * new moon after or before <code>days</code>
 */
int32_t newMoonNear(const TimeZone* timeZone, double days, UBool after) {
    return (int32_t) millisToDays(
        timeZone,
        CalendarAstronomer(daysToMillis(timeZone, days))
              .getMoonTime(CalendarAstronomer::NEW_MOON(), after));
}

/**
 * Return the nearest integer number of synodic months between
 * two dates.
 * @param day1 days after January 1, 1970 0:00 Asia/Shanghai
 * @param day2 days after January 1, 1970 0:00 Asia/Shanghai
 * @return the nearest integer number of months between day1 and day2
 */
int32_t synodicMonthsBetween(int32_t day1, int32_t day2) {
    double roundme = ((day2 - day1) / CalendarAstronomer::SYNODIC_MONTH);
    return (int32_t) (roundme + (roundme >= 0 ? .5 : -.5));
}

/**
 * Return the major solar term on or before a given date.  This
 * will be an integer from 1..12, with 1 corresponding to 330 degrees,
 * 2 to 0 degrees, 3 to 30 degrees,..., and 12 to 300 degrees.
 * @param timeZone time zone for the Astro calculation.
 * @param days days after January 1, 1970 0:00 Asia/Shanghai
 */
int32_t majorSolarTerm(const TimeZone* timeZone, int32_t days) {
    // Compute (floor(solarLongitude / (pi/6)) + 2) % 12
    int32_t term = ( ((int32_t)(6 * CalendarAstronomer(daysToMillis(timeZone, days))
                                .getSunLongitude() / CalendarAstronomer::PI)) + 2 ) % 12;
    if (term < 1) {
        term += 12;
    }
    return term;
}

/**
 * Return true if the given month lacks a major solar term.
 * @param timeZone time zone for the Astro calculation.
 * @param newMoon days after January 1, 1970 0:00 Asia/Shanghai of a new
 * moon
 */
UBool hasNoMajorSolarTerm(const TimeZone* timeZone, int32_t newMoon) {
    return majorSolarTerm(timeZone, newMoon) ==
        majorSolarTerm(timeZone, newMoonNear(timeZone, newMoon + SYNODIC_GAP, true));
}


//------------------------------------------------------------------
// Time to fields
//------------------------------------------------------------------

/**
 * Return true if there is a leap month on or after month newMoon1 and
 * at or before month newMoon2.
 * @param timeZone time zone for the Astro calculation.
 * @param newMoon1 days after January 1, 1970 0:00 astronomical base zone
 * of a new moon
 * @param newMoon2 days after January 1, 1970 0:00 astronomical base zone
 * of a new moon
 */
UBool isLeapMonthBetween(const TimeZone* timeZone, int32_t newMoon1, int32_t newMoon2) {

#ifdef U_DEBUG_CHNSECAL
    // This is only needed to debug the timeOfAngle divergence bug.
    // Remove this later. Liu 11/9/00
    if (synodicMonthsBetween(newMoon1, newMoon2) >= 50) {
        U_DEBUG_CHNSECAL_MSG((
            "isLeapMonthBetween(%d, %d): Invalid parameters", newMoon1, newMoon2
            ));
    }
#endif

    while (newMoon2 >= newMoon1) {
        if (hasNoMajorSolarTerm(timeZone, newMoon2)) {
            return true;
        }
        newMoon2 = newMoonNear(timeZone, newMoon2 - SYNODIC_GAP, false);
    }
    return false;
}


/**
 * Compute the information about the year.
 * @param setting setting (time zone and caches) for the Astro calculation.
 * @param gyear the Gregorian year of the given date
 * @param days days after January 1, 1970 0:00 astronomical base zone
 * of the date to compute fields for
 * @return The MonthInfo result.
 */
struct MonthInfo computeMonthInfo(
    const icu::ChineseCalendar::Setting& setting,
    int32_t gyear, int32_t days) {
    struct MonthInfo output;
    // Find the winter solstices before and after the target date.
    // These define the boundaries of this Chinese year, specifically,
    // the position of month 11, which always contains the solstice.
    // We want solsticeBefore <= date < solsticeAfter.
    int32_t solsticeBefore;
    int32_t solsticeAfter = winterSolstice(setting, gyear);
    if (days < solsticeAfter) {
        solsticeBefore = winterSolstice(setting, gyear - 1);
    } else {
        solsticeBefore = solsticeAfter;
        solsticeAfter = winterSolstice(setting, gyear + 1);
    }

    const TimeZone* timeZone = setting.zoneAstroCalc;
    // Find the start of the month after month 11.  This will be either
    // the prior month 12 or leap month 11 (very rare).  Also find the
    // start of the following month 11.
    int32_t firstMoon = newMoonNear(timeZone, solsticeBefore + 1, true);
    int32_t lastMoon = newMoonNear(timeZone, solsticeAfter + 1, false);
    output.thisMoon = newMoonNear(timeZone, days + 1, false); // Start of this month
    output.hasLeapMonthBetweenWinterSolstices = synodicMonthsBetween(firstMoon, lastMoon) == 12;

    output.month = synodicMonthsBetween(firstMoon, output.thisMoon);
    int32_t theNewYear = newYear(setting, gyear);
    if (days < theNewYear) {
        theNewYear = newYear(setting, gyear-1);
    }
    if (output.hasLeapMonthBetweenWinterSolstices &&
        isLeapMonthBetween(timeZone, firstMoon, output.thisMoon)) {
        output.month--;
    }
    if (output.month < 1) {
        output.month += 12;
    }
    output.ordinalMonth = synodicMonthsBetween(theNewYear, output.thisMoon);
    if (output.ordinalMonth < 0) {
        output.ordinalMonth += 12;
    }
    output.isLeapMonth = output.hasLeapMonthBetweenWinterSolstices &&
        hasNoMajorSolarTerm(timeZone, output.thisMoon) &&
        !isLeapMonthBetween(timeZone, firstMoon,
                            newMoonNear(timeZone, output.thisMoon - SYNODIC_GAP, false));
    return output;
}

}  // namespace

/**
 * Override Calendar to compute several fields specific to the Chinese
 * calendar system.  These are:
 *
 * <ul><li>ERA
 * <li>YEAR
 * <li>MONTH
 * <li>DAY_OF_MONTH
 * <li>DAY_OF_YEAR
 * <li>EXTENDED_YEAR</ul>
 * 
 * The DAY_OF_WEEK and DOW_LOCAL fields are already set when this
 * method is called.  The getGregorianXxx() methods return Gregorian
 * calendar equivalents for the given Julian day.
 *
 * <p>Compute the ChineseCalendar-specific field IS_LEAP_MONTH.
 * @stable ICU 2.8
 */
void ChineseCalendar::handleComputeFields(int32_t julianDay, UErrorCode & status) {
    if (U_FAILURE(status)) {
        return;
    }
    int32_t days;
    if (uprv_add32_overflow(julianDay, -kEpochStartAsJulianDay, &days)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    int32_t gyear = getGregorianYear();
    int32_t gmonth = getGregorianMonth();

    const Setting setting = getSetting(status);
    if (U_FAILURE(status)) {
       return;
    }
    struct MonthInfo monthInfo = computeMonthInfo(setting, gyear, days);
    hasLeapMonthBetweenWinterSolstices = monthInfo.hasLeapMonthBetweenWinterSolstices;

    // Extended year and cycle year is based on the epoch year
    int32_t eyear = gyear - setting.epochYear;
    int32_t cycle_year = gyear - CHINESE_EPOCH_YEAR;
    if (monthInfo.month < 11 ||
        gmonth >= UCAL_JULY) {
        eyear++;
        cycle_year++;
    }
    int32_t dayOfMonth = days - monthInfo.thisMoon + 1;

    // 0->0,60  1->1,1  60->1,60  61->2,1  etc.
    int32_t yearOfCycle;
    int32_t cycle = ClockMath::floorDivide(cycle_year - 1, 60, &yearOfCycle);

    // Days will be before the first new year we compute if this
    // date is in month 11, leap 11, 12.  There is never a leap 12.
    // New year computations are cached so this should be cheap in
    // the long run.
    int32_t theNewYear = newYear(setting, gyear);
    if (days < theNewYear) {
        theNewYear = newYear(setting, gyear-1);
    }
    cycle++;
    yearOfCycle++;
    int32_t dayOfYear = days - theNewYear + 1;

    int32_t minYear = this->handleGetLimit(UCAL_EXTENDED_YEAR, UCAL_LIMIT_MINIMUM);
    if (eyear < minYear) {
        if (!isLenient()) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        eyear = minYear;
    }
    int32_t maxYear = this->handleGetLimit(UCAL_EXTENDED_YEAR, UCAL_LIMIT_MAXIMUM);
    if (maxYear < eyear) {
        if (!isLenient()) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        eyear = maxYear;
    }

    internalSet(UCAL_MONTH, monthInfo.month-1); // Convert from 1-based to 0-based
    internalSet(UCAL_ORDINAL_MONTH, monthInfo.ordinalMonth); // Convert from 1-based to 0-based
    internalSet(UCAL_IS_LEAP_MONTH, monthInfo.isLeapMonth?1:0);

    internalSet(UCAL_EXTENDED_YEAR, eyear);
    internalSet(UCAL_ERA, cycle);
    internalSet(UCAL_YEAR, yearOfCycle);
    internalSet(UCAL_DAY_OF_MONTH, dayOfMonth);
    internalSet(UCAL_DAY_OF_YEAR, dayOfYear);
}

//------------------------------------------------------------------
// Fields to time
//------------------------------------------------------------------

namespace {

/**
 * Return the Chinese new year of the given Gregorian year.
 * @param setting setting (time zone and caches) for the Astro calculation.
 * @param gyear a Gregorian year
 * @return days after January 1, 1970 0:00 astronomical base zone of the
 * Chinese new year of the given year (this will be a new moon)
 */
int32_t newYear(const icu::ChineseCalendar::Setting& setting,
                int32_t gyear) {
    const TimeZone* timeZone = setting.zoneAstroCalc;
    UErrorCode status = U_ZERO_ERROR;
    int32_t cacheValue = CalendarCache::get(setting.newYearCache, gyear, status);

    if (cacheValue == 0) {

        int32_t solsticeBefore= winterSolstice(setting, gyear - 1);
        int32_t solsticeAfter = winterSolstice(setting, gyear);
        int32_t newMoon1 = newMoonNear(timeZone, solsticeBefore + 1, true);
        int32_t newMoon2 = newMoonNear(timeZone, newMoon1 + SYNODIC_GAP, true);
        int32_t newMoon11 = newMoonNear(timeZone, solsticeAfter + 1, false);

        if (synodicMonthsBetween(newMoon1, newMoon11) == 12 &&
            (hasNoMajorSolarTerm(timeZone, newMoon1) ||
             hasNoMajorSolarTerm(timeZone, newMoon2))) {
            cacheValue = newMoonNear(timeZone, newMoon2 + SYNODIC_GAP, true);
        } else {
            cacheValue = newMoon2;
        }

        CalendarCache::put(setting.newYearCache, gyear, cacheValue, status);
    }
    if(U_FAILURE(status)) {
        cacheValue = 0;
    }
    return cacheValue;
}

}  // namespace

/**
 * Adjust this calendar to be delta months before or after a given
 * start position, pinning the day of month if necessary.  The start
 * position is given as a local days number for the start of the month
 * and a day-of-month.  Used by add() and roll().
 * @param newMoon the local days of the first day of the month of the
 * start position (days after January 1, 1970 0:00 Asia/Shanghai)
 * @param dayOfMonth the 1-based day-of-month of the start position
 * @param delta the number of months to move forward or backward from
 * the start position
 * @param status The status.
 */
void ChineseCalendar::offsetMonth(int32_t newMoon, int32_t dayOfMonth, int32_t delta,
                                  UErrorCode& status) {
    const Setting setting = getSetting(status);
    if (U_FAILURE(status)) { return; }

    // Move to the middle of the month before our target month.
    double value = newMoon;
    value += (CalendarAstronomer::SYNODIC_MONTH *
                          (static_cast<double>(delta) - 0.5));
    if (value < INT32_MIN || value > INT32_MAX) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    newMoon = static_cast<int32_t>(value);

    // Search forward to the target month's new moon
    newMoon = newMoonNear(setting.zoneAstroCalc, newMoon, true);

    // Find the target dayOfMonth
    int32_t jd = newMoon + kEpochStartAsJulianDay - 1 + dayOfMonth;

    // Pin the dayOfMonth.  In this calendar all months are 29 or 30 days
    // so pinning just means handling dayOfMonth 30.
    if (dayOfMonth > 29) {
        set(UCAL_JULIAN_DAY, jd-1);
        // TODO Fix this.  We really shouldn't ever have to
        // explicitly call complete().  This is either a bug in
        // this method, in ChineseCalendar, or in
        // Calendar.getActualMaximum().  I suspect the last.
        complete(status);
        if (U_FAILURE(status)) return;
        if (getActualMaximum(UCAL_DAY_OF_MONTH, status) >= dayOfMonth) {
            if (U_FAILURE(status)) return;
            set(UCAL_JULIAN_DAY, jd);
        }
    } else {
        set(UCAL_JULIAN_DAY, jd);
    }
}

constexpr uint32_t kChineseRelatedYearDiff = -2637;

int32_t ChineseCalendar::getRelatedYear(UErrorCode &status) const
{
    int32_t year = get(UCAL_EXTENDED_YEAR, status);
    if (U_FAILURE(status)) {
        return 0;
    }
    if (uprv_add32_overflow(year, kChineseRelatedYearDiff, &year)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    return year;
}

void ChineseCalendar::setRelatedYear(int32_t year)
{
    // set extended year
    set(UCAL_EXTENDED_YEAR, year - kChineseRelatedYearDiff);
}

IMPL_SYSTEM_DEFAULT_CENTURY(ChineseCalendar, "@calendar=chinese")

bool
ChineseCalendar::inTemporalLeapYear(UErrorCode &status) const
{
    int32_t days = getActualMaximum(UCAL_DAY_OF_YEAR, status);
    if (U_FAILURE(status)) return false;
    return days > 360;
}

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(ChineseCalendar)


static const char * const gTemporalLeapMonthCodes[] = {
    "M01L", "M02L", "M03L", "M04L", "M05L", "M06L",
    "M07L", "M08L", "M09L", "M10L", "M11L", "M12L", nullptr
};

const char* ChineseCalendar::getTemporalMonthCode(UErrorCode &status) const {
    // We need to call get, not internalGet, to force the calculation
    // from UCAL_ORDINAL_MONTH.
    int32_t is_leap = get(UCAL_IS_LEAP_MONTH, status);
    if (U_FAILURE(status)) return nullptr;
    if (is_leap != 0) {
        int32_t month = get(UCAL_MONTH, status);
        if (U_FAILURE(status)) return nullptr;
        return gTemporalLeapMonthCodes[month];
    }
    return Calendar::getTemporalMonthCode(status);
}

void
ChineseCalendar::setTemporalMonthCode(const char* code, UErrorCode& status )
{
    if (U_FAILURE(status)) return;
    int32_t len = static_cast<int32_t>(uprv_strlen(code));
    if (len != 4 || code[0] != 'M' || code[3] != 'L') {
        set(UCAL_IS_LEAP_MONTH, 0);
        return Calendar::setTemporalMonthCode(code, status);
    }
    for (int m = 0; gTemporalLeapMonthCodes[m] != nullptr; m++) {
        if (uprv_strcmp(code, gTemporalLeapMonthCodes[m]) == 0) {
            set(UCAL_MONTH, m);
            set(UCAL_IS_LEAP_MONTH, 1);
            return;
        }
    }
    status = U_ILLEGAL_ARGUMENT_ERROR;
}

int32_t ChineseCalendar::internalGetMonth(UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return 0;
    }
    if (resolveFields(kMonthPrecedence) == UCAL_MONTH) {
        return internalGet(UCAL_MONTH);
    }
    LocalPointer<Calendar> temp(this->clone());
    temp->set(UCAL_MONTH, 0);
    temp->set(UCAL_IS_LEAP_MONTH, 0);
    temp->set(UCAL_DATE, 1);
    // Calculate the UCAL_MONTH and UCAL_IS_LEAP_MONTH by adding number of
    // months.
    temp->roll(UCAL_MONTH, internalGet(UCAL_ORDINAL_MONTH), status);
    if (U_FAILURE(status)) {
        return 0;
    }

    ChineseCalendar *nonConstThis = (ChineseCalendar*)this; // cast away const
    nonConstThis->internalSet(UCAL_IS_LEAP_MONTH, temp->get(UCAL_IS_LEAP_MONTH, status));
    int32_t month = temp->get(UCAL_MONTH, status);
    if (U_FAILURE(status)) {
        return 0;
    }
    nonConstThis->internalSet(UCAL_MONTH, month);
    return month;
}

int32_t ChineseCalendar::internalGetMonth(int32_t defaultValue, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return 0;
    }
    if (resolveFields(kMonthPrecedence) == UCAL_MONTH) {
        return internalGet(UCAL_MONTH, defaultValue);
    }
    return internalGetMonth(status);
}

ChineseCalendar::Setting ChineseCalendar::getSetting(UErrorCode&) const {
  return {
        CHINESE_EPOCH_YEAR,
        getAstronomerTimeZone(),
        &gWinterSolsticeCache,
        &gNewYearCache
  };
}

U_NAMESPACE_END

#endif

