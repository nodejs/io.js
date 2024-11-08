// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 1997-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File GREGOCAL.CPP
*
* Modification History:
*
*   Date        Name        Description
*   02/05/97    clhuang     Creation.
*   03/28/97    aliu        Made highly questionable fix to computeFields to
*                           handle DST correctly.
*   04/22/97    aliu        Cleaned up code drastically.  Added monthLength().
*                           Finished unimplemented parts of computeTime() for
*                           week-based date determination.  Removed quetionable
*                           fix and wrote correct fix for computeFields() and
*                           daylight time handling.  Rewrote inDaylightTime()
*                           and computeFields() to handle sensitive Daylight to
*                           Standard time transitions correctly.
*   05/08/97    aliu        Added code review changes.  Fixed isLeapYear() to
*                           not cutover.
*   08/12/97    aliu        Added equivalentTo.  Misc other fixes.  Updated
*                           add() from Java source.
*    07/28/98    stephen        Sync up with JDK 1.2
*    09/14/98    stephen        Changed type of kOneDay, kOneWeek to double.
*                            Fixed bug in roll() 
*   10/15/99    aliu        Fixed j31, incorrect WEEK_OF_YEAR computation.
*   10/15/99    aliu        Fixed j32, cannot set date to Feb 29 2000 AD.
*                           {JDK bug 4210209 4209272}
*   11/15/99    weiv        Added YEAR_WOY and DOW_LOCAL computation
*                           to timeToFields method, updated kMinValues, kMaxValues & kLeastMaxValues
*   12/09/99    aliu        Fixed j81, calculation errors and roll bugs
*                           in year of cutover.
*   01/24/2000  aliu        Revised computeJulianDay for YEAR YEAR_WOY WOY.
********************************************************************************
*/

#include "unicode/utypes.h"
#include <float.h>

#if !UCONFIG_NO_FORMATTING

#include "unicode/gregocal.h"
#include "gregoimp.h"
#include "umutex.h"
#include "uassert.h"

// *****************************************************************************
// class GregorianCalendar
// *****************************************************************************

/**
* Note that the Julian date used here is not a true Julian date, since
* it is measured from midnight, not noon.  This value is the Julian
* day number of January 1, 1970 (Gregorian calendar) at noon UTC. [LIU]
*/

static const int16_t kNumDays[]
= {0,31,59,90,120,151,181,212,243,273,304,334}; // 0-based, for day-in-year
static const int16_t kLeapNumDays[]
= {0,31,60,91,121,152,182,213,244,274,305,335}; // 0-based, for day-in-year
static const int8_t kMonthLength[]
= {31,28,31,30,31,30,31,31,30,31,30,31}; // 0-based
static const int8_t kLeapMonthLength[]
= {31,29,31,30,31,30,31,31,30,31,30,31}; // 0-based

// setTimeInMillis() limits the Julian day range to +/-7F000000.
// This would seem to limit the year range to:
//  ms=+183882168921600000  jd=7f000000  December 20, 5828963 AD
//  ms=-184303902528000000  jd=81000000  September 20, 5838270 BC
// HOWEVER, CalendarRegressionTest/Test4167060 shows that the actual
// range limit on the year field is smaller (~ +/-140000). [alan 3.0]

static const int32_t kGregorianCalendarLimits[UCAL_FIELD_COUNT][4] = {
    // Minimum  Greatest    Least  Maximum
    //           Minimum  Maximum
    {        0,        0,        1,        1}, // ERA
    {        1,        1,   140742,   144683}, // YEAR
    {        0,        0,       11,       11}, // MONTH
    {        1,        1,       52,       53}, // WEEK_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // WEEK_OF_MONTH
    {        1,        1,       28,       31}, // DAY_OF_MONTH
    {        1,        1,      365,      366}, // DAY_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DAY_OF_WEEK
    {       -1,       -1,        4,        5}, // DAY_OF_WEEK_IN_MONTH
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // AM_PM
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // HOUR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // HOUR_OF_DAY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MINUTE
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // SECOND
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MILLISECOND
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // ZONE_OFFSET
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DST_OFFSET
    {  -140742,  -140742,   140742,   144683}, // YEAR_WOY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DOW_LOCAL
    {  -140742,  -140742,   140742,   144683}, // EXTENDED_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // JULIAN_DAY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // MILLISECONDS_IN_DAY
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // IS_LEAP_MONTH
    {        0,        0,       11,       11}, // ORDINAL_MONTH
};

/*
* <pre>
*                            Greatest       Least 
* Field name        Minimum   Minimum     Maximum     Maximum
* ----------        -------   -------     -------     -------
* ERA                     0         0           1           1
* YEAR                    1         1      140742      144683
* MONTH                   0         0          11          11
* WEEK_OF_YEAR            1         1          52          53
* WEEK_OF_MONTH           0         0           4           6
* DAY_OF_MONTH            1         1          28          31
* DAY_OF_YEAR             1         1         365         366
* DAY_OF_WEEK             1         1           7           7
* DAY_OF_WEEK_IN_MONTH   -1        -1           4           5
* AM_PM                   0         0           1           1
* HOUR                    0         0          11          11
* HOUR_OF_DAY             0         0          23          23
* MINUTE                  0         0          59          59
* SECOND                  0         0          59          59
* MILLISECOND             0         0         999         999
* ZONE_OFFSET           -12*      -12*         12*         12*
* DST_OFFSET              0         0           1*          1*
* YEAR_WOY                1         1      140742      144683
* DOW_LOCAL               1         1           7           7
* </pre>
* (*) In units of one-hour
*/

#if defined( U_DEBUG_CALSVC ) || defined (U_DEBUG_CAL)
#include <stdio.h>
#endif

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(GregorianCalendar)

// 00:00:00 UTC, October 15, 1582, expressed in ms from the epoch.
// Note that only Italy and other Catholic countries actually
// observed this cutover.  Most other countries followed in
// the next few centuries, some as late as 1928. [LIU]
// in Java, -12219292800000L
//const UDate GregorianCalendar::kPapalCutover = -12219292800000L;
static const uint32_t kCutoverJulianDay = 2299161;
static const UDate kPapalCutover = (2299161.0 - kEpochStartAsJulianDay) * U_MILLIS_PER_DAY;
//static const UDate kPapalCutoverJulian = (2299161.0 - kEpochStartAsJulianDay);

// -------------------------------------

GregorianCalendar::GregorianCalendar(UErrorCode& status)
:   Calendar(status),
fGregorianCutover(kPapalCutover),
fCutoverJulianDay(kCutoverJulianDay), fNormalizedGregorianCutover(fGregorianCutover), fGregorianCutoverYear(1582),
fIsGregorian(true), fInvertGregorian(false)
{
    setTimeInMillis(getNow(), status);
}

// -------------------------------------

GregorianCalendar::GregorianCalendar(TimeZone* zone, UErrorCode& status)
:   Calendar(zone, Locale::getDefault(), status),
fGregorianCutover(kPapalCutover),
fCutoverJulianDay(kCutoverJulianDay), fNormalizedGregorianCutover(fGregorianCutover), fGregorianCutoverYear(1582),
fIsGregorian(true), fInvertGregorian(false)
{
    setTimeInMillis(getNow(), status);
}

// -------------------------------------

GregorianCalendar::GregorianCalendar(const TimeZone& zone, UErrorCode& status)
:   Calendar(zone, Locale::getDefault(), status),
fGregorianCutover(kPapalCutover),
fCutoverJulianDay(kCutoverJulianDay), fNormalizedGregorianCutover(fGregorianCutover), fGregorianCutoverYear(1582),
fIsGregorian(true), fInvertGregorian(false)
{
    setTimeInMillis(getNow(), status);
}

// -------------------------------------

GregorianCalendar::GregorianCalendar(const Locale& aLocale, UErrorCode& status)
:   Calendar(TimeZone::forLocaleOrDefault(aLocale), aLocale, status),
fGregorianCutover(kPapalCutover),
fCutoverJulianDay(kCutoverJulianDay), fNormalizedGregorianCutover(fGregorianCutover), fGregorianCutoverYear(1582),
fIsGregorian(true), fInvertGregorian(false)
{
    setTimeInMillis(getNow(), status);
}

// -------------------------------------

GregorianCalendar::GregorianCalendar(TimeZone* zone, const Locale& aLocale,
                                     UErrorCode& status)
                                     :   Calendar(zone, aLocale, status),
                                     fGregorianCutover(kPapalCutover),
                                     fCutoverJulianDay(kCutoverJulianDay), fNormalizedGregorianCutover(fGregorianCutover), fGregorianCutoverYear(1582),
                                     fIsGregorian(true), fInvertGregorian(false)
{
    setTimeInMillis(getNow(), status);
}

// -------------------------------------

GregorianCalendar::GregorianCalendar(const TimeZone& zone, const Locale& aLocale,
                                     UErrorCode& status)
                                     :   Calendar(zone, aLocale, status),
                                     fGregorianCutover(kPapalCutover),
                                     fCutoverJulianDay(kCutoverJulianDay), fNormalizedGregorianCutover(fGregorianCutover), fGregorianCutoverYear(1582),
                                     fIsGregorian(true), fInvertGregorian(false)
{
    setTimeInMillis(getNow(), status);
}

// -------------------------------------

GregorianCalendar::GregorianCalendar(int32_t year, int32_t month, int32_t date,
                                     UErrorCode& status)
                                     :   Calendar(TimeZone::createDefault(), Locale::getDefault(), status),
                                     fGregorianCutover(kPapalCutover),
                                     fCutoverJulianDay(kCutoverJulianDay), fNormalizedGregorianCutover(fGregorianCutover), fGregorianCutoverYear(1582),
                                     fIsGregorian(true), fInvertGregorian(false)
{
    set(UCAL_ERA, AD);
    set(UCAL_YEAR, year);
    set(UCAL_MONTH, month);
    set(UCAL_DATE, date);
}

// -------------------------------------

GregorianCalendar::GregorianCalendar(int32_t year, int32_t month, int32_t date,
                                     int32_t hour, int32_t minute, UErrorCode& status)
                                     :   Calendar(TimeZone::createDefault(), Locale::getDefault(), status),
                                     fGregorianCutover(kPapalCutover),
                                     fCutoverJulianDay(kCutoverJulianDay), fNormalizedGregorianCutover(fGregorianCutover), fGregorianCutoverYear(1582),
                                     fIsGregorian(true), fInvertGregorian(false)
{
    set(UCAL_ERA, AD);
    set(UCAL_YEAR, year);
    set(UCAL_MONTH, month);
    set(UCAL_DATE, date);
    set(UCAL_HOUR_OF_DAY, hour);
    set(UCAL_MINUTE, minute);
}

// -------------------------------------

GregorianCalendar::GregorianCalendar(int32_t year, int32_t month, int32_t date,
                                     int32_t hour, int32_t minute, int32_t second,
                                     UErrorCode& status)
                                     :   Calendar(TimeZone::createDefault(), Locale::getDefault(), status),
                                     fGregorianCutover(kPapalCutover),
                                     fCutoverJulianDay(kCutoverJulianDay), fNormalizedGregorianCutover(fGregorianCutover), fGregorianCutoverYear(1582),
                                     fIsGregorian(true), fInvertGregorian(false)
{
    set(UCAL_ERA, AD);
    set(UCAL_YEAR, year);
    set(UCAL_MONTH, month);
    set(UCAL_DATE, date);
    set(UCAL_HOUR_OF_DAY, hour);
    set(UCAL_MINUTE, minute);
    set(UCAL_SECOND, second);
}

// -------------------------------------

GregorianCalendar::~GregorianCalendar()
{
}

// -------------------------------------

GregorianCalendar::GregorianCalendar(const GregorianCalendar &source)
:   Calendar(source),
fGregorianCutover(source.fGregorianCutover),
fCutoverJulianDay(source.fCutoverJulianDay), fNormalizedGregorianCutover(source.fNormalizedGregorianCutover), fGregorianCutoverYear(source.fGregorianCutoverYear),
fIsGregorian(source.fIsGregorian), fInvertGregorian(source.fInvertGregorian)
{
}

// -------------------------------------

GregorianCalendar* GregorianCalendar::clone() const
{
    return new GregorianCalendar(*this);
}

// -------------------------------------

GregorianCalendar &
GregorianCalendar::operator=(const GregorianCalendar &right)
{
    if (this != &right)
    {
        Calendar::operator=(right);
        fGregorianCutover = right.fGregorianCutover;
        fNormalizedGregorianCutover = right.fNormalizedGregorianCutover;
        fGregorianCutoverYear = right.fGregorianCutoverYear;
        fCutoverJulianDay = right.fCutoverJulianDay;
    }
    return *this;
}

// -------------------------------------

UBool GregorianCalendar::isEquivalentTo(const Calendar& other) const
{
    // Calendar override.
    return Calendar::isEquivalentTo(other) &&
        fGregorianCutover == ((GregorianCalendar*)&other)->fGregorianCutover;
}

// -------------------------------------

void
GregorianCalendar::setGregorianChange(UDate date, UErrorCode& status)
{
    if (U_FAILURE(status)) 
        return;

    // Precompute two internal variables which we use to do the actual
    // cutover computations.  These are the normalized cutover, which is the
    // midnight at or before the cutover, and the cutover year.  The
    // normalized cutover is in pure date milliseconds; it contains no time
    // of day or timezone component, and it used to compare against other
    // pure date values.
    double cutoverDay = ClockMath::floorDivide(date, kOneDay);

    // Handle the rare case of numeric overflow where the user specifies a time
    // outside of INT32_MIN .. INT32_MAX number of days.
    
    if (cutoverDay <= INT32_MIN) {
        cutoverDay = INT32_MIN;
        fGregorianCutover = fNormalizedGregorianCutover = cutoverDay * kOneDay;
    } else if (cutoverDay >= INT32_MAX) {
        cutoverDay = INT32_MAX;
        fGregorianCutover = fNormalizedGregorianCutover = cutoverDay * kOneDay;
    } else {
        fNormalizedGregorianCutover = cutoverDay * kOneDay;
        fGregorianCutover = date;
    }

    // Normalize the year so BC values are represented as 0 and negative
    // values.
    GregorianCalendar *cal = new GregorianCalendar(getTimeZone(), status);
    /* test for nullptr */
    if (cal == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    if(U_FAILURE(status)) {
        return;
    }
    cal->setTime(date, status);
    fGregorianCutoverYear = cal->get(UCAL_YEAR, status);
    if (cal->get(UCAL_ERA, status) == BC) {
        fGregorianCutoverYear = 1 - fGregorianCutoverYear;
    }
    fCutoverJulianDay = static_cast<int32_t>(cutoverDay);
    delete cal;
}

void GregorianCalendar::handleComputeFields(int32_t julianDay, UErrorCode& status) {
    int32_t eyear, month, dayOfMonth, dayOfYear, unusedRemainder;

    if(U_FAILURE(status)) {
        return;
    }

#if defined (U_DEBUG_CAL)
    fprintf(stderr, "%s:%d: jd%d- (greg's %d)- [cut=%d]\n", 
        __FILE__, __LINE__, julianDay, getGregorianDayOfYear(), fCutoverJulianDay);
#endif

    if (julianDay >= fCutoverJulianDay) {
        month = getGregorianMonth();
        dayOfMonth = getGregorianDayOfMonth();
        dayOfYear = getGregorianDayOfYear();
        eyear = getGregorianYear();
    } else {
        // The Julian epoch day (not the same as Julian Day)
        // is zero on Saturday December 30, 0 (Gregorian).
        int32_t julianEpochDay = julianDay - (kJan1_1JulianDay - 2);
		eyear = static_cast<int32_t>(ClockMath::floorDivide((4.0 * julianEpochDay) + 1464.0, static_cast<int32_t>(1461), &unusedRemainder));

        // Compute the Julian calendar day number for January 1, eyear
        int32_t january1 = 365 * (eyear - 1) + ClockMath::floorDivide(eyear - 1, static_cast<int32_t>(4));
        dayOfYear = (julianEpochDay - january1); // 0-based

        // Julian leap years occurred historically every 4 years starting
        // with 8 AD.  Before 8 AD the spacing is irregular; every 3 years
        // from 45 BC to 9 BC, and then none until 8 AD.  However, we don't
        // implement this historical detail; instead, we implement the
        // computationally cleaner proleptic calendar, which assumes
        // consistent 4-year cycles throughout time.
        UBool isLeap = ((eyear&0x3) == 0); // equiv. to (eyear%4 == 0)

        // Common Julian/Gregorian calculation
        int32_t correction = 0;
        int32_t march1 = isLeap ? 60 : 59; // zero-based DOY for March 1
        if (dayOfYear >= march1) {
            correction = isLeap ? 1 : 2;
        }
        month = (12 * (dayOfYear + correction) + 6) / 367; // zero-based month
        dayOfMonth = dayOfYear - (isLeap?kLeapNumDays[month]:kNumDays[month]) + 1; // one-based DOM
        ++dayOfYear;
#if defined (U_DEBUG_CAL)
        //     fprintf(stderr, "%d - %d[%d] + 1\n", dayOfYear, isLeap?kLeapNumDays[month]:kNumDays[month], month );
        //           fprintf(stderr, "%s:%d:  greg's HCF %d -> %d/%d/%d not %d/%d/%d\n", 
        //                   __FILE__, __LINE__,julianDay,
        //          eyear,month,dayOfMonth,
        //          getGregorianYear(), getGregorianMonth(), getGregorianDayOfMonth()  );
        fprintf(stderr, "%s:%d: doy %d (greg's %d)- [cut=%d]\n", 
            __FILE__, __LINE__, dayOfYear, getGregorianDayOfYear(), fCutoverJulianDay);
#endif

    }

    // [j81] if we are after the cutover in its year, shift the day of the year
    if((eyear == fGregorianCutoverYear) && (julianDay >= fCutoverJulianDay)) {
        //from handleComputeMonthStart
        int32_t gregShift = Grego::gregorianShift(eyear);
#if defined (U_DEBUG_CAL)
        fprintf(stderr, "%s:%d:  gregorian shift %d :::  doy%d => %d [cut=%d]\n",
            __FILE__, __LINE__,gregShift, dayOfYear, dayOfYear+gregShift, fCutoverJulianDay);
#endif
        dayOfYear += gregShift;
    }

    internalSet(UCAL_MONTH, month);
    internalSet(UCAL_ORDINAL_MONTH, month);
    internalSet(UCAL_DAY_OF_MONTH, dayOfMonth);
    internalSet(UCAL_DAY_OF_YEAR, dayOfYear);
    internalSet(UCAL_EXTENDED_YEAR, eyear);
    int32_t era = AD;
    if (eyear < 1) {
        era = BC;
        eyear = 1 - eyear;
    }
    internalSet(UCAL_ERA, era);
    internalSet(UCAL_YEAR, eyear);
}


// -------------------------------------

UDate
GregorianCalendar::getGregorianChange() const
{
    return fGregorianCutover;
}

// -------------------------------------

UBool 
GregorianCalendar::isLeapYear(int32_t year) const
{
    // MSVC complains bitterly if we try to use Grego::isLeapYear here
    // NOTE: year&0x3 == year%4
    return (year >= fGregorianCutoverYear ?
        (((year&0x3) == 0) && ((year%100 != 0) || (year%400 == 0))) : // Gregorian
    ((year&0x3) == 0)); // Julian
}

// -------------------------------------

int32_t GregorianCalendar::handleComputeJulianDay(UCalendarDateFields bestField, UErrorCode& status)
{
    fInvertGregorian = false;

    int32_t jd = Calendar::handleComputeJulianDay(bestField, status);
    if (U_FAILURE(status)) {
        return 0;
    }

    if((bestField == UCAL_WEEK_OF_YEAR) &&  // if we are doing WOY calculations, we are counting relative to Jan 1 *julian*
        (internalGet(UCAL_EXTENDED_YEAR)==fGregorianCutoverYear) && 
        jd >= fCutoverJulianDay) { 
            fInvertGregorian = true;  // So that the Julian Jan 1 will be used in handleComputeMonthStart
            return Calendar::handleComputeJulianDay(bestField, status);
        }


        // The following check handles portions of the cutover year BEFORE the
        // cutover itself happens.
        //if ((fIsGregorian==true) != (jd >= fCutoverJulianDay)) {  /*  cutoverJulianDay)) { */
        if ((fIsGregorian) != (jd >= fCutoverJulianDay)) {  /*  cutoverJulianDay)) { */
#if defined (U_DEBUG_CAL)
            fprintf(stderr, "%s:%d: jd [invert] %d\n", 
                __FILE__, __LINE__, jd);
#endif
            fInvertGregorian = true;
            jd = Calendar::handleComputeJulianDay(bestField, status);
            if (U_FAILURE(status)) {
                return 0;
            }
#if defined (U_DEBUG_CAL)
            fprintf(stderr, "%s:%d:  fIsGregorian %s, fInvertGregorian %s - ", 
                __FILE__, __LINE__,fIsGregorian?"T":"F", fInvertGregorian?"T":"F");
            fprintf(stderr, " jd NOW %d\n", 
                jd);
#endif
        } else {
#if defined (U_DEBUG_CAL)
            fprintf(stderr, "%s:%d: jd [==] %d - %sfIsGregorian %sfInvertGregorian, %d\n", 
                __FILE__, __LINE__, jd, fIsGregorian?"T":"F", fInvertGregorian?"T":"F", bestField);
#endif
        }

        if(fIsGregorian && (internalGet(UCAL_EXTENDED_YEAR) == fGregorianCutoverYear)) {
            int32_t gregShift = Grego::gregorianShift(internalGet(UCAL_EXTENDED_YEAR));
            if (bestField == UCAL_DAY_OF_YEAR) {
#if defined (U_DEBUG_CAL)
                fprintf(stderr, "%s:%d: [DOY%d] gregorian shift of JD %d += %d\n", 
                    __FILE__, __LINE__, fFields[bestField],jd, gregShift);
#endif
                jd -= gregShift;
            } else if ( bestField == UCAL_WEEK_OF_MONTH ) {
                int32_t weekShift = 14;
#if defined (U_DEBUG_CAL)
                fprintf(stderr, "%s:%d: [WOY/WOM] gregorian week shift of %d += %d\n", 
                    __FILE__, __LINE__, jd, weekShift);
#endif
                jd += weekShift; // shift by weeks for week based fields.
            }
        }

        return jd;
}

int64_t GregorianCalendar::handleComputeMonthStart(int32_t eyear, int32_t month,

                                                   UBool /* useMonth */, UErrorCode& status) const
{
    if (U_FAILURE(status)) {
        return 0;
    }
    GregorianCalendar* nonConstThis = const_cast<GregorianCalendar*>(this); // cast away const

    // If the month is out of range, adjust it into range, and
    // modify the extended year value accordingly.
    if (month < 0 || month > 11) {
        if (uprv_add32_overflow(ClockMath::floorDivide(month, 12, &month),
                                eyear, &eyear)) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return 0;
        }
    }

    UBool isLeap = eyear%4 == 0;
    int64_t y = static_cast<int64_t>(eyear) - 1;
    int64_t julianDay = 365LL * y +
        ClockMath::floorDivideInt64(y, 4LL) + kJan1_1JulianDay - 3LL;

    nonConstThis->fIsGregorian = (eyear >= fGregorianCutoverYear);
#if defined (U_DEBUG_CAL)
    fprintf(stderr, "%s:%d: (hcms%d/%d) fIsGregorian %s, fInvertGregorian %s\n", 
        __FILE__, __LINE__, eyear,month, fIsGregorian?"T":"F", fInvertGregorian?"T":"F");
#endif
    if (fInvertGregorian) {
        nonConstThis->fIsGregorian = !fIsGregorian;
    }
    if (fIsGregorian) {
        isLeap = isLeap && ((eyear%100 != 0) || (eyear%400 == 0));
        // Add 2 because Gregorian calendar starts 2 days after
        // Julian calendar
        int32_t gregShift = Grego::gregorianShift(eyear);
#if defined (U_DEBUG_CAL)
        fprintf(stderr, "%s:%d: (hcms%d/%d) gregorian shift of %d += %d\n", 
            __FILE__, __LINE__, eyear, month, julianDay, gregShift);
#endif
        julianDay += gregShift;
    }

    // At this point julianDay indicates the day BEFORE the first
    // day of January 1, <eyear> of either the Julian or Gregorian
    // calendar.

    if (month != 0) {
        julianDay += isLeap?kLeapNumDays[month]:kNumDays[month];
    }

    return julianDay;
}

int32_t GregorianCalendar::handleGetMonthLength(int32_t extendedYear, int32_t month, UErrorCode& /* status */)  const
{
    // If the month is out of range, adjust it into range, and
    // modify the extended year value accordingly.
    if (month < 0 || month > 11) {
        extendedYear += ClockMath::floorDivide(month, 12, &month);
    }

    return isLeapYear(extendedYear) ? kLeapMonthLength[month] : kMonthLength[month];
}

int32_t GregorianCalendar::handleGetYearLength(int32_t eyear) const {
    return isLeapYear(eyear) ? 366 : 365;
}


int32_t
GregorianCalendar::monthLength(int32_t month, UErrorCode& status) const
{
    int32_t year = internalGet(UCAL_EXTENDED_YEAR);
    return handleGetMonthLength(year, month, status);
}

// -------------------------------------

int32_t
GregorianCalendar::monthLength(int32_t month, int32_t year) const
{
    return isLeapYear(year) ? kLeapMonthLength[month] : kMonthLength[month];
}

// -------------------------------------

int32_t
GregorianCalendar::yearLength() const
{
    return isLeapYear(internalGet(UCAL_YEAR)) ? 366 : 365;
}

// -------------------------------------

UBool
GregorianCalendar::validateFields() const
{
    for (int32_t field = 0; field < UCAL_FIELD_COUNT; field++) {
        // Ignore DATE and DAY_OF_YEAR which are handled below
        if (field != UCAL_DATE &&
            field != UCAL_DAY_OF_YEAR &&
            isSet(static_cast<UCalendarDateFields>(field)) &&
            !boundsCheck(internalGet(static_cast<UCalendarDateFields>(field)), static_cast<UCalendarDateFields>(field)))
            return false;
    }

    // Values differ in Least-Maximum and Maximum should be handled
    // specially.
    if (isSet(UCAL_DATE)) {
        int32_t date = internalGet(UCAL_DATE);
        UErrorCode internalStatus = U_ZERO_ERROR;
        if (date < getMinimum(UCAL_DATE) ||
            date > monthLength(internalGetMonth(internalStatus), internalStatus) ||
            U_FAILURE(internalStatus)) {
                return false;
        }
    }

    if (isSet(UCAL_DAY_OF_YEAR)) {
        int32_t days = internalGet(UCAL_DAY_OF_YEAR);
        if (days < 1 || days > yearLength()) {
            return false;
        }
    }

    // Handle DAY_OF_WEEK_IN_MONTH, which must not have the value zero.
    // We've checked against minimum and maximum above already.
    if (isSet(UCAL_DAY_OF_WEEK_IN_MONTH) &&
        0 == internalGet(UCAL_DAY_OF_WEEK_IN_MONTH)) {
            return false;
        }

        return true;
}

// -------------------------------------

UBool
GregorianCalendar::boundsCheck(int32_t value, UCalendarDateFields field) const
{
    return value >= getMinimum(field) && value <= getMaximum(field);
}

// -------------------------------------

UDate 
GregorianCalendar::getEpochDay(UErrorCode& status) 
{
    complete(status);
    // Divide by 1000 (convert to seconds) in order to prevent overflow when
    // dealing with UDate(Long.MIN_VALUE) and UDate(Long.MAX_VALUE).
    double wallSec = internalGetTime()/1000 + (internalGet(UCAL_ZONE_OFFSET) + internalGet(UCAL_DST_OFFSET))/1000;

    return ClockMath::floorDivide(wallSec, kOneDay/1000.0);
}

// -------------------------------------


// -------------------------------------

/**
* Compute the julian day number of the day BEFORE the first day of
* January 1, year 1 of the given calendar.  If julianDay == 0, it
* specifies (Jan. 1, 1) - 1, in whatever calendar we are using (Julian
* or Gregorian).
*/
double GregorianCalendar::computeJulianDayOfYear(UBool isGregorian,
                                                 int32_t year, UBool& isLeap)
{
    isLeap = year%4 == 0;
    int32_t y = year - 1;
    double julianDay = 365.0*y + ClockMath::floorDivide(y, 4) + (kJan1_1JulianDay - 3);

    if (isGregorian) {
        isLeap = isLeap && ((year%100 != 0) || (year%400 == 0));
        // Add 2 because Gregorian calendar starts 2 days after Julian calendar
        julianDay += Grego::gregorianShift(year);
    }

    return julianDay;
}

// /**
//  * Compute the day of week, relative to the first day of week, from
//  * 0..6, of the current DOW_LOCAL or DAY_OF_WEEK fields.  This is
//  * equivalent to get(DOW_LOCAL) - 1.
//  */
// int32_t GregorianCalendar::computeRelativeDOW() const {
//     int32_t relDow = 0;
//     if (fStamp[UCAL_DOW_LOCAL] > fStamp[UCAL_DAY_OF_WEEK]) {
//         relDow = internalGet(UCAL_DOW_LOCAL) - 1; // 1-based
//     } else if (fStamp[UCAL_DAY_OF_WEEK] != kUnset) {
//         relDow = internalGet(UCAL_DAY_OF_WEEK) - getFirstDayOfWeek();
//         if (relDow < 0) relDow += 7;
//     }
//     return relDow;
// }

// /**
//  * Compute the day of week, relative to the first day of week,
//  * from 0..6 of the given julian day.
//  */
// int32_t GregorianCalendar::computeRelativeDOW(double julianDay) const {
//   int32_t relDow = julianDayToDayOfWeek(julianDay) - getFirstDayOfWeek();
//     if (relDow < 0) {
//         relDow += 7;
//     }
//     return relDow;
// }

// /**
//  * Compute the DOY using the WEEK_OF_YEAR field and the julian day
//  * of the day BEFORE January 1 of a year (a return value from
//  * computeJulianDayOfYear).
//  */
// int32_t GregorianCalendar::computeDOYfromWOY(double julianDayOfYear) const {
//     // Compute DOY from day of week plus week of year

//     // Find the day of the week for the first of this year.  This
//     // is zero-based, with 0 being the locale-specific first day of
//     // the week.  Add 1 to get first day of year.
//     int32_t fdy = computeRelativeDOW(julianDayOfYear + 1);

//     return
//         // Compute doy of first (relative) DOW of WOY 1
//         (((7 - fdy) < getMinimalDaysInFirstWeek())
//          ? (8 - fdy) : (1 - fdy))

//         // Adjust for the week number.
//         + (7 * (internalGet(UCAL_WEEK_OF_YEAR) - 1))

//         // Adjust for the DOW
//         + computeRelativeDOW();
// }

// -------------------------------------

double
GregorianCalendar::millisToJulianDay(UDate millis)
{
    return static_cast<double>(kEpochStartAsJulianDay) + ClockMath::floorDivide(millis, kOneDay);
}

// -------------------------------------

UDate
GregorianCalendar::julianDayToMillis(double julian)
{
    return static_cast<UDate>((julian - kEpochStartAsJulianDay) * kOneDay);
}

// -------------------------------------

int32_t
GregorianCalendar::aggregateStamp(int32_t stamp_a, int32_t stamp_b)
{
    return (((stamp_a != kUnset && stamp_b != kUnset)
        ? uprv_max(stamp_a, stamp_b)
        : static_cast<int32_t>(kUnset)));
}

// -------------------------------------

/**
* Roll a field by a signed amount.
* Note: This will be made public later. [LIU]
*/

void
GregorianCalendar::roll(EDateFields field, int32_t amount, UErrorCode& status) {
    roll(static_cast<UCalendarDateFields>(field), amount, status);
}

void
GregorianCalendar::roll(UCalendarDateFields field, int32_t amount, UErrorCode& status) UPRV_NO_SANITIZE_UNDEFINED {
    if((amount == 0) || U_FAILURE(status)) {
        return;
    }

    // J81 processing. (gregorian cutover)
    UBool inCutoverMonth = false;
    int32_t cMonthLen=0; // 'c' for cutover; in days
    int32_t cDayOfMonth=0; // no discontinuity: [0, cMonthLen)
    double cMonthStart=0.0; // in ms

    // Common code - see if we're in the cutover month of the cutover year
    if(get(UCAL_EXTENDED_YEAR, status) == fGregorianCutoverYear) {
        switch (field) {
        case UCAL_DAY_OF_MONTH:
        case UCAL_WEEK_OF_MONTH:
            {
                int32_t max = monthLength(internalGetMonth(status), status);
                if (U_FAILURE(status)) {
                    return;
                }
                UDate t = internalGetTime();
                // We subtract 1 from the DAY_OF_MONTH to make it zero-based, and an
                // additional 10 if we are after the cutover. Thus the monthStart
                // value will be correct iff we actually are in the cutover month.
                cDayOfMonth = internalGet(UCAL_DAY_OF_MONTH) - ((t >= fGregorianCutover) ? 10 : 0);
                cMonthStart = t - ((cDayOfMonth - 1) * kOneDay);
                // A month containing the cutover is 10 days shorter.
                if ((cMonthStart < fGregorianCutover) &&
                    (cMonthStart + (cMonthLen=(max-10))*kOneDay >= fGregorianCutover)) {
                        inCutoverMonth = true;
                }
            }
            break;
        default:
            ;
        }
    }

    switch (field) {
    case UCAL_WEEK_OF_YEAR: {
        // Unlike WEEK_OF_MONTH, WEEK_OF_YEAR never shifts the day of the
        // week.  Also, rolling the week of the year can have seemingly
        // strange effects simply because the year of the week of year
        // may be different from the calendar year.  For example, the
        // date Dec 28, 1997 is the first day of week 1 of 1998 (if
        // weeks start on Sunday and the minimal days in first week is
        // <= 3).
        int32_t woy = get(UCAL_WEEK_OF_YEAR, status);
        // Get the ISO year, which matches the week of year.  This
        // may be one year before or after the calendar year.
        int32_t isoYear = get(UCAL_YEAR_WOY, status);
        int32_t isoDoy = internalGet(UCAL_DAY_OF_YEAR);
        int32_t month = internalGetMonth(status);
        if (U_FAILURE(status)) {
            return;
        }
        if (month == UCAL_JANUARY) {
            if (woy >= 52) {
                isoDoy += handleGetYearLength(isoYear);
            }
        } else {
            if (woy == 1) {
                isoDoy -= handleGetYearLength(isoYear - 1);
            }
        }
        if (uprv_add32_overflow(woy, amount, &woy)) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        // Do fast checks to avoid unnecessary computation:
        if (woy < 1 || woy > 52) {
            // Determine the last week of the ISO year.
            // We do this using the standard formula we use
            // everywhere in this file.  If we can see that the
            // days at the end of the year are going to fall into
            // week 1 of the next year, we drop the last week by
            // subtracting 7 from the last day of the year.
            int32_t lastDoy = handleGetYearLength(isoYear);
            int32_t lastRelDow = (lastDoy - isoDoy + internalGet(UCAL_DAY_OF_WEEK) -
                getFirstDayOfWeek()) % 7;
            if (lastRelDow < 0) lastRelDow += 7;
            if ((6 - lastRelDow) >= getMinimalDaysInFirstWeek()) lastDoy -= 7;
            int32_t lastWoy = weekNumber(lastDoy, lastRelDow + 1);
            woy = ((woy + lastWoy - 1) % lastWoy) + 1;
        }
        set(UCAL_WEEK_OF_YEAR, woy);
        set(UCAL_YEAR_WOY,isoYear);
        return;
                            }

    case UCAL_DAY_OF_MONTH:
        if( !inCutoverMonth ) { 
            Calendar::roll(field, amount, status);
            return;
        }
        {
            // [j81] 1582 special case for DOM
            // The default computation works except when the current month
            // contains the Gregorian cutover.  We handle this special case
            // here.  [j81 - aliu]
            double monthLen = cMonthLen * kOneDay;
            double msIntoMonth = uprv_fmod(internalGetTime() - cMonthStart +
                amount * kOneDay, monthLen);
            if (msIntoMonth < 0) {
                msIntoMonth += monthLen;
            }
#if defined (U_DEBUG_CAL)
            fprintf(stderr, "%s:%d: roll DOM %d  -> %.0lf ms  \n", 
                __FILE__, __LINE__,amount, cMonthLen, cMonthStart+msIntoMonth);
#endif
            setTimeInMillis(cMonthStart + msIntoMonth, status);
            return;
        }

    case UCAL_WEEK_OF_MONTH:
        if( !inCutoverMonth ) { 
            Calendar::roll(field, amount, status);
            return;
        }
        {
#if defined (U_DEBUG_CAL)
            fprintf(stderr, "%s:%d: roll WOM %d ??????????????????? \n", 
                __FILE__, __LINE__,amount);
#endif
            // NOTE: following copied from  the old
            //     GregorianCalendar::roll( WEEK_OF_MONTH )  code 

            // This is tricky, because during the roll we may have to shift
            // to a different day of the week.  For example:

            //    s  m  t  w  r  f  s
            //          1  2  3  4  5
            //    6  7  8  9 10 11 12

            // When rolling from the 6th or 7th back one week, we go to the
            // 1st (assuming that the first partial week counts).  The same
            // thing happens at the end of the month.

            // The other tricky thing is that we have to figure out whether
            // the first partial week actually counts or not, based on the
            // minimal first days in the week.  And we have to use the
            // correct first day of the week to delineate the week
            // boundaries.

            // Here's our algorithm.  First, we find the real boundaries of
            // the month.  Then we discard the first partial week if it
            // doesn't count in this locale.  Then we fill in the ends with
            // phantom days, so that the first partial week and the last
            // partial week are full weeks.  We then have a nice square
            // block of weeks.  We do the usual rolling within this block,
            // as is done elsewhere in this method.  If we wind up on one of
            // the phantom days that we added, we recognize this and pin to
            // the first or the last day of the month.  Easy, eh?

            // Another wrinkle: To fix jitterbug 81, we have to make all this
            // work in the oddball month containing the Gregorian cutover.
            // This month is 10 days shorter than usual, and also contains
            // a discontinuity in the days; e.g., the default cutover month
            // is Oct 1582, and goes from day of month 4 to day of month 15.

            // Normalize the DAY_OF_WEEK so that 0 is the first day of the week
            // in this locale.  We have dow in 0..6.
            int32_t dow = internalGet(UCAL_DAY_OF_WEEK) - getFirstDayOfWeek();
            if (dow < 0) 
                dow += 7;

            // Find the day of month, compensating for cutover discontinuity.
            int32_t dom = cDayOfMonth;

            // Find the day of the week (normalized for locale) for the first
            // of the month.
            int32_t fdm = (dow - dom + 1) % 7;
            if (fdm < 0) 
                fdm += 7;

            // Get the first day of the first full week of the month,
            // including phantom days, if any.  Figure out if the first week
            // counts or not; if it counts, then fill in phantom days.  If
            // not, advance to the first real full week (skip the partial week).
            int32_t start;
            if ((7 - fdm) < getMinimalDaysInFirstWeek())
                start = 8 - fdm; // Skip the first partial week
            else
                start = 1 - fdm; // This may be zero or negative

            // Get the day of the week (normalized for locale) for the last
            // day of the month.
            int32_t monthLen = cMonthLen;
            int32_t ldm = (monthLen - dom + dow) % 7;
            // We know monthLen >= DAY_OF_MONTH so we skip the += 7 step here.

            // Get the limit day for the blocked-off rectangular month; that
            // is, the day which is one past the last day of the month,
            // after the month has already been filled in with phantom days
            // to fill out the last week.  This day has a normalized DOW of 0.
            int32_t limit = monthLen + 7 - ldm;

            // Now roll between start and (limit - 1).
            int32_t gap = limit - start;
            int32_t newDom = (dom + amount*7 - start) % gap;
            if (newDom < 0) 
                newDom += gap;
            newDom += start;

            // Finally, pin to the real start and end of the month.
            if (newDom < 1) 
                newDom = 1;
            if (newDom > monthLen) 
                newDom = monthLen;

            // Set the DAY_OF_MONTH.  We rely on the fact that this field
            // takes precedence over everything else (since all other fields
            // are also set at this point).  If this fact changes (if the
            // disambiguation algorithm changes) then we will have to unset
            // the appropriate fields here so that DAY_OF_MONTH is attended
            // to.

            // If we are in the cutover month, manipulate ms directly.  Don't do
            // this in general because it doesn't work across DST boundaries
            // (details, details).  This takes care of the discontinuity.
            setTimeInMillis(cMonthStart + (newDom-1)*kOneDay, status);                
            return;
        }

    default:
        Calendar::roll(field, amount, status);
        return;
    }
}

// -------------------------------------


/**
* Return the minimum value that this field could have, given the current date.
* For the Gregorian calendar, this is the same as getMinimum() and getGreatestMinimum().
* @param field    the time field.
* @return         the minimum value that this field could have, given the current date.
* @deprecated ICU 2.6. Use getActualMinimum(UCalendarDateFields field) instead.
*/
int32_t GregorianCalendar::getActualMinimum(EDateFields field) const
{
    return getMinimum(static_cast<UCalendarDateFields>(field));
}

int32_t GregorianCalendar::getActualMinimum(EDateFields field, UErrorCode& /* status */) const
{
    return getMinimum(static_cast<UCalendarDateFields>(field));
}

/**
* Return the minimum value that this field could have, given the current date.
* For the Gregorian calendar, this is the same as getMinimum() and getGreatestMinimum().
* @param field    the time field.
* @return         the minimum value that this field could have, given the current date.
* @draft ICU 2.6.
*/
int32_t GregorianCalendar::getActualMinimum(UCalendarDateFields field, UErrorCode& /* status */) const
{
    return getMinimum(field);
}


// ------------------------------------

/**
* Old year limits were least max 292269054, max 292278994.
*/

/**
* @stable ICU 2.0
*/
int32_t GregorianCalendar::handleGetLimit(UCalendarDateFields field, ELimitType limitType) const {
    return kGregorianCalendarLimits[field][limitType];
}

/**
* Return the maximum value that this field could have, given the current date.
* For example, with the date "Feb 3, 1997" and the DAY_OF_MONTH field, the actual
* maximum would be 28; for "Feb 3, 1996" it s 29.  Similarly for a Hebrew calendar,
* for some years the actual maximum for MONTH is 12, and for others 13.
* @stable ICU 2.0
*/
int32_t GregorianCalendar::getActualMaximum(UCalendarDateFields field, UErrorCode& status) const
{
    /* It is a known limitation that the code here (and in getActualMinimum)
    * won't behave properly at the extreme limits of GregorianCalendar's
    * representable range (except for the code that handles the YEAR
    * field).  That's because the ends of the representable range are at
    * odd spots in the year.  For calendars with the default Gregorian
    * cutover, these limits are Sun Dec 02 16:47:04 GMT 292269055 BC to Sun
    * Aug 17 07:12:55 GMT 292278994 AD, somewhat different for non-GMT
    * zones.  As a result, if the calendar is set to Aug 1 292278994 AD,
    * the actual maximum of DAY_OF_MONTH is 17, not 30.  If the date is Mar
    * 31 in that year, the actual maximum month might be Jul, whereas is
    * the date is Mar 15, the actual maximum might be Aug -- depending on
    * the precise semantics that are desired.  Similar considerations
    * affect all fields.  Nonetheless, this effect is sufficiently arcane
    * that we permit it, rather than complicating the code to handle such
    * intricacies. - liu 8/20/98

    * UPDATE: No longer true, since we have pulled in the limit values on
    * the year. - Liu 11/6/00 */

    switch (field) {

    case UCAL_YEAR:
        /* The year computation is no different, in principle, from the
        * others, however, the range of possible maxima is large.  In
        * addition, the way we know we've exceeded the range is different.
        * For these reasons, we use the special case code below to handle
        * this field.
        *
        * The actual maxima for YEAR depend on the type of calendar:
        *
        *     Gregorian = May 17, 292275056 BC - Aug 17, 292278994 AD
        *     Julian    = Dec  2, 292269055 BC - Jan  3, 292272993 AD
        *     Hybrid    = Dec  2, 292269055 BC - Aug 17, 292278994 AD
        *
        * We know we've exceeded the maximum when either the month, date,
        * time, or era changes in response to setting the year.  We don't
        * check for month, date, and time here because the year and era are
        * sufficient to detect an invalid year setting.  NOTE: If code is
        * added to check the month and date in the future for some reason,
        * Feb 29 must be allowed to shift to Mar 1 when setting the year.
        */
        {
            if(U_FAILURE(status)) return 0;
            Calendar *cal = clone();
            if(!cal) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return 0;
            }

            cal->setLenient(true);

            int32_t era = cal->get(UCAL_ERA, status);
            UDate d = cal->getTime(status);

            /* Perform a binary search, with the invariant that lowGood is a
            * valid year, and highBad is an out of range year.
            */
            int32_t lowGood = kGregorianCalendarLimits[UCAL_YEAR][1];
            int32_t highBad = kGregorianCalendarLimits[UCAL_YEAR][2]+1;
            while ((lowGood + 1) < highBad) {
                int32_t y = (lowGood + highBad) / 2;
                cal->set(UCAL_YEAR, y);
                if (cal->get(UCAL_YEAR, status) == y && cal->get(UCAL_ERA, status) == era) {
                    lowGood = y;
                } else {
                    highBad = y;
                    cal->setTime(d, status); // Restore original fields
                }
            }

            delete cal;
            return lowGood;
        }

    default:
        return Calendar::getActualMaximum(field,status);
    }
}


int32_t GregorianCalendar::handleGetExtendedYear(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return 0;
    }
    // the year to return
    int32_t year = kEpochYear;

    // year field to use
    int32_t yearField = UCAL_EXTENDED_YEAR;

    // There are three separate fields which could be used to
    // derive the proper year.  Use the one most recently set.
    if (fStamp[yearField] < fStamp[UCAL_YEAR])
        yearField = UCAL_YEAR;
    if (fStamp[yearField] < fStamp[UCAL_YEAR_WOY])
        yearField = UCAL_YEAR_WOY;

    // based on the "best" year field, get the year
    switch(yearField) {
    case UCAL_EXTENDED_YEAR:
        year = internalGet(UCAL_EXTENDED_YEAR, kEpochYear);
        break;

    case UCAL_YEAR:
        {
            // The year defaults to the epoch start, the era to AD
            int32_t era = internalGet(UCAL_ERA, AD);
            if (era == BC) {
                year = 1 - internalGet(UCAL_YEAR, 1); // Convert to extended year
            } else if (era == AD) {
                year = internalGet(UCAL_YEAR, kEpochYear);
            } else {
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return 0;
            }
        }
        break;

    case UCAL_YEAR_WOY:
        year = handleGetExtendedYearFromWeekFields(
            internalGet(UCAL_YEAR_WOY), internalGet(UCAL_WEEK_OF_YEAR), status);
        if (U_FAILURE(status)) {
            return 0;
        }
#if defined (U_DEBUG_CAL)
        //    if(internalGet(UCAL_YEAR_WOY) != year) {
        fprintf(stderr, "%s:%d: hGEYFWF[%d,%d] ->  %d\n", 
            __FILE__, __LINE__,internalGet(UCAL_YEAR_WOY),internalGet(UCAL_WEEK_OF_YEAR),year);
        //}
#endif
        break;

    default:
        year = kEpochYear;
    }
    return year;
}

int32_t GregorianCalendar::handleGetExtendedYearFromWeekFields(int32_t yearWoy, int32_t woy, UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return 0;
    }
    // convert year to extended form
    int32_t era = internalGet(UCAL_ERA, AD);
    if(era == BC) {
        yearWoy = 1 - yearWoy;
    }
    return Calendar::handleGetExtendedYearFromWeekFields(yearWoy, woy, status);
}


// -------------------------------------

/**
* Return the ERA.  We need a special method for this because the
* default ERA is AD, but a zero (unset) ERA is BC.
*/
int32_t
GregorianCalendar::internalGetEra() const {
    return isSet(UCAL_ERA) ? internalGet(UCAL_ERA) : static_cast<int32_t>(AD);
}

const char *
GregorianCalendar::getType() const {
    //static const char kGregorianType = "gregorian";

    return "gregorian";
}

IMPL_SYSTEM_DEFAULT_CENTURY(GregorianCalendar, "@calendar=gregory")

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
