// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2003 - 2009, International Business Machines Corporation and  *
* others. All Rights Reserved.                                                *
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "cecal.h"
#include "gregoimp.h"   //Math
#include "cstring.h"

U_NAMESPACE_BEGIN

static const int32_t LIMITS[UCAL_FIELD_COUNT][4] = {
    // Minimum  Greatest    Least  Maximum
    //           Minimum  Maximum
    {        0,        0,        1,        1}, // ERA
    {        1,        1,  5000000,  5000000}, // YEAR
    {        0,        0,       12,       12}, // MONTH
    {        1,        1,       52,       53}, // WEEK_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // WEEK_OF_MONTH
    {        1,        1,        5,       30}, // DAY_OF_MONTH
    {        1,        1,      365,      366}, // DAY_OF_YEAR
    {/*N/A*/-1,/*N/A*/-1,/*N/A*/-1,/*N/A*/-1}, // DAY_OF_WEEK
    {       -1,       -1,        1,        5}, // DAY_OF_WEEK_IN_MONTH
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
    {        0,        0,       12,       12}, // ORDINAL_MONTH
};

//-------------------------------------------------------------------------
// Constructors...
//-------------------------------------------------------------------------

CECalendar::CECalendar(const Locale& aLocale, UErrorCode& success)
:   Calendar(TimeZone::forLocaleOrDefault(aLocale), aLocale, success)
{
    setTimeInMillis(getNow(), success);
}

CECalendar::CECalendar (const CECalendar& other) 
:   Calendar(other)
{
}

CECalendar::~CECalendar()
{
}

CECalendar&
CECalendar::operator=(const CECalendar& right)
{
    Calendar::operator=(right);
    return *this;
}

//-------------------------------------------------------------------------
// Calendar framework
//-------------------------------------------------------------------------

int64_t
CECalendar::handleComputeMonthStart(int32_t eyear,int32_t emonth, UBool /*useMonth*/, UErrorCode& /*status*/) const
{
    int64_t year64 = eyear;
    // handle month > 12, < 0 (e.g. from add/set)
    if ( emonth >= 0 ) {
        year64 += emonth/13;
        emonth %= 13;
    } else {
        ++emonth;
        year64 += emonth/13 - 1;
        emonth = emonth%13 + 12;
    }

    return (
        getJDEpochOffset()                    // difference from Julian epoch to 1,1,1
        + 365LL * year64                      // number of days from years
        + ClockMath::floorDivideInt64(year64, 4LL) // extra day of leap year
        + 30 * emonth                         // number of days from months (months are 0-based)
        - 1                                   // number of days for present month (1 based)
        );
}

int32_t
CECalendar::handleGetLimit(UCalendarDateFields field, ELimitType limitType) const
{
    return LIMITS[field][limitType];
}

//-------------------------------------------------------------------------
// Calendar system Conversion methods...
//-------------------------------------------------------------------------

void
CECalendar::jdToCE(int32_t julianDay, int32_t jdEpochOffset, int32_t& year, int32_t& month, int32_t& day, UErrorCode& status)
{
    int32_t c4; // number of 4 year cycle (1461 days)
    int32_t r4; // remainder of 4 year cycle, always positive

    if (uprv_add32_overflow(julianDay, -jdEpochOffset, &julianDay)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    c4 = ClockMath::floorDivide(julianDay, 1461, &r4);

    year = 4 * c4 + (r4/365 - r4/1460); // 4 * <number of 4year cycle> + <years within the last cycle>

    int32_t doy = (r4 == 1460) ? 365 : (r4 % 365); // days in present year

    month = doy / 30;       // 30 -> Coptic/Ethiopic month length up to 12th month
    day = (doy % 30) + 1;   // 1-based days in a month
}

static const char* kMonthCode13 = "M13";

const char* CECalendar::getTemporalMonthCode(UErrorCode& status) const {
    if (get(UCAL_MONTH, status) == 12) {
        return kMonthCode13;
    }
    return Calendar::getTemporalMonthCode(status);
}

void
CECalendar::setTemporalMonthCode(const char* code, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (uprv_strcmp(code, kMonthCode13) == 0) {
        set(UCAL_MONTH, 12);
        set(UCAL_IS_LEAP_MONTH, 0);
        return;
    }
    Calendar::setTemporalMonthCode(code, status);
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
//eof
