// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 * Copyright (C) 2003-2014, International Business Machines Corporation
 * and others. All Rights Reserved.
 ******************************************************************************
 *
 * File INDIANCAL.CPP
 *****************************************************************************
 */

#include "indiancal.h"
#include <stdlib.h>
#if !UCONFIG_NO_FORMATTING

#include "mutex.h"
#include <float.h>
#include "gregoimp.h" // Math
#include "uhash.h"

// Debugging
#ifdef U_DEBUG_INDIANCAL
#include <stdio.h>
#include <stdarg.h>

#endif

U_NAMESPACE_BEGIN

// Implementation of the IndianCalendar class

//-------------------------------------------------------------------------
// Constructors...
//-------------------------------------------------------------------------


IndianCalendar* IndianCalendar::clone() const {
  return new IndianCalendar(*this);
}

IndianCalendar::IndianCalendar(const Locale& aLocale, UErrorCode& success)
  :   Calendar(TimeZone::forLocaleOrDefault(aLocale), aLocale, success)
{
  setTimeInMillis(getNow(), success); // Call this again now that the vtable is set up properly.
}

IndianCalendar::IndianCalendar(const IndianCalendar& other) : Calendar(other) {
}

IndianCalendar::~IndianCalendar()
{
}
const char *IndianCalendar::getType() const { 
   return "indian";
}
  
static const int32_t LIMITS[UCAL_FIELD_COUNT][4] = {
    // Minimum  Greatest     Least   Maximum
    //           Minimum   Maximum
    {        0,        0,        0,        0}, // ERA
    { -5000000, -5000000,  5000000,  5000000}, // YEAR
    {        0,        0,       11,       11}, // MONTH
    {        1,        1,       52,       53}, // WEEK_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // WEEK_OF_MONTH
    {        1,        1,       30,       31}, // DAY_OF_MONTH
    {        1,        1,      365,      366}, // DAY_OF_YEAR
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
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // IS_LEAP_MONTH
    {        0,        0,       11,       11}, // ORDINAL_MONTH
};

static const int32_t INDIAN_ERA_START  = 78;
static const int32_t INDIAN_YEAR_START = 80;

int32_t IndianCalendar::handleGetLimit(UCalendarDateFields field, ELimitType limitType) const {
  return LIMITS[field][limitType];
}

/*
 * Determine whether the given gregorian year is a Leap year 
 */
static UBool isGregorianLeap(int32_t year)
{
    return Grego::isLeapYear(year);
}
  
//----------------------------------------------------------------------
// Calendar framework
//----------------------------------------------------------------------

/*
 * Return the length (in days) of the given month.
 *
 * @param eyear  The year in Saka Era
 * @param month  The month(0-based) in Indian calendar
 */
int32_t IndianCalendar::handleGetMonthLength(int32_t eyear, int32_t month, UErrorCode& /* status */) const {
   if (month < 0 || month > 11) {
      eyear += ClockMath::floorDivide(month, 12, &month);
   }

   if (isGregorianLeap(eyear + INDIAN_ERA_START) && month == 0) {
       return 31;
   }

   if (month >= 1 && month <= 5) {
       return 31;
   }

   return 30;
}

/*
 * Return the number of days in the given Indian year
 *
 * @param eyear The year in Saka Era.
 */
int32_t IndianCalendar::handleGetYearLength(int32_t eyear) const {
    return isGregorianLeap(eyear + INDIAN_ERA_START) ? 366 : 365;
}
/*
 * Returns the Julian Day corresponding to gregorian date
 *
 * @param year The Gregorian year
 * @param month The month in Gregorian Year, 0 based.
 * @param date The date in Gregorian day in month
 */
static double gregorianToJD(int32_t year, int32_t month, int32_t date) {
   return Grego::fieldsToDay(year, month, date) + kEpochStartAsJulianDay - 0.5;
}

/*
 * Returns the Gregorian Date corresponding to a given Julian Day
 * Month is 0 based.
 * @param jd The Julian Day
 */
static int32_t* jdToGregorian(double jd, int32_t gregorianDate[3], UErrorCode& status) {
   int32_t gdow;
   Grego::dayToFields(jd - kEpochStartAsJulianDay,
                      gregorianDate[0], gregorianDate[1], gregorianDate[2], gdow, status);
   return gregorianDate;
}

   
//-------------------------------------------------------------------------
// Functions for converting from field values to milliseconds....
//-------------------------------------------------------------------------
static double IndianToJD(int32_t year, int32_t month, int32_t date) {
   int32_t leapMonth, gyear, m;
   double start, jd;

   gyear = year + INDIAN_ERA_START;


   if(isGregorianLeap(gyear)) {
      leapMonth = 31;
      start = gregorianToJD(gyear, 2 /* The third month in 0 based month */, 21);
   } 
   else {
      leapMonth = 30;
      start = gregorianToJD(gyear, 2 /* The third month in 0 based month */, 22);
   }

   if (month == 1) {
      jd = start + (date - 1);
   } else {
      jd = start + leapMonth;
      m = month - 2;

      //m = Math.min(m, 5);
      if (m > 5) {
          m = 5;
      }

      jd += m * 31;

      if (month >= 8) {
         m = month - 7;
         jd += m * 30;
      }
      jd += date - 1;
   }

   return jd;
}

/*
 * Return JD of start of given month/year of Indian Calendar
 * @param eyear The year in Indian Calendar measured from Saka Era (78 AD).
 * @param month The month in Indian calendar
 */
int64_t IndianCalendar::handleComputeMonthStart(int32_t eyear, int32_t month, UBool /* useMonth */, UErrorCode& status) const {
   if (U_FAILURE(status)) {
       return 0;
   }

   //month is 0 based; converting it to 1-based 
   int32_t imonth;

    // If the month is out of range, adjust it into range, and adjust the extended year accordingly
   if (month < 0 || month > 11) {
      if (uprv_add32_overflow(eyear, ClockMath::floorDivide(month, 12, &month), &eyear)) {
          status = U_ILLEGAL_ARGUMENT_ERROR;
          return 0;
      }
   }

   if(month == 12){
       imonth = 1;
   } else {
       imonth = month + 1; 
   }
   
   int64_t jd = IndianToJD(eyear ,imonth, 1);

   return jd;
}

//-------------------------------------------------------------------------
// Functions for converting from milliseconds to field values
//-------------------------------------------------------------------------

int32_t IndianCalendar::handleGetExtendedYear(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return 0;
    }
    int32_t year;

    if (newerField(UCAL_EXTENDED_YEAR, UCAL_YEAR) == UCAL_EXTENDED_YEAR) {
        year = internalGet(UCAL_EXTENDED_YEAR, 1); // Default to year 1
    } else {
        year = internalGet(UCAL_YEAR, 1); // Default to year 1
    }

    return year;
}

/*
 * Override Calendar to compute several fields specific to the Indian
 * calendar system.  These are:
 *
 * <ul><li>ERA
 * <li>YEAR
 * <li>MONTH
 * <li>DAY_OF_MONTH
 * <li>EXTENDED_YEAR</ul>
 * 
 * The DAY_OF_WEEK and DOW_LOCAL fields are already set when this
 * method is called. The getGregorianXxx() methods return Gregorian
 * calendar equivalents for the given Julian day.
 */
void IndianCalendar::handleComputeFields(int32_t julianDay, UErrorCode&  status) {
    double jdAtStartOfGregYear;
    int32_t leapMonth, IndianYear, yday, IndianMonth, IndianDayOfMonth, mday;
    int32_t gregorianYear;      // Stores gregorian date corresponding to Julian day;
    int32_t gd[3];

    gregorianYear = jdToGregorian(julianDay, gd, status)[0];          // Gregorian date for Julian day
    if (U_FAILURE(status)) return;
    IndianYear = gregorianYear - INDIAN_ERA_START;            // Year in Saka era
    jdAtStartOfGregYear = gregorianToJD(gregorianYear, 0, 1); // JD at start of Gregorian year
    yday = static_cast<int32_t>(julianDay - jdAtStartOfGregYear); // Day number in Gregorian year (starting from 0)

    if (yday < INDIAN_YEAR_START) {
        // Day is at the end of the preceding Saka year
        IndianYear -= 1;
        leapMonth = isGregorianLeap(gregorianYear - 1) ? 31 : 30; // Days in leapMonth this year, previous Gregorian year
        yday += leapMonth + (31 * 5) + (30 * 3) + 10;
    } else {
        leapMonth = isGregorianLeap(gregorianYear) ? 31 : 30; // Days in leapMonth this year
        yday -= INDIAN_YEAR_START;
    }

    if (yday < leapMonth) {
        IndianMonth = 0;
        IndianDayOfMonth = yday + 1;
    } else {
        mday = yday - leapMonth;
        if (mday < (31 * 5)) {
            IndianMonth = static_cast<int32_t>(uprv_floor(mday / 31)) + 1;
            IndianDayOfMonth = (mday % 31) + 1;
        } else {
            mday -= 31 * 5;
            IndianMonth = static_cast<int32_t>(uprv_floor(mday / 30)) + 6;
            IndianDayOfMonth = (mday % 30) + 1;
        }
   }

   internalSet(UCAL_ERA, 0);
   internalSet(UCAL_EXTENDED_YEAR, IndianYear);
   internalSet(UCAL_YEAR, IndianYear);
   internalSet(UCAL_MONTH, IndianMonth);
   internalSet(UCAL_ORDINAL_MONTH, IndianMonth);
   internalSet(UCAL_DAY_OF_MONTH, IndianDayOfMonth);
   internalSet(UCAL_DAY_OF_YEAR, yday + 1); // yday is 0-based
}    

constexpr uint32_t kIndianRelatedYearDiff = 79;

int32_t IndianCalendar::getRelatedYear(UErrorCode &status) const
{
    int32_t year = get(UCAL_EXTENDED_YEAR, status);
    if (U_FAILURE(status)) {
        return 0;
    }
    if (uprv_add32_overflow(year, kIndianRelatedYearDiff, &year)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    return year;
}

void IndianCalendar::setRelatedYear(int32_t year)
{
    // set extended year
    set(UCAL_EXTENDED_YEAR, year - kIndianRelatedYearDiff);
}

IMPL_SYSTEM_DEFAULT_CENTURY(IndianCalendar, "@calendar=indian")

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(IndianCalendar)

U_NAMESPACE_END

#endif

