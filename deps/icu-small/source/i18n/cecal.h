// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2003 - 2008, International Business Machines Corporation and  *
* others. All Rights Reserved.                                                *
*******************************************************************************
*/

#ifndef CECAL_H
#define CECAL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"

U_NAMESPACE_BEGIN

/**
 * Base class for EthiopicCalendar and CopticCalendar.
 * @internal
 */
class U_I18N_API CECalendar : public Calendar {

public:

   /**
    * Gets The Temporal monthCode value corresponding to the month for the date.
    * The value is a string identifier that starts with the literal grapheme
    * "M" followed by two graphemes representing the zero-padded month number
    * of the current month in a normal (non-leap) year. For the short thirteen
    * month in each year in the CECalendar, the value is "M13".
    *
    * @param status        ICU Error Code
    * @return       One of 13 possible strings in {"M01".. "M12", "M13"}.
    * @draft ICU 73
    */
    virtual const char* getTemporalMonthCode(UErrorCode& status) const override;

    /**
     * Sets The Temporal monthCode which is a string identifier that starts
     * with the literal grapheme "M" followed by two graphemes representing
     * the zero-padded month number of the current month in a normal
     * (non-leap) year. For CECalendar calendar, the values
     * are "M01" .. "M13" while the "M13" is represent the short thirteen month
     * in each year.
     *
     * @param temporalMonth  The value to be set for temporal monthCode.
     * @param status        ICU Error Code
     *
     * @draft ICU 73
     */
    virtual void setTemporalMonthCode(const char* code, UErrorCode& status) override;

protected:
    //-------------------------------------------------------------------------
    // Constructors...
    //-------------------------------------------------------------------------

    /**
     * Constructs a CECalendar based on the current time in the default time zone
     * with the given locale with the Julian epoch offiset
     *
     * @param aLocale  The given locale.
     * @param success  Indicates the status of CECalendar object construction.
     *                 Returns U_ZERO_ERROR if constructed successfully.
     * @internal
     */
    CECalendar(const Locale& aLocale, UErrorCode& success);

    /**
     * Copy Constructor
     * @internal
     */
    CECalendar (const CECalendar& other);

    /**
     * Destructor.
     * @internal
     */
    virtual ~CECalendar();

    /**
     * Default assignment operator
     * @param right    Calendar object to be copied
     * @internal
     */
    CECalendar& operator=(const CECalendar& right);

protected:
    //-------------------------------------------------------------------------
    // Calendar framework
    //-------------------------------------------------------------------------

    /**
     * Return JD of start of given month/extended year
     * @internal
     */
    virtual int64_t handleComputeMonthStart(int32_t eyear, int32_t month, UBool useMonth, UErrorCode& status) const override;

    /**
     * Calculate the limit for a specified type of limit and field
     * @internal
     */
    virtual int32_t handleGetLimit(UCalendarDateFields field, ELimitType limitType) const override;

protected:
    /**
     * The Coptic and Ethiopic calendars differ only in their epochs.
     * This method must be implemented by CECalendar subclasses to
     * return the date offset from Julian
     * @internal
     */
    virtual int32_t getJDEpochOffset() const = 0;

    /**
     * Convert a Julian day to an Coptic/Ethiopic year, month and day
     *
     * @param julianDay the Julian day
     * @param jdEpochOffset the epoch offset from Julian epoch
     * @param year receives the extended year
     * @param month receives the month
     * @param date receives the day
     * @internal
     */
    static void jdToCE(int32_t julianDay, int32_t jdEpochOffset,
        int32_t& year, int32_t& month, int32_t& day, UErrorCode& status);
};

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
#endif
//eof
