// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 *******************************************************************************
 * Copyright (C) 1997-2013, International Business Machines Corporation and
 * others. All Rights Reserved.
 *******************************************************************************
 *
 * File SIMPLETZ.H
 *
 * Modification History:
 *
 *   Date        Name        Description
 *   12/05/96    clhuang     Creation.
 *   04/21/97    aliu        Fixed miscellaneous bugs found by inspection and
 *                           testing.
 *   07/29/97    aliu        Ported source bodies back from Java version with
 *                           numerous feature enhancements and bug fixes.
 *   08/10/98    stephen     JDK 1.2 sync.
 *   09/17/98    stephen     Fixed getOffset() for last hour of year and DST
 *   12/02/99    aliu        Added TimeMode and constructor and setStart/EndRule
 *                           methods that take TimeMode. Whitespace cleanup.
 ********************************************************************************
 */

#include "utypeinfo.h"  // for 'typeid' to work

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/simpletz.h"
#include "unicode/gregocal.h"
#include "unicode/smpdtfmt.h"

#include "cmemory.h"
#include "gregoimp.h"
#include "umutex.h"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(SimpleTimeZone)

// Use only for decodeStartRule() and decodeEndRule() where the year is not
// available. Set February to 29 days to accommodate rules with that date
// and day-of-week-on-or-before-that-date mode (DOW_LE_DOM_MODE).
// The compareToRule() method adjusts to February 28 in non-leap years.
//
// For actual getOffset() calculations, use Grego::monthLength() and
// Grego::previousMonthLength() which take leap years into account.
// We handle leap years assuming always
// Gregorian, since we know they didn't have daylight time when
// Gregorian calendar started.
const int8_t SimpleTimeZone::STATICMONTHLENGTH[] = {31,29,31,30,31,30,31,31,30,31,30,31};

static const char16_t DST_STR[] = {0x0028,0x0044,0x0053,0x0054,0x0029,0}; // "(DST)"
static const char16_t STD_STR[] = {0x0028,0x0053,0x0054,0x0044,0x0029,0}; // "(STD)"


// *****************************************************************************
// class SimpleTimeZone
// *****************************************************************************


SimpleTimeZone::SimpleTimeZone(int32_t rawOffsetGMT, const UnicodeString& ID)
:   BasicTimeZone(ID),
    startMonth(0),
    startDay(0),
    startDayOfWeek(0),
    startTime(0),
    startTimeMode(WALL_TIME),
    endTimeMode(WALL_TIME),
    endMonth(0),
    endDay(0),
    endDayOfWeek(0),
    endTime(0),
    startYear(0),
    rawOffset(rawOffsetGMT),
    useDaylight(false),
    startMode(DOM_MODE),
    endMode(DOM_MODE),
    dstSavings(U_MILLIS_PER_HOUR)
{
    clearTransitionRules();
}

// -------------------------------------

SimpleTimeZone::SimpleTimeZone(int32_t rawOffsetGMT, const UnicodeString& ID,
    int8_t savingsStartMonth, int8_t savingsStartDay,
    int8_t savingsStartDayOfWeek, int32_t savingsStartTime,
    int8_t savingsEndMonth, int8_t savingsEndDay,
    int8_t savingsEndDayOfWeek, int32_t savingsEndTime,
    UErrorCode& status)
:   BasicTimeZone(ID)
{
    clearTransitionRules();
    construct(rawOffsetGMT,
              savingsStartMonth, savingsStartDay, savingsStartDayOfWeek,
              savingsStartTime, WALL_TIME,
              savingsEndMonth, savingsEndDay, savingsEndDayOfWeek,
              savingsEndTime, WALL_TIME,
              U_MILLIS_PER_HOUR, status);
}

// -------------------------------------

SimpleTimeZone::SimpleTimeZone(int32_t rawOffsetGMT, const UnicodeString& ID,
    int8_t savingsStartMonth, int8_t savingsStartDay,
    int8_t savingsStartDayOfWeek, int32_t savingsStartTime,
    int8_t savingsEndMonth, int8_t savingsEndDay,
    int8_t savingsEndDayOfWeek, int32_t savingsEndTime,
    int32_t savingsDST, UErrorCode& status)
:   BasicTimeZone(ID)
{
    clearTransitionRules();
    construct(rawOffsetGMT,
              savingsStartMonth, savingsStartDay, savingsStartDayOfWeek,
              savingsStartTime, WALL_TIME,
              savingsEndMonth, savingsEndDay, savingsEndDayOfWeek,
              savingsEndTime, WALL_TIME,
              savingsDST, status);
}

// -------------------------------------

SimpleTimeZone::SimpleTimeZone(int32_t rawOffsetGMT, const UnicodeString& ID,
    int8_t savingsStartMonth, int8_t savingsStartDay,
    int8_t savingsStartDayOfWeek, int32_t savingsStartTime,
    TimeMode savingsStartTimeMode,
    int8_t savingsEndMonth, int8_t savingsEndDay,
    int8_t savingsEndDayOfWeek, int32_t savingsEndTime,
    TimeMode savingsEndTimeMode,
    int32_t savingsDST, UErrorCode& status)
:   BasicTimeZone(ID)
{
    clearTransitionRules();
    construct(rawOffsetGMT,
              savingsStartMonth, savingsStartDay, savingsStartDayOfWeek,
              savingsStartTime, savingsStartTimeMode,
              savingsEndMonth, savingsEndDay, savingsEndDayOfWeek,
              savingsEndTime, savingsEndTimeMode,
              savingsDST, status);
}

/**
 * Internal construction method.
 */
void SimpleTimeZone::construct(int32_t rawOffsetGMT,
                               int8_t savingsStartMonth,
                               int8_t savingsStartDay,
                               int8_t savingsStartDayOfWeek,
                               int32_t savingsStartTime,
                               TimeMode savingsStartTimeMode,
                               int8_t savingsEndMonth,
                               int8_t savingsEndDay,
                               int8_t savingsEndDayOfWeek,
                               int32_t savingsEndTime,
                               TimeMode savingsEndTimeMode,
                               int32_t savingsDST,
                               UErrorCode& status)
{
    this->rawOffset      = rawOffsetGMT;
    this->startMonth     = savingsStartMonth;
    this->startDay       = savingsStartDay;
    this->startDayOfWeek = savingsStartDayOfWeek;
    this->startTime      = savingsStartTime;
    this->startTimeMode  = savingsStartTimeMode;
    this->endMonth       = savingsEndMonth;
    this->endDay         = savingsEndDay;
    this->endDayOfWeek   = savingsEndDayOfWeek;
    this->endTime        = savingsEndTime;
    this->endTimeMode    = savingsEndTimeMode;
    this->dstSavings     = savingsDST;
    this->startYear      = 0;
    this->startMode      = DOM_MODE;
    this->endMode        = DOM_MODE;

    decodeRules(status);

    if (savingsDST == 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
}

// -------------------------------------

SimpleTimeZone::~SimpleTimeZone()
{
    deleteTransitionRules();
}

// -------------------------------------

// Called by TimeZone::createDefault(), then clone() inside a Mutex - be careful.
SimpleTimeZone::SimpleTimeZone(const SimpleTimeZone &source)
:   BasicTimeZone(source)
{
    *this = source;
}

// -------------------------------------

// Called by TimeZone::createDefault(), then clone() inside a Mutex - be careful.
SimpleTimeZone &
SimpleTimeZone::operator=(const SimpleTimeZone &right)
{
    if (this != &right)
    {
        TimeZone::operator=(right);
        rawOffset      = right.rawOffset;
        startMonth     = right.startMonth;
        startDay       = right.startDay;
        startDayOfWeek = right.startDayOfWeek;
        startTime      = right.startTime;
        startTimeMode  = right.startTimeMode;
        startMode      = right.startMode;
        endMonth       = right.endMonth;
        endDay         = right.endDay;
        endDayOfWeek   = right.endDayOfWeek;
        endTime        = right.endTime;
        endTimeMode    = right.endTimeMode;
        endMode        = right.endMode;
        startYear      = right.startYear;
        dstSavings     = right.dstSavings;
        useDaylight    = right.useDaylight;
        clearTransitionRules();
    }
    return *this;
}

// -------------------------------------

bool
SimpleTimeZone::operator==(const TimeZone& that) const
{
    return ((this == &that) ||
            (typeid(*this) == typeid(that) &&
            TimeZone::operator==(that) &&
            hasSameRules(that)));
}

// -------------------------------------

// Called by TimeZone::createDefault() inside a Mutex - be careful.
SimpleTimeZone*
SimpleTimeZone::clone() const
{
    return new SimpleTimeZone(*this);
}

// -------------------------------------

/**
 * Sets the daylight savings starting year, that is, the year this time zone began
 * observing its specified daylight savings time rules.  The time zone is considered
 * not to observe daylight savings time prior to that year; SimpleTimeZone doesn't
 * support historical daylight-savings-time rules.
 * @param year the daylight savings starting year.
 */
void
SimpleTimeZone::setStartYear(int32_t year)
{
    startYear = year;
    transitionRulesInitialized = false;
}

// -------------------------------------

/**
 * Sets the daylight savings starting rule. For example, in the U.S., Daylight Savings
 * Time starts at the first Sunday in April, at 2 AM in standard time.
 * Therefore, you can set the start rule by calling:
 * setStartRule(TimeFields.APRIL, 1, TimeFields.SUNDAY, 2*60*60*1000);
 * The dayOfWeekInMonth and dayOfWeek parameters together specify how to calculate
 * the exact starting date.  Their exact meaning depend on their respective signs,
 * allowing various types of rules to be constructed, as follows:<ul>
 *   <li>If both dayOfWeekInMonth and dayOfWeek are positive, they specify the
 *       day of week in the month (e.g., (2, WEDNESDAY) is the second Wednesday
 *       of the month).
 *   <li>If dayOfWeek is positive and dayOfWeekInMonth is negative, they specify
 *       the day of week in the month counting backward from the end of the month.
 *       (e.g., (-1, MONDAY) is the last Monday in the month)
 *   <li>If dayOfWeek is zero and dayOfWeekInMonth is positive, dayOfWeekInMonth
 *       specifies the day of the month, regardless of what day of the week it is.
 *       (e.g., (10, 0) is the tenth day of the month)
 *   <li>If dayOfWeek is zero and dayOfWeekInMonth is negative, dayOfWeekInMonth
 *       specifies the day of the month counting backward from the end of the
 *       month, regardless of what day of the week it is (e.g., (-2, 0) is the
 *       next-to-last day of the month).
 *   <li>If dayOfWeek is negative and dayOfWeekInMonth is positive, they specify the
 *       first specified day of the week on or after the specified day of the month.
 *       (e.g., (15, -SUNDAY) is the first Sunday after the 15th of the month
 *       [or the 15th itself if the 15th is a Sunday].)
 *   <li>If dayOfWeek and DayOfWeekInMonth are both negative, they specify the
 *       last specified day of the week on or before the specified day of the month.
 *       (e.g., (-20, -TUESDAY) is the last Tuesday before the 20th of the month
 *       [or the 20th itself if the 20th is a Tuesday].)</ul>
 * @param month the daylight savings starting month. Month is 0-based.
 * eg, 0 for January.
 * @param dayOfWeekInMonth the daylight savings starting
 * day-of-week-in-month. Please see the member description for an example.
 * @param dayOfWeek the daylight savings starting day-of-week. Please see
 * the member description for an example.
 * @param time the daylight savings starting time. Please see the member
 * description for an example.
 */
 
void
SimpleTimeZone::setStartRule(int32_t month, int32_t dayOfWeekInMonth, int32_t dayOfWeek,
                             int32_t time, TimeMode mode, UErrorCode& status)
{
    startMonth = static_cast<int8_t>(month);
    startDay = static_cast<int8_t>(dayOfWeekInMonth);
    startDayOfWeek = static_cast<int8_t>(dayOfWeek);
    startTime      = time;
    startTimeMode  = mode;
    decodeStartRule(status);
    transitionRulesInitialized = false;
}

// -------------------------------------

void 
SimpleTimeZone::setStartRule(int32_t month, int32_t dayOfMonth, 
                             int32_t time, TimeMode mode, UErrorCode& status) 
{
    setStartRule(month, dayOfMonth, 0, time, mode, status);
}

// -------------------------------------

void 
SimpleTimeZone::setStartRule(int32_t month, int32_t dayOfMonth, int32_t dayOfWeek, 
                             int32_t time, TimeMode mode, UBool after, UErrorCode& status)
{
    setStartRule(month, after ? dayOfMonth : -dayOfMonth,
                 -dayOfWeek, time, mode, status);
}

// -------------------------------------

/**
 * Sets the daylight savings ending rule. For example, in the U.S., Daylight
 * Savings Time ends at the last (-1) Sunday in October, at 2 AM in standard time.
 * Therefore, you can set the end rule by calling:
 * setEndRule(TimeFields.OCTOBER, -1, TimeFields.SUNDAY, 2*60*60*1000);
 * Various other types of rules can be specified by manipulating the dayOfWeek
 * and dayOfWeekInMonth parameters.  For complete details, see the documentation
 * for setStartRule().
 * @param month the daylight savings ending month. Month is 0-based.
 * eg, 0 for January.
 * @param dayOfWeekInMonth the daylight savings ending
 * day-of-week-in-month. See setStartRule() for a complete explanation.
 * @param dayOfWeek the daylight savings ending day-of-week. See setStartRule()
 * for a complete explanation.
 * @param time the daylight savings ending time. Please see the member
 * description for an example.
 */

void
SimpleTimeZone::setEndRule(int32_t month, int32_t dayOfWeekInMonth, int32_t dayOfWeek,
                           int32_t time, TimeMode mode, UErrorCode& status)
{
    endMonth = static_cast<int8_t>(month);
    endDay = static_cast<int8_t>(dayOfWeekInMonth);
    endDayOfWeek = static_cast<int8_t>(dayOfWeek);
    endTime      = time;
    endTimeMode  = mode;
    decodeEndRule(status);
    transitionRulesInitialized = false;
}

// -------------------------------------

void 
SimpleTimeZone::setEndRule(int32_t month, int32_t dayOfMonth, 
                           int32_t time, TimeMode mode, UErrorCode& status)
{
    setEndRule(month, dayOfMonth, 0, time, mode, status);
}

// -------------------------------------

void 
SimpleTimeZone::setEndRule(int32_t month, int32_t dayOfMonth, int32_t dayOfWeek, 
                           int32_t time, TimeMode mode, UBool after, UErrorCode& status)
{
    setEndRule(month, after ? dayOfMonth : -dayOfMonth,
               -dayOfWeek, time, mode, status);
}

// -------------------------------------

int32_t
SimpleTimeZone::getOffset(uint8_t era, int32_t year, int32_t month, int32_t day,
                          uint8_t dayOfWeek, int32_t millis, UErrorCode& status) const
{
    // Check the month before calling Grego::monthLength(). This
    // duplicates the test that occurs in the 7-argument getOffset(),
    // however, this is unavoidable. We don't mind because this method, in
    // fact, should not be called; internal code should always call the
    // 7-argument getOffset(), and outside code should use Calendar.get(int
    // field) with fields ZONE_OFFSET and DST_OFFSET. We can't get rid of
    // this method because it's public API. - liu 8/10/98
    if(month < UCAL_JANUARY || month > UCAL_DECEMBER) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    return getOffset(era, year, month, day, dayOfWeek, millis, Grego::monthLength(year, month), status);
}

int32_t 
SimpleTimeZone::getOffset(uint8_t era, int32_t year, int32_t month, int32_t day,
                          uint8_t dayOfWeek, int32_t millis, 
                          int32_t /*monthLength*/, UErrorCode& status) const
{
    // Check the month before calling Grego::monthLength(). This
    // duplicates a test that occurs in the 9-argument getOffset(),
    // however, this is unavoidable. We don't mind because this method, in
    // fact, should not be called; internal code should always call the
    // 9-argument getOffset(), and outside code should use Calendar.get(int
    // field) with fields ZONE_OFFSET and DST_OFFSET. We can't get rid of
    // this method because it's public API. - liu 8/10/98
    if (month < UCAL_JANUARY
        || month > UCAL_DECEMBER) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    // We ignore monthLength because it can be derived from year and month.
    // This is so that February in leap years is calculated correctly.
    // We keep this argument in this function for backwards compatibility.
    return getOffset(era, year, month, day, dayOfWeek, millis,
                     Grego::monthLength(year, month),
                     Grego::previousMonthLength(year, month),
                     status);
}

int32_t 
SimpleTimeZone::getOffset(uint8_t era, int32_t year, int32_t month, int32_t day,
                          uint8_t dayOfWeek, int32_t millis, 
                          int32_t monthLength, int32_t prevMonthLength,
                          UErrorCode& status) const
{
    if(U_FAILURE(status)) return 0;

    if ((era != GregorianCalendar::AD && era != GregorianCalendar::BC)
        || month < UCAL_JANUARY
        || month > UCAL_DECEMBER
        || day < 1
        || day > monthLength
        || dayOfWeek < UCAL_SUNDAY
        || dayOfWeek > UCAL_SATURDAY
        || millis < 0
        || millis >= U_MILLIS_PER_DAY
        || monthLength < 28
        || monthLength > 31
        || prevMonthLength < 28
        || prevMonthLength > 31) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    int32_t result = rawOffset;

    // Bail out if we are before the onset of daylight savings time
    if(!useDaylight || year < startYear || era != GregorianCalendar::AD) 
        return result;

    // Check for southern hemisphere.  We assume that the start and end
    // month are different.
    UBool southern = (startMonth > endMonth);

    // Compare the date to the starting and ending rules.+1 = date>rule, -1
    // = date<rule, 0 = date==rule.
    int32_t startCompare = compareToRule(static_cast<int8_t>(month), static_cast<int8_t>(monthLength), static_cast<int8_t>(prevMonthLength),
                                         static_cast<int8_t>(day), static_cast<int8_t>(dayOfWeek), millis,
                                         startTimeMode == UTC_TIME ? -rawOffset : 0,
                                         startMode, startMonth, startDayOfWeek,
                                         startDay, startTime);
    int32_t endCompare = 0;

    /* We don't always have to compute endCompare.  For many instances,
     * startCompare is enough to determine if we are in DST or not.  In the
     * northern hemisphere, if we are before the start rule, we can't have
     * DST.  In the southern hemisphere, if we are after the start rule, we
     * must have DST.  This is reflected in the way the next if statement
     * (not the one immediately following) short circuits. */
    if(southern != (startCompare >= 0)) {
        endCompare = compareToRule(static_cast<int8_t>(month), static_cast<int8_t>(monthLength), static_cast<int8_t>(prevMonthLength),
                                   static_cast<int8_t>(day), static_cast<int8_t>(dayOfWeek), millis,
                                   endTimeMode == WALL_TIME ? dstSavings :
                                    (endTimeMode == UTC_TIME ? -rawOffset : 0),
                                   endMode, endMonth, endDayOfWeek,
                                   endDay, endTime);
    }

    // Check for both the northern and southern hemisphere cases.  We
    // assume that in the northern hemisphere, the start rule is before the
    // end rule within the calendar year, and vice versa for the southern
    // hemisphere.
    if ((!southern && (startCompare >= 0 && endCompare < 0)) ||
        (southern && (startCompare >= 0 || endCompare < 0)))
        result += dstSavings;

    return result;
}

void
SimpleTimeZone::getOffsetFromLocal(UDate date, UTimeZoneLocalOption nonExistingTimeOpt,
                                   UTimeZoneLocalOption duplicatedTimeOpt, int32_t& rawOffsetGMT,
                                   int32_t& savingsDST, UErrorCode& status) const
{
    if (U_FAILURE(status)) {
        return;
    }

    rawOffsetGMT = getRawOffset();
    int32_t year, month, dom, dow, millis;
    double dday = ClockMath::floorDivide(date, U_MILLIS_PER_DAY, &millis);
    if (dday > INT32_MAX || dday < INT32_MIN) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    int32_t day = dday;

    Grego::dayToFields(day, year, month, dom, dow, status);
    if (U_FAILURE(status)) return;

    savingsDST = getOffset(GregorianCalendar::AD, year, month, dom,
                          static_cast<uint8_t>(dow), millis,
                          Grego::monthLength(year, month),
                          status) - rawOffsetGMT;
    if (U_FAILURE(status)) {
        return;
    }

    UBool recalc = false;

    // Now we need some adjustment
    if (savingsDST > 0) {
        if ((nonExistingTimeOpt & kStdDstMask) == kStandard
            || ((nonExistingTimeOpt & kStdDstMask) != kDaylight && (nonExistingTimeOpt & kFormerLatterMask) != kLatter)) {
            date -= getDSTSavings();
            recalc = true;
        }
    } else {
        if ((duplicatedTimeOpt & kStdDstMask) == kDaylight
                || ((duplicatedTimeOpt & kStdDstMask) != kStandard && (duplicatedTimeOpt & kFormerLatterMask) == kFormer)) {
            date -= getDSTSavings();
            recalc = true;
        }
    }
    if (recalc) {
        day = ClockMath::floorDivide(date, U_MILLIS_PER_DAY, &millis);
        Grego::dayToFields(day, year, month, dom, dow, status);
        if (U_FAILURE(status)) return;
        savingsDST = getOffset(GregorianCalendar::AD, year, month, dom,
                          static_cast<uint8_t>(dow), millis,
                          Grego::monthLength(year, month),
                          status) - rawOffsetGMT;
    }
}

// -------------------------------------

/**
 * Compare a given date in the year to a rule. Return 1, 0, or -1, depending
 * on whether the date is after, equal to, or before the rule date. The
 * millis are compared directly against the ruleMillis, so any
 * standard-daylight adjustments must be handled by the caller.
 *
 * @return  1 if the date is after the rule date, -1 if the date is before
 *          the rule date, or 0 if the date is equal to the rule date.
 */
int32_t 
SimpleTimeZone::compareToRule(int8_t month, int8_t monthLen, int8_t prevMonthLen,
                              int8_t dayOfMonth,
                              int8_t dayOfWeek, int32_t millis, int32_t millisDelta,
                              EMode ruleMode, int8_t ruleMonth, int8_t ruleDayOfWeek,
                              int8_t ruleDay, int32_t ruleMillis)
{
    // Make adjustments for startTimeMode and endTimeMode
    millis += millisDelta;
    while (millis >= U_MILLIS_PER_DAY) {
        millis -= U_MILLIS_PER_DAY;
        ++dayOfMonth;
        dayOfWeek = static_cast<int8_t>(1 + (dayOfWeek % 7)); // dayOfWeek is one-based
        if (dayOfMonth > monthLen) {
            dayOfMonth = 1;
            /* When incrementing the month, it is desirable to overflow
             * from DECEMBER to DECEMBER+1, since we use the result to
             * compare against a real month. Wraparound of the value
             * leads to bug 4173604. */
            ++month;
        }
    }
    while (millis < 0) {
        millis += U_MILLIS_PER_DAY;
        --dayOfMonth;
        dayOfWeek = static_cast<int8_t>(1 + ((dayOfWeek + 5) % 7)); // dayOfWeek is one-based
        if (dayOfMonth < 1) {
            dayOfMonth = prevMonthLen;
            --month;
        }
    }

    // first compare months.  If they're different, we don't have to worry about days
    // and times
    if (month < ruleMonth) return -1;
    else if (month > ruleMonth) return 1;

    // calculate the actual day of month for the rule
    int32_t ruleDayOfMonth = 0;

    // Adjust the ruleDay to the monthLen, for non-leap year February 29 rule days.
    if (ruleDay > monthLen) {
        ruleDay = monthLen;
    }

    switch (ruleMode)
    {
    // if the mode is day-of-month, the day of month is given
    case DOM_MODE:
        ruleDayOfMonth = ruleDay;
        break;

    // if the mode is day-of-week-in-month, calculate the day-of-month from it
    case DOW_IN_MONTH_MODE:
        // In this case ruleDay is the day-of-week-in-month (this code is using
        // the dayOfWeek and dayOfMonth parameters to figure out the day-of-week
        // of the first day of the month, so it's trusting that they're really
        // consistent with each other)
        if (ruleDay > 0)
            ruleDayOfMonth = 1 + (ruleDay - 1) * 7 +
                (7 + ruleDayOfWeek - (dayOfWeek - dayOfMonth + 1)) % 7;
        
        // if ruleDay is negative (we assume it's not zero here), we have to do
        // the same calculation figuring backward from the last day of the month.
        else
        {
            // (again, this code is trusting that dayOfWeek and dayOfMonth are
            // consistent with each other here, since we're using them to figure
            // the day of week of the first of the month)
            ruleDayOfMonth = monthLen + (ruleDay + 1) * 7 -
                (7 + (dayOfWeek + monthLen - dayOfMonth) - ruleDayOfWeek) % 7;
        }
        break;

    case DOW_GE_DOM_MODE:
        ruleDayOfMonth = ruleDay +
            (49 + ruleDayOfWeek - ruleDay - dayOfWeek + dayOfMonth) % 7;
        break;

    case DOW_LE_DOM_MODE:
        ruleDayOfMonth = ruleDay -
            (49 - ruleDayOfWeek + ruleDay + dayOfWeek - dayOfMonth) % 7;
        // Note at this point ruleDayOfMonth may be <1, although it will
        // be >=1 for well-formed rules.
        break;
    }

    // now that we have a real day-in-month for the rule, we can compare days...
    if (dayOfMonth < ruleDayOfMonth) return -1;
    else if (dayOfMonth > ruleDayOfMonth) return 1;

    // ...and if they're equal, we compare times
    if (millis < ruleMillis) return -1;
    else if (millis > ruleMillis) return 1;
    else return 0;
}

// -------------------------------------

int32_t
SimpleTimeZone::getRawOffset() const
{
    return rawOffset;
}

// -------------------------------------

void
SimpleTimeZone::setRawOffset(int32_t offsetMillis)
{
    rawOffset = offsetMillis;
    transitionRulesInitialized = false;
}

// -------------------------------------

void 
SimpleTimeZone::setDSTSavings(int32_t millisSavedDuringDST, UErrorCode& status) 
{
    if (millisSavedDuringDST == 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
    else {
        dstSavings = millisSavedDuringDST;
    }
    transitionRulesInitialized = false;
}

// -------------------------------------

int32_t 
SimpleTimeZone::getDSTSavings() const
{
    return dstSavings;
}

// -------------------------------------

UBool
SimpleTimeZone::useDaylightTime() const
{
    return useDaylight;
}

// -------------------------------------

/**
 * Overrides TimeZone
 * Queries if the given date is in Daylight Savings Time.
 */
UBool SimpleTimeZone::inDaylightTime(UDate date, UErrorCode& status) const
{
    // This method is wasteful since it creates a new GregorianCalendar and
    // deletes it each time it is called.  However, this is a deprecated method
    // and provided only for Java compatibility as of 8/6/97 [LIU].
    if (U_FAILURE(status)) return false;
    GregorianCalendar *gc = new GregorianCalendar(*this, status);
    /* test for nullptr */
    if (gc == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return false;
    }
    gc->setTime(date, status);
    UBool result = gc->inDaylightTime(status);
    delete gc;
    return result;
}

// -------------------------------------

/**
 * Return true if this zone has the same rules and offset as another zone.
 * @param other the TimeZone object to be compared with
 * @return true if the given zone has the same rules and offset as this one
 */
UBool 
SimpleTimeZone::hasSameRules(const TimeZone& other) const
{
    if (this == &other) return true;
    if (typeid(*this) != typeid(other)) return false;
    SimpleTimeZone *that = (SimpleTimeZone*)&other;
    return rawOffset     == that->rawOffset &&
        useDaylight     == that->useDaylight &&
        (!useDaylight
         // Only check rules if using DST
         || (dstSavings     == that->dstSavings &&
             startMode      == that->startMode &&
             startMonth     == that->startMonth &&
             startDay       == that->startDay &&
             startDayOfWeek == that->startDayOfWeek &&
             startTime      == that->startTime &&
             startTimeMode  == that->startTimeMode &&
             endMode        == that->endMode &&
             endMonth       == that->endMonth &&
             endDay         == that->endDay &&
             endDayOfWeek   == that->endDayOfWeek &&
             endTime        == that->endTime &&
             endTimeMode    == that->endTimeMode &&
             startYear      == that->startYear));
}

// -------------------------------------

//----------------------------------------------------------------------
// Rule representation
//
// We represent the following flavors of rules:
//       5        the fifth of the month
//       lastSun  the last Sunday in the month
//       lastMon  the last Monday in the month
//       Sun>=8   first Sunday on or after the eighth
//       Sun<=25  last Sunday on or before the 25th
// This is further complicated by the fact that we need to remain
// backward compatible with the 1.1 FCS.  Finally, we need to minimize
// API changes.  In order to satisfy these requirements, we support
// three representation systems, and we translate between them.
//
// INTERNAL REPRESENTATION
// This is the format SimpleTimeZone objects take after construction or
// streaming in is complete.  Rules are represented directly, using an
// unencoded format.  We will discuss the start rule only below; the end
// rule is analogous.
//   startMode      Takes on enumerated values DAY_OF_MONTH,
//                  DOW_IN_MONTH, DOW_AFTER_DOM, or DOW_BEFORE_DOM.
//   startDay       The day of the month, or for DOW_IN_MONTH mode, a
//                  value indicating which DOW, such as +1 for first,
//                  +2 for second, -1 for last, etc.
//   startDayOfWeek The day of the week.  Ignored for DAY_OF_MONTH.
//
// ENCODED REPRESENTATION
// This is the format accepted by the constructor and by setStartRule()
// and setEndRule().  It uses various combinations of positive, negative,
// and zero values to encode the different rules.  This representation
// allows us to specify all the different rule flavors without altering
// the API.
//   MODE              startMonth    startDay    startDayOfWeek
//   DOW_IN_MONTH_MODE >=0           !=0         >0
//   DOM_MODE          >=0           >0          ==0
//   DOW_GE_DOM_MODE   >=0           >0          <0
//   DOW_LE_DOM_MODE   >=0           <0          <0
//   (no DST)          don't care    ==0         don't care
//
// STREAMED REPRESENTATION
// We must retain binary compatibility with the 1.1 FCS.  The 1.1 code only
// handles DOW_IN_MONTH_MODE and non-DST mode, the latter indicated by the
// flag useDaylight.  When we stream an object out, we translate into an
// approximate DOW_IN_MONTH_MODE representation so the object can be parsed
// and used by 1.1 code.  Following that, we write out the full
// representation separately so that contemporary code can recognize and
// parse it.  The full representation is written in a "packed" format,
// consisting of a version number, a length, and an array of bytes.  Future
// versions of this class may specify different versions.  If they wish to
// include additional data, they should do so by storing them after the
// packed representation below.
//----------------------------------------------------------------------

/**
 * Given a set of encoded rules in startDay and startDayOfMonth, decode
 * them and set the startMode appropriately.  Do the same for endDay and
 * endDayOfMonth.  Upon entry, the day of week variables may be zero or
 * negative, in order to indicate special modes.  The day of month
 * variables may also be negative.  Upon exit, the mode variables will be
 * set, and the day of week and day of month variables will be positive.
 * This method also recognizes a startDay or endDay of zero as indicating
 * no DST.
 */
void 
SimpleTimeZone::decodeRules(UErrorCode& status)
{
    decodeStartRule(status);
    decodeEndRule(status);
}

/**
 * Decode the start rule and validate the parameters.  The parameters are
 * expected to be in encoded form, which represents the various rule modes
 * by negating or zeroing certain values.  Representation formats are:
 * <p>
 * <pre>
 *            DOW_IN_MONTH  DOM    DOW>=DOM  DOW<=DOM  no DST
 *            ------------  -----  --------  --------  ----------
 * month       0..11        same    same      same     don't care
 * day        -5..5         1..31   1..31    -1..-31   0
 * dayOfWeek   1..7         0      -1..-7    -1..-7    don't care
 * time        0..ONEDAY    same    same      same     don't care
 * </pre>
 * The range for month does not include UNDECIMBER since this class is
 * really specific to GregorianCalendar, which does not use that month.
 * The range for time includes ONEDAY (vs. ending at ONEDAY-1) because the
 * end rule is an exclusive limit point.  That is, the range of times that
 * are in DST include those >= the start and < the end.  For this reason,
 * it should be possible to specify an end of ONEDAY in order to include the
 * entire day.  Although this is equivalent to time 0 of the following day,
 * it's not always possible to specify that, for example, on December 31.
 * While arguably the start range should still be 0..ONEDAY-1, we keep
 * the start and end ranges the same for consistency.
 */
void 
SimpleTimeZone::decodeStartRule(UErrorCode& status) 
{
    if(U_FAILURE(status)) return;

    useDaylight = static_cast<UBool>(startDay != 0 && endDay != 0);
    if (useDaylight && dstSavings == 0) {
        dstSavings = U_MILLIS_PER_HOUR;
    }
    if (startDay != 0) {
        if (startMonth < UCAL_JANUARY || startMonth > UCAL_DECEMBER) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        if (startTime < 0 || startTime > U_MILLIS_PER_DAY ||
            startTimeMode < WALL_TIME || startTimeMode > UTC_TIME) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        if (startDayOfWeek == 0) {
            startMode = DOM_MODE;
        } else {
            if (startDayOfWeek > 0) {
                startMode = DOW_IN_MONTH_MODE;
            } else {
                startDayOfWeek = static_cast<int8_t>(-startDayOfWeek);
                if (startDay > 0) {
                    startMode = DOW_GE_DOM_MODE;
                } else {
                    startDay = static_cast<int8_t>(-startDay);
                    startMode = DOW_LE_DOM_MODE;
                }
            }
            if (startDayOfWeek > UCAL_SATURDAY) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return;
            }
        }
        if (startMode == DOW_IN_MONTH_MODE) {
            if (startDay < -5 || startDay > 5) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return;
            }
        } else if (startDay<1 || startDay > STATICMONTHLENGTH[startMonth]) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
    }
}

/**
 * Decode the end rule and validate the parameters.  This method is exactly
 * analogous to decodeStartRule().
 * @see decodeStartRule
 */
void 
SimpleTimeZone::decodeEndRule(UErrorCode& status) 
{
    if(U_FAILURE(status)) return;

    useDaylight = static_cast<UBool>(startDay != 0 && endDay != 0);
    if (useDaylight && dstSavings == 0) {
        dstSavings = U_MILLIS_PER_HOUR;
    }
    if (endDay != 0) {
        if (endMonth < UCAL_JANUARY || endMonth > UCAL_DECEMBER) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        if (endTime < 0 || endTime > U_MILLIS_PER_DAY ||
            endTimeMode < WALL_TIME || endTimeMode > UTC_TIME) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        if (endDayOfWeek == 0) {
            endMode = DOM_MODE;
        } else {
            if (endDayOfWeek > 0) {
                endMode = DOW_IN_MONTH_MODE;
            } else {
                endDayOfWeek = static_cast<int8_t>(-endDayOfWeek);
                if (endDay > 0) {
                    endMode = DOW_GE_DOM_MODE;
                } else {
                    endDay = static_cast<int8_t>(-endDay);
                    endMode = DOW_LE_DOM_MODE;
                }
            }
            if (endDayOfWeek > UCAL_SATURDAY) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return;
            }
        }
        if (endMode == DOW_IN_MONTH_MODE) {
            if (endDay < -5 || endDay > 5) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return;
            }
        } else if (endDay<1 || endDay > STATICMONTHLENGTH[endMonth]) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
    }
}

UBool
SimpleTimeZone::getNextTransition(UDate base, UBool inclusive, TimeZoneTransition& result) const {
    if (!useDaylight) {
        return false;
    }

    UErrorCode status = U_ZERO_ERROR;
    checkTransitionRules(status);
    if (U_FAILURE(status)) {
        return false;
    }

    UDate firstTransitionTime = firstTransition->getTime();
    if (base < firstTransitionTime || (inclusive && base == firstTransitionTime)) {
        result = *firstTransition;
    }
    UDate stdDate, dstDate;
    UBool stdAvail = stdRule->getNextStart(base, dstRule->getRawOffset(), dstRule->getDSTSavings(), inclusive, stdDate);
    UBool dstAvail = dstRule->getNextStart(base, stdRule->getRawOffset(), stdRule->getDSTSavings(), inclusive, dstDate);
    if (stdAvail && (!dstAvail || stdDate < dstDate)) {
        result.setTime(stdDate);
        result.setFrom(*dstRule);
        result.setTo(*stdRule);
        return true;
    }
    if (dstAvail && (!stdAvail || dstDate < stdDate)) {
        result.setTime(dstDate);
        result.setFrom(*stdRule);
        result.setTo(*dstRule);
        return true;
    }
    return false;
}

UBool
SimpleTimeZone::getPreviousTransition(UDate base, UBool inclusive, TimeZoneTransition& result) const {
    if (!useDaylight) {
        return false;
    }

    UErrorCode status = U_ZERO_ERROR;
    checkTransitionRules(status);
    if (U_FAILURE(status)) {
        return false;
    }

    UDate firstTransitionTime = firstTransition->getTime();
    if (base < firstTransitionTime || (!inclusive && base == firstTransitionTime)) {
        return false;
    }
    UDate stdDate, dstDate;
    UBool stdAvail = stdRule->getPreviousStart(base, dstRule->getRawOffset(), dstRule->getDSTSavings(), inclusive, stdDate);
    UBool dstAvail = dstRule->getPreviousStart(base, stdRule->getRawOffset(), stdRule->getDSTSavings(), inclusive, dstDate);
    if (stdAvail && (!dstAvail || stdDate > dstDate)) {
        result.setTime(stdDate);
        result.setFrom(*dstRule);
        result.setTo(*stdRule);
        return true;
    }
    if (dstAvail && (!stdAvail || dstDate > stdDate)) {
        result.setTime(dstDate);
        result.setFrom(*stdRule);
        result.setTo(*dstRule);
        return true;
    }
    return false;
}

void
SimpleTimeZone::clearTransitionRules() {
    initialRule = nullptr;
    firstTransition = nullptr;
    stdRule = nullptr;
    dstRule = nullptr;
    transitionRulesInitialized = false;
}

void
SimpleTimeZone::deleteTransitionRules() {
    delete initialRule;
    delete firstTransition;
    delete stdRule;
    delete dstRule;
    clearTransitionRules();
 }

/*
 * Lazy transition rules initializer
 *
 *    Note On the removal of UMTX_CHECK from checkTransitionRules():
 *
 *         It would be faster to have a UInitOnce as part of a SimpleTimeZone object,
 *         which would avoid needing to lock a mutex to check the initialization state.
 *         But we can't easily because simpletz.h is a public header, and including
 *         a UInitOnce as a member of SimpleTimeZone would publicly expose internal ICU headers.
 *
 *         Alternatively we could have a pointer to a UInitOnce in the SimpleTimeZone object,
 *         allocate it in the constructors. This would be a more intrusive change, but doable
 *         if performance turns out to be an issue.
 */

void
SimpleTimeZone::checkTransitionRules(UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    static UMutex gLock;
    umtx_lock(&gLock);
    if (!transitionRulesInitialized) {
        SimpleTimeZone *ncThis = const_cast<SimpleTimeZone*>(this);
        ncThis->initTransitionRules(status);
    }
    umtx_unlock(&gLock);
}

void
SimpleTimeZone::initTransitionRules(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (transitionRulesInitialized) {
        return;
    }
    deleteTransitionRules();
    UnicodeString tzid;
    getID(tzid);

    if (useDaylight) {
        DateTimeRule* dtRule;
        DateTimeRule::TimeRuleType timeRuleType;
        UDate firstStdStart, firstDstStart;

        // Create a TimeZoneRule for daylight saving time
        timeRuleType = (startTimeMode == STANDARD_TIME) ? DateTimeRule::STANDARD_TIME :
            ((startTimeMode == UTC_TIME) ? DateTimeRule::UTC_TIME : DateTimeRule::WALL_TIME);
        switch (startMode) {
        case DOM_MODE:
            dtRule = new DateTimeRule(startMonth, startDay, startTime, timeRuleType);
            break;
        case DOW_IN_MONTH_MODE:
            dtRule = new DateTimeRule(startMonth, startDay, startDayOfWeek, startTime, timeRuleType);
            break;
        case DOW_GE_DOM_MODE:
            dtRule = new DateTimeRule(startMonth, startDay, startDayOfWeek, true, startTime, timeRuleType);
            break;
        case DOW_LE_DOM_MODE:
            dtRule = new DateTimeRule(startMonth, startDay, startDayOfWeek, false, startTime, timeRuleType);
            break;
        default:
            status = U_INVALID_STATE_ERROR;
            return;
        }
        // Check for Null pointer
        if (dtRule == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        // For now, use ID + "(DST)" as the name
        dstRule = new AnnualTimeZoneRule(tzid+UnicodeString(DST_STR), getRawOffset(), getDSTSavings(),
            dtRule, startYear, AnnualTimeZoneRule::MAX_YEAR);
        
        // Check for Null pointer
        if (dstRule == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            deleteTransitionRules();
            return;
        }
 
        // Calculate the first DST start time
        dstRule->getFirstStart(getRawOffset(), 0, firstDstStart);

        // Create a TimeZoneRule for standard time
        timeRuleType = (endTimeMode == STANDARD_TIME) ? DateTimeRule::STANDARD_TIME :
            ((endTimeMode == UTC_TIME) ? DateTimeRule::UTC_TIME : DateTimeRule::WALL_TIME);
        switch (endMode) {
        case DOM_MODE:
            dtRule = new DateTimeRule(endMonth, endDay, endTime, timeRuleType);
            break;
        case DOW_IN_MONTH_MODE:
            dtRule = new DateTimeRule(endMonth, endDay, endDayOfWeek, endTime, timeRuleType);
            break;
        case DOW_GE_DOM_MODE:
            dtRule = new DateTimeRule(endMonth, endDay, endDayOfWeek, true, endTime, timeRuleType);
            break;
        case DOW_LE_DOM_MODE:
            dtRule = new DateTimeRule(endMonth, endDay, endDayOfWeek, false, endTime, timeRuleType);
            break;
        }
        
        // Check for Null pointer
        if (dtRule == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            deleteTransitionRules();
            return;
        }
        // For now, use ID + "(STD)" as the name
        stdRule = new AnnualTimeZoneRule(tzid+UnicodeString(STD_STR), getRawOffset(), 0,
            dtRule, startYear, AnnualTimeZoneRule::MAX_YEAR);
        
        //Check for Null pointer
        if (stdRule == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            deleteTransitionRules();
            return;
        }

        // Calculate the first STD start time
        stdRule->getFirstStart(getRawOffset(), dstRule->getDSTSavings(), firstStdStart);

        // Create a TimeZoneRule for initial time
        if (firstStdStart < firstDstStart) {
            initialRule = new InitialTimeZoneRule(tzid+UnicodeString(DST_STR), getRawOffset(), dstRule->getDSTSavings());
            if (initialRule == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                deleteTransitionRules();
                return;
            }
            firstTransition = new TimeZoneTransition(firstStdStart, *initialRule, *stdRule);
        } else {
            initialRule = new InitialTimeZoneRule(tzid+UnicodeString(STD_STR), getRawOffset(), 0);
            if (initialRule == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                deleteTransitionRules();
                return;
            }
            firstTransition = new TimeZoneTransition(firstDstStart, *initialRule, *dstRule);
        }
        if (firstTransition == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            deleteTransitionRules();
            return;
        }
        
    } else {
        // Create a TimeZoneRule for initial time
        initialRule = new InitialTimeZoneRule(tzid, getRawOffset(), 0);
        // Check for null pointer.
        if (initialRule == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            deleteTransitionRules();
            return;
        }
    }

    transitionRulesInitialized = true;
}

int32_t
SimpleTimeZone::countTransitionRules(UErrorCode& /*status*/) const {
    return (useDaylight) ? 2 : 0;
}

void
SimpleTimeZone::getTimeZoneRules(const InitialTimeZoneRule*& initial,
                                 const TimeZoneRule* trsrules[],
                                 int32_t& trscount,
                                 UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    checkTransitionRules(status);
    if (U_FAILURE(status)) {
        return;
    }
    initial = initialRule;
    int32_t cnt = 0;
    if (stdRule != nullptr) {
        if (cnt < trscount) {
            trsrules[cnt++] = stdRule;
        }
        if (cnt < trscount) {
            trsrules[cnt++] = dstRule;
        }
    }
    trscount = cnt;
}


U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
