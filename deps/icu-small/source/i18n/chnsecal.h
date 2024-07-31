// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 *****************************************************************************
 * Copyright (C) 2007-2013, International Business Machines Corporation
 * and others. All Rights Reserved.
 *****************************************************************************
 *
 * File CHNSECAL.H
 *
 * Modification History:
 *
 *   Date        Name        Description
 *   9/18/2007  ajmacher         ported from java ChineseCalendar
 *****************************************************************************
 */

#ifndef CHNSECAL_H
#define CHNSECAL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"
#include "unicode/timezone.h"

U_NAMESPACE_BEGIN

class CalendarCache;
/**
 * <code>ChineseCalendar</code> is a concrete subclass of {@link Calendar}
 * that implements a traditional Chinese calendar.  The traditional Chinese
 * calendar is a lunisolar calendar: Each month starts on a new moon, and
 * the months are numbered according to solar events, specifically, to
 * guarantee that month 11 always contains the winter solstice.  In order
 * to accomplish this, leap months are inserted in certain years.  Leap
 * months are numbered the same as the month they follow.  The decision of
 * which month is a leap month depends on the relative movements of the sun
 * and moon.
 *
 * <p>This class defines one addition field beyond those defined by
 * <code>Calendar</code>: The <code>IS_LEAP_MONTH</code> field takes the
 * value of 0 for normal months, or 1 for leap months.
 *
 * <p>All astronomical computations are performed with respect to a time
 * zone of GMT+8:00 and a longitude of 120 degrees east.  Although some
 * calendars implement a historically more accurate convention of using
 * Beijing's local longitude (116 degrees 25 minutes east) and time zone
 * (GMT+7:45:40) for dates before 1929, we do not implement this here.
 *
 * <p>Years are counted in two different ways in the Chinese calendar.  The
 * first method is by sequential numbering from the 61st year of the reign
 * of Huang Di, 2637 BCE, which is designated year 1 on the Chinese
 * calendar.  The second method uses 60-year cycles from the same starting
 * point, which is designated year 1 of cycle 1.  In this class, the
 * <code>EXTENDED_YEAR</code> field contains the sequential year count.
 * The <code>ERA</code> field contains the cycle number, and the
 * <code>YEAR</code> field contains the year of the cycle, a value between
 * 1 and 60.
 *
 * <p>There is some variation in what is considered the starting point of
 * the calendar, with some sources starting in the first year of the reign
 * of Huang Di, rather than the 61st.  This gives continuous year numbers
 * 60 years greater and cycle numbers one greater than what this class
 * implements.
 *
 * <p>Because <code>ChineseCalendar</code> defines an additional field and
 * redefines the way the <code>ERA</code> field is used, it requires a new
 * format class, <code>ChineseDateFormat</code>.  As always, use the
 * methods <code>DateFormat.getXxxInstance(Calendar cal,...)</code> to
 * obtain a formatter for this calendar.
 *
 * <p>References:<ul>
 * 
 * <li>Dershowitz and Reingold, <i>Calendrical Calculations</i>,
 * Cambridge University Press, 1997</li>
 * 
 * <li>The <a href="http://www.tondering.dk/claus/calendar.html">
 * Calendar FAQ</a></li>
 *
 * </ul>
 *
 * <p>
 * This class should only be subclassed to implement variants of the Chinese lunar calendar.</p>
 * <p>
 * ChineseCalendar usually should be instantiated using 
 * {@link com.ibm.icu.util.Calendar#getInstance(ULocale)} passing in a <code>ULocale</code>
 * with the tag <code>"@calendar=chinese"</code>.</p>
 *
 * @see com.ibm.icu.text.ChineseDateFormat
 * @see com.ibm.icu.util.Calendar
 * @author Alan Liu
 * @internal
 */
class U_I18N_API ChineseCalendar : public Calendar {
 public:
  //-------------------------------------------------------------------------
  // Constructors...
  //-------------------------------------------------------------------------

  /**
   * Constructs a ChineseCalendar based on the current time in the default time zone
   * with the given locale.
   *
   * @param aLocale  The given locale.
   * @param success  Indicates the status of ChineseCalendar object construction.
   *                 Returns U_ZERO_ERROR if constructed successfully.
   * @internal
   */
  ChineseCalendar(const Locale& aLocale, UErrorCode &success);

  /**
   * Returns true if the date is in a leap year.
   *
   * @param status        ICU Error Code
   * @return       True if the date in the fields is in a Temporal proposal
   *               defined leap year. False otherwise.
   */
  virtual bool inTemporalLeapYear(UErrorCode &status) const override;

  /**
   * Gets The Temporal monthCode value corresponding to the month for the date.
   * The value is a string identifier that starts with the literal grapheme
   * "M" followed by two graphemes representing the zero-padded month number
   * of the current month in a normal (non-leap) year and suffixed by an
   * optional literal grapheme "L" if this is a leap month in a lunisolar
   * calendar. For Chinese calendars (including Dangi), the values are
   * "M01" .. "M12" for non-leap year, and "M01" .. "M12" with one of
   * "M01L" .. "M12L" for leap year.
   *
   * @param status        ICU Error Code
   * @return       One of 24 possible strings in
   *               {"M01" .. "M12", "M01L" .. "M12L"}.
   * @draft ICU 73
   */
  virtual const char* getTemporalMonthCode(UErrorCode &status) const override;

  /**
   * Sets The Temporal monthCode which is a string identifier that starts
   * with the literal grapheme "M" followed by two graphemes representing
   * the zero-padded month number of the current month in a normal
   * (non-leap) year and suffixed by an optional literal grapheme "L" if this
   * is a leap month in a lunisolar calendar. For Chinese calendars, the values
   * are "M01" .. "M12" for non-leap years, and "M01" .. "M12" plus one in
   * "M01L" .. "M12L" for leap year.
   *
   * @param temporalMonth  The value to be set for temporal monthCode. One of
   *                    24 possible strings in {"M01" .. "M12", "M01L" .. "M12L"}.
   * @param status        ICU Error Code
   *
   * @draft ICU 73
   */
  virtual void setTemporalMonthCode(const char* code, UErrorCode& status) override;

 public:
  /**
   * Copy Constructor
   * @internal
   */
  ChineseCalendar(const ChineseCalendar& other);

  /**
   * Destructor.
   * @internal
   */
  virtual ~ChineseCalendar();

  // clone
  virtual ChineseCalendar* clone() const override;

 private:

  //-------------------------------------------------------------------------
  // Internal data....
  //-------------------------------------------------------------------------
    
  // There is a leap month between the Winter Solstice before and after the
  // current date.This is different from leap year because in some year, such as
  // 1813 and 2033, the leap month is after the Winter Solstice of that year. So
  // this value could be false for a date prior to the Winter Solstice of that
  // year but that year still has a leap month and therefor is a leap year.
  UBool hasLeapMonthBetweenWinterSolstices;

  //----------------------------------------------------------------------
  // Calendar framework
  //----------------------------------------------------------------------

 protected:
  virtual int32_t handleGetLimit(UCalendarDateFields field, ELimitType limitType) const override;
  virtual int32_t handleGetMonthLength(int32_t extendedYear, int32_t month, UErrorCode& status) const override;
  virtual int64_t handleComputeMonthStart(int32_t eyear, int32_t month, UBool useMonth, UErrorCode& status) const override;
  virtual int32_t handleGetExtendedYear(UErrorCode& status) override;
  virtual void handleComputeFields(int32_t julianDay, UErrorCode &status) override;
  virtual const UFieldResolutionTable* getFieldResolutionTable() const override;

 public:
  virtual void add(UCalendarDateFields field, int32_t amount, UErrorCode &status) override;
  virtual void add(EDateFields field, int32_t amount, UErrorCode &status) override;
  virtual void roll(UCalendarDateFields field, int32_t amount, UErrorCode &status) override;
  virtual void roll(EDateFields field, int32_t amount, UErrorCode &status) override;

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

  //----------------------------------------------------------------------
  // Internal methods & astronomical calculations
  //----------------------------------------------------------------------

 private:

  static const UFieldResolutionTable CHINESE_DATE_PRECEDENCE[];

  virtual void offsetMonth(int32_t newMoon, int32_t dom, int32_t delta, UErrorCode& status);

  // UObject stuff
 public: 
  /**
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
   * return the calendar type, "chinese".
   *
   * @return calendar type
   * @internal
   */
  virtual const char * getType() const override;

  struct Setting {
      int32_t epochYear;
      const TimeZone* zoneAstroCalc;
      CalendarCache** winterSolsticeCache;
      CalendarCache** newYearCache;
  };
 protected:
  virtual Setting getSetting(UErrorCode& status) const;
  virtual int32_t internalGetMonth(int32_t defaultValue, UErrorCode& status) const override;

  virtual int32_t internalGetMonth(UErrorCode& status) const override;

 protected:

  DECLARE_OVERRIDE_SYSTEM_DEFAULT_CENTURY

 private: // default century stuff.

  ChineseCalendar() = delete; // default constructor not implemented

#ifdef __CalendarTest__
  friend void CalendarTest::TestChineseCalendarComputeMonthStart();
#endif
};

U_NAMESPACE_END

#endif
#endif
