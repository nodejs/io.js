// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 ******************************************************************************
 * Copyright (C) 2003-2013, International Business Machines Corporation
 * and others. All Rights Reserved.
 ******************************************************************************
 *
 * File PERSNCAL.H
 *
 * Modification History:
 *
 *   Date        Name        Description
 *   9/23/2003 mehran        posted to icu-design
 *****************************************************************************
 */

#ifndef PERSNCAL_H
#define PERSNCAL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"

U_NAMESPACE_BEGIN

/**
 * <code>PersianCalendar</code> is a subclass of <code>Calendar</code>
 * that implements the Persian calendar.  It is used as the official
 * calendar in Iran.  This calendar is also known as the "Hijri Shamsi"
 * calendar, since it starts at the time of Mohammed's emigration (or
 * "hijra") to Medinah on Thursday, July 15, 622 AD (Julian) and is a
 * solar calendar system (or "shamsi").
 * <p>
 * The Persian calendar is strictly solar, and thus a Persian year has twelve
 * solar months. A Persian year is about 365 days long, except in leap years
 * which is 366 days long.
 * <p>
 * The six first months of Persian Calendar are 31 days long. The next five
 * months are 30 days long. The last month is 29 days long in normal years,
 * and 30 days long in leap years.
 *
 * @see GregorianCalendar
 *
 * @author Mehran Mehr
 * @internal
 */
class PersianCalendar : public Calendar {
 public:
  //-------------------------------------------------------------------------
  // Constants...
  //-------------------------------------------------------------------------
  /**
   * Constants for the months
   * @internal
   */
  enum EMonths {
    /**
     * Constant for Farvardin, the 1st month of the Persian year. 
     * @internal
     */
    FARVARDIN = 0,

    /**
     * Constant for Ordibehesht, the 2nd month of the Persian year. 
     * @internal
     */
    ORDIBEHESHT = 1,

    /**
     * Constant for Khordad, the 3rd month of the Persian year. 
     * @internal 
     */
    KHORDAD = 2,

    /**
     * Constant for Tir, the 4th month of the Persian year. 
     * @internal 
     */
    TIR = 3,

    /**
     * Constant for Mordad, the 5th month of the Persian year. 
     * @internal 
     */
    MORDAD = 4,

    /**
     * Constant for Shahrivar, the 6th month of the Persian year. 
     * @internal 
     */
    SHAHRIVAR = 5,

    /**
     * Constant for Mehr, the 7th month of the Persian year. 
     * @internal 
     */
    MEHR = 6,

    /**
     * Constant for Aban, the 8th month of the Persian year. 
     * @internal 
     */
    ABAN = 7,

    /**
     * Constant for Azar, the 9th month of the Persian year. 
     * @internal 
     */
    AZAR = 8,

    /**
     * Constant for Dei, the 10th month of the Persian year. 
     * @internal 
     */
    DEI = 9,

    /**
     * Constant for Bahman, the 11th month of the Persian year. 
     * @internal 
     */
    BAHMAN = 10,

    /**
     * Constant for Esfand, the 12th month of the Persian year. 
     * @internal 
     */
    ESFAND = 11,
    
    PERSIAN_MONTH_MAX
  }; 



  //-------------------------------------------------------------------------
  // Constructors...
  //-------------------------------------------------------------------------

  /**
   * Constructs a PersianCalendar based on the current time in the default time zone
   * with the given locale.
   *
   * @param aLocale  The given locale.
   * @param success  Indicates the status of PersianCalendar object construction.
   *                 Returns U_ZERO_ERROR if constructed successfully.
   * @internal
   */
  PersianCalendar(const Locale& aLocale, UErrorCode &success);

  /**
   * Copy Constructor
   * @internal
   */
  PersianCalendar(const PersianCalendar& other);

  /**
   * Destructor.
   * @internal
   */
  virtual ~PersianCalendar();

  // TODO: copy c'tor, etc

  // clone
  virtual PersianCalendar* clone() const override;

 private:
  /**
   * Determine whether a year is a leap year in the Persian calendar
   */
  static UBool isLeapYear(int32_t year);
    
  /**
   * Return the day # on which the given year starts.  Days are counted
   * from the Hijri epoch, origin 0.
   */
  int32_t yearStart(int32_t year, UErrorCode& status);

  /**
   * Return the day # on which the given month starts.  Days are counted
   * from the Hijri epoch, origin 0.
   *
   * @param year  The hijri shamsi year
   * @param year  The hijri shamsi month, 0-based
   */
  int32_t monthStart(int32_t year, int32_t month, UErrorCode& status) const;
    
  //----------------------------------------------------------------------
  // Calendar framework
  //----------------------------------------------------------------------
 protected:
  /**
   * @internal
   */
  virtual int32_t handleGetLimit(UCalendarDateFields field, ELimitType limitType) const override;
  
  /**
   * Return the length (in days) of the given month.
   *
   * @param year  The hijri shamsi year
   * @param year  The hijri shamsi month, 0-based
   * @internal
   */
  virtual int32_t handleGetMonthLength(int32_t extendedYear, int32_t month, UErrorCode& status) const override;
  
  /**
   * Return the number of days in the given Persian year
   * @internal
   */
  virtual int32_t handleGetYearLength(int32_t extendedYear) const override;
    
  //-------------------------------------------------------------------------
  // Functions for converting from field values to milliseconds....
  //-------------------------------------------------------------------------

  // Return JD of start of given month/year
  /**
   * @internal
   */
  virtual int64_t handleComputeMonthStart(int32_t eyear, int32_t month, UBool useMonth, UErrorCode& status) const override;

  //-------------------------------------------------------------------------
  // Functions for converting from milliseconds to field values
  //-------------------------------------------------------------------------

  /**
   * @internal
   */
  virtual int32_t handleGetExtendedYear(UErrorCode& status) override;

  /**
   * Override Calendar to compute several fields specific to the Persian
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
   * method is called. The getGregorianXxx() methods return Gregorian
   * calendar equivalents for the given Julian day.
   * @internal
   */
  virtual void handleComputeFields(int32_t julianDay, UErrorCode &status) override;

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
  U_I18N_API static UClassID U_EXPORT2 getStaticClassID();

  /**
   * return the calendar type, "persian".
   *
   * @return calendar type
   * @internal
   */
  virtual const char * getType() const override;

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

 private:
  PersianCalendar(); // default constructor not implemented

 protected:
  DECLARE_OVERRIDE_SYSTEM_DEFAULT_CENTURY
};

U_NAMESPACE_END

#endif
#endif



