// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
* Copyright (C) 2003-2013, International Business Machines Corporation
* and others. All Rights Reserved.
******************************************************************************
*
* File HEBRWCAL.H
*
* Modification History:
*
*   Date        Name        Description
*   05/13/2003  srl          copied from gregocal.h
*   11/26/2003  srl          copied from buddhcal.h
******************************************************************************
*/

#ifndef HEBRWCAL_H
#define HEBRWCAL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"
#include "unicode/gregocal.h"

U_NAMESPACE_BEGIN

/**
 * <code>HebrewCalendar</code> is a subclass of <code>Calendar</code>
 * that that implements the traditional Hebrew calendar.
 * This is the civil calendar in Israel and the liturgical calendar
 * of the Jewish faith worldwide.
 * <p>
 * The Hebrew calendar is lunisolar and thus has a number of interesting
 * properties that distinguish it from the Gregorian.  Months start
 * on the day of (an arithmetic approximation of) each new moon.  Since the
 * solar year (approximately 365.24 days) is not an even multiple of
 * the lunar month (approximately 29.53 days) an extra "leap month" is
 * inserted in 7 out of every 19 years.  To make matters even more
 * interesting, the start of a year can be delayed by up to three days
 * in order to prevent certain holidays from falling on the Sabbath and
 * to prevent certain illegal year lengths.  Finally, the lengths of certain
 * months can vary depending on the number of days in the year.
 * <p>
 * The leap month is known as "Adar 1" and is inserted between the
 * months of Shevat and Adar in leap years.  Since the leap month does
 * not come at the end of the year, calculations involving
 * month numbers are particularly complex.  Users of this class should
 * make sure to use the {@link #roll roll} and {@link #add add} methods
 * rather than attempting to perform date arithmetic by manipulating
 * the fields directly.
 * <p>
 * <b>Note:</b> In the traditional Hebrew calendar, days start at sunset.
 * However, in order to keep the time fields in this class
 * synchronized with those of the other calendars and with local clock time,
 * we treat days and months as beginning at midnight,
 * roughly 6 hours after the corresponding sunset.
 * <p>
 * If you are interested in more information on the rules behind the Hebrew
 * calendar, see one of the following references:
 * <ul>
 * <li>"<a href="http://www.amazon.com/exec/obidos/ASIN/0521564743">Calendrical Calculations</a>",
 *      by Nachum Dershowitz & Edward Reingold, Cambridge University Press, 1997, pages 85-91.
 *
 * <li>Hebrew Calendar Science and Myths,
 *      <a href="http://www.geocities.com/Athens/1584/">
 *      http://www.geocities.com/Athens/1584/</a>
 *
 * <li>The Calendar FAQ,
 *      <a href="http://www.faqs.org/faqs/calendars/faq/">
 *      http://www.faqs.org/faqs/calendars/faq/</a>
 * </ul>
 * <p>
 * @see com.ibm.icu.util.GregorianCalendar
 *
 * @author Laura Werner
 * @author Alan Liu
 * @author Steven R. Loomis
 * <p>
 * @internal
 */
class U_I18N_API HebrewCalendar : public Calendar {
public:
  /**
   * Useful constants for HebrewCalendar.
   * @internal
   */
  enum Month {
    /** 
     * Constant for Tishri, the 1st month of the Hebrew year. 
     */
      TISHRI,
      /**
     * Constant for Heshvan, the 2nd month of the Hebrew year. 
     */
      HESHVAN,
      /**
     * Constant for Kislev, the 3rd month of the Hebrew year. 
     */
      KISLEV,

    /**
     * Constant for Tevet, the 4th month of the Hebrew year. 
     */
      TEVET,

    /**
     * Constant for Shevat, the 5th month of the Hebrew year. 
     */
      SHEVAT,

    /**
     * Constant for Adar I, the 6th month of the Hebrew year
     * (present in leap years only). In non-leap years, the calendar
     * jumps from Shevat (5th month) to Adar (7th month).
     */
      ADAR_1,

    /** 
     * Constant for the Adar, the 7th month of the Hebrew year. 
     */
      ADAR,

    /**
     * Constant for Nisan, the 8th month of the Hebrew year. 
     */
      NISAN,

    /**
     * Constant for Iyar, the 9th month of the Hebrew year. 
     */
      IYAR,

    /**
     * Constant for Sivan, the 10th month of the Hebrew year. 
     */
      SIVAN,

    /**
     * Constant for Tammuz, the 11th month of the Hebrew year. 
     */
      TAMUZ,

    /**
     * Constant for Av, the 12th month of the Hebrew year. 
     */
      AV,

    /**
     * Constant for Elul, the 13th month of the Hebrew year. 
     */
      ELUL
    };

    /**
     * Constructs a HebrewCalendar based on the current time in the default time zone
     * with the given locale.
     *
     * @param aLocale  The given locale.
     * @param success  Indicates the status of HebrewCalendar object construction.
     *                 Returns U_ZERO_ERROR if constructed successfully.
     * @internal
     */
    HebrewCalendar(const Locale& aLocale, UErrorCode& success);


    /**
     * Destructor
     * @internal
     */
    virtual ~HebrewCalendar();

    /**
     * Copy constructor
     * @param source    the object to be copied.
     * @internal
     */
    HebrewCalendar(const HebrewCalendar& source);

    /**
     * Create and return a polymorphic copy of this calendar.
     * @return    return a polymorphic copy of this calendar.
     * @internal
     */
    virtual HebrewCalendar* clone() const override;
    
public:
    /**
     * Override Calendar Returns a unique class ID POLYMORPHICALLY. Pure virtual
     * override. This method is to implement a simple version of RTTI, since not all C++
     * compilers support genuine RTTI. Polymorphic operator==() and clone() methods call
     * this method.
     *
     * @return   The class ID for this object. All objects of a given class have the
     *           same class ID. Objects of other classes have different class IDs.
     * @internal
     */
    virtual UClassID getDynamicClassID() const override;

    /**
     * Return the class ID for this class. This is useful only for comparing to a return
     * value from getDynamicClassID(). For example:
     *
     *      Base* polymorphic_pointer = createPolymorphicObject();
     *      if (polymorphic_pointer->getDynamicClassID() ==
     *          Derived::getStaticClassID()) ...
     *
     * @return   The class ID for all objects of this class.
     * @internal
     */
    static UClassID U_EXPORT2 getStaticClassID();

    /**
     * return the calendar type, "hebrew".
     *
     * @return calendar type
     * @internal
     */
    virtual const char * getType() const override;


    // Calendar API
 public:
    /**
     * (Overrides Calendar) UDate Arithmetic function. Adds the specified (signed) amount
     * of time to the given time field, based on the calendar's rules.  For more
     * information, see the documentation for Calendar::add().
     *
     * @param field   The time field.
     * @param amount  The amount of date or time to be added to the field.
     * @param status  Output param set to success/failure code on exit. If any value
     *                previously set in the time field is invalid, this will be set to
     *                an error status.
     */
    virtual void add(UCalendarDateFields field, int32_t amount, UErrorCode& status) override;
    /**
     * @deprecated ICU 2.6 use UCalendarDateFields instead of EDateFields
     */
    virtual void add(EDateFields field, int32_t amount, UErrorCode& status) override;


    /**
     * (Overrides Calendar) Rolls up or down by the given amount in the specified field.
     * For more information, see the documentation for Calendar::roll().
     *
     * @param field   The time field.
     * @param amount  Indicates amount to roll.
     * @param status  Output param set to success/failure code on exit. If any value
     *                previously set in the time field is invalid, this will be set to
     *                an error status.
     * @internal
     */
    virtual void roll(UCalendarDateFields field, int32_t amount, UErrorCode& status) override;

    /**
     * (Overrides Calendar) Rolls up or down by the given amount in the specified field.
     * For more information, see the documentation for Calendar::roll().
     *
     * @param field   The time field.
     * @param amount  Indicates amount to roll.
     * @param status  Output param set to success/failure code on exit. If any value
     *                previously set in the time field is invalid, this will be set to
     *                an error status.
     * @deprecated ICU 2.6. Use roll(UCalendarDateFields field, int32_t amount, UErrorCode& status) instead.
`     */
    virtual void roll(EDateFields field, int32_t amount, UErrorCode& status) override;

    /**
     * @internal 
     */
    static UBool isLeapYear(int32_t year) ;

    /**
     * @return      The related Gregorian year; will be obtained by modifying the value
     *              obtained by get from UCAL_EXTENDED_YEAR field
     * @internal
     */
    virtual int32_t getRelatedYear(UErrorCode &status) const override;

    /**
     * @param year  The related Gregorian year to set; will be modified as necessary then
     *              set in UCAL_EXTENDED_YEAR field
     * @internal
     */
    virtual void setRelatedYear(int32_t year) override;

 protected:

    /**
     * Subclass API for defining limits of different types.
     * Subclasses must implement this method to return limits for the
     * following fields:
     *
     * <pre>UCAL_ERA
     * UCAL_YEAR
     * UCAL_MONTH
     * UCAL_WEEK_OF_YEAR
     * UCAL_WEEK_OF_MONTH
     * UCAL_DATE (DAY_OF_MONTH on Java)
     * UCAL_DAY_OF_YEAR
     * UCAL_DAY_OF_WEEK_IN_MONTH
     * UCAL_YEAR_WOY
     * UCAL_EXTENDED_YEAR</pre>
     *
     * @param field one of the above field numbers
     * @param limitType one of <code>MINIMUM</code>, <code>GREATEST_MINIMUM</code>,
     * <code>LEAST_MAXIMUM</code>, or <code>MAXIMUM</code>
     * @internal
     */
    virtual int32_t handleGetLimit(UCalendarDateFields field, ELimitType limitType) const override;

    /**
     * Return the number of days in the given month of the given extended
     * year of this calendar system.  Subclasses should override this
     * method if they can provide a more correct or more efficient
     * implementation than the default implementation in Calendar.
     * @internal
     */
    virtual int32_t handleGetMonthLength(int32_t extendedYear, int32_t month, UErrorCode& status) const override;

    /**
     * Return the number of days in the given extended year of this
     * calendar system.  Subclasses should override this method if they can
     * provide a more correct or more efficient implementation than the
     * default implementation in Calendar.
     * @stable ICU 2.0
     */
    virtual int32_t handleGetYearLength(int32_t eyear) const override;
    /**
     * Subclasses may override this method to compute several fields
     * specific to each calendar system.  These are:
     *
     * <ul><li>ERA
     * <li>YEAR
     * <li>MONTH
     * <li>DAY_OF_MONTH
     * <li>DAY_OF_YEAR
     * <li>EXTENDED_YEAR</ul>
     *
     * <p>The GregorianCalendar implementation implements
     * a calendar with the specified Julian/Gregorian cutover date.
     * @internal
     */
    virtual void handleComputeFields(int32_t julianDay, UErrorCode &status) override;
    /**
     * Return the extended year defined by the current fields.  This will
     * use the UCAL_EXTENDED_YEAR field or the UCAL_YEAR and supra-year fields (such
     * as UCAL_ERA) specific to the calendar system, depending on which set of
     * fields is newer.
     * @param status
     * @return the extended year
     * @internal
     */
    virtual int32_t handleGetExtendedYear(UErrorCode& status) override;
    /**
     * Return the Julian day number of day before the first day of the
     * given month in the given extended year.  Subclasses should override
     * this method to implement their calendar system.
     * @param eyear the extended year
     * @param month the zero-based month, or 0 if useMonth is false
     * @param useMonth if false, compute the day before the first day of
     * the given year, otherwise, compute the day before the first day of
     * the given month
     * @param return the Julian day number of the day before the first
     * day of the given month and year
     * @internal
     */
    virtual int64_t handleComputeMonthStart(int32_t eyear, int32_t month,
                                                   UBool useMonth, UErrorCode& status) const override;


    /**
     * Validate a single field of this calendar.
     * Overrides Calendar::validateField(int) to provide
     * special handling for month validation for Hebrew calendar.
     * @internal
     */
    virtual void validateField(UCalendarDateFields field, UErrorCode &status) override;

 protected:
  DECLARE_OVERRIDE_SYSTEM_DEFAULT_CENTURY

 public:
  /**
   * Returns true if the date is in a leap year.
   *
   * @param status        ICU Error Code
   * @return       True if the date in the fields is in a Temporal proposal
   *               defined leap year. False otherwise.
   */
  virtual bool inTemporalLeapYear(UErrorCode& status) const override;

  /**
   * Gets The Temporal monthCode value corresponding to the month for the date.
   * The value is a string identifier that starts with the literal grapheme
   * "M" followed by two graphemes representing the zero-padded month number
   * of the current month in a normal (non-leap) year and suffixed by an
   * optional literal grapheme "L" if this is a leap month in a lunisolar
   * calendar. For the Hebrew calendar, the values are "M01" .. "M12" for
   * non-leap year, and "M01" .. "M05", "M05L", "M06" .. "M12" for leap year.
   *
   * @param status        ICU Error Code
   * @return       One of 13 possible strings in {"M01".. "M05", "M05L",
   * "M06" .. "M12"}.
   * @draft ICU 73
   */
  virtual const char* getTemporalMonthCode(UErrorCode& status) const override;

  /**
   * Sets The Temporal monthCode which is a string identifier that starts
   * with the literal grapheme "M" followed by two graphemes representing
   * the zero-padded month number of the current month in a normal
   * (non-leap) year and suffixed by an optional literal grapheme "L" if this
   * is a leap month in a lunisolar calendar. For Hebrew calendar, the values
   * are "M01" .. "M12" for non-leap years, and "M01" .. "M05", "M05L", "M06"
   * .. "M12" for leap year.
   *
   * @param temporalMonth  The value to be set for temporal monthCode.
   * @param status        ICU Error Code
   *
   * @draft ICU 73
   */
  virtual void setTemporalMonthCode(const char* code, UErrorCode& status ) override;

 protected:
   virtual int32_t internalGetMonth(UErrorCode& status) const override;

 private: // Calendar-specific implementation
    /**
     * Finds the day # of the first day in the given Hebrew year.
     * To do this, we want to calculate the time of the Tishri 1 new moon
     * in that year.
     * <p>
     * The algorithm here is similar to ones described in a number of
     * references, including:
     * <ul>
     * <li>"Calendrical Calculations", by Nachum Dershowitz & Edward Reingold,
     *     Cambridge University Press, 1997, pages 85-91.
     *
     * <li>Hebrew Calendar Science and Myths,
     *     <a href="http://www.geocities.com/Athens/1584/">
     *     http://www.geocities.com/Athens/1584/</a>
     *
     * <li>The Calendar FAQ,
     *      <a href="http://www.faqs.org/faqs/calendars/faq/">
     *      http://www.faqs.org/faqs/calendars/faq/</a>
     * </ul>
     * @param year extended year
     * @return day number (JD)
     * @internal
     */
    static int32_t startOfYear(int32_t year, UErrorCode& status);

    static int32_t absoluteDayToDayOfWeek(int32_t day) ;
    
    /**
     * @internal 
     */
    int32_t yearType(int32_t year) const;

    /**
     * @internal 
     */
    static int32_t monthsInYear(int32_t year) ;
};

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif 
//eof

