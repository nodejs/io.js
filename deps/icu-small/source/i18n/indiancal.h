// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 *****************************************************************************
 * Copyright (C) 2003-2008, International Business Machines Corporation
 * and others. All Rights Reserved.
 *****************************************************************************
 *
 * File INDIANCAL.H
 *****************************************************************************
 */

#ifndef INDIANCAL_H
#define INDIANCAL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"

U_NAMESPACE_BEGIN

/**
 * Concrete class which provides the Indian calendar.
 * <P>
 * <code>IndianCalendar</code> is a subclass of <code>Calendar</code>
 * that numbers years since the beginning of SAKA ERA.  This is the civil calendar
 * which is accepted by government of India as Indian National Calendar.
 * The two calendars most widely used in India today are the Vikrama calendar
 * followed in North India and the Shalivahana or Saka calendar which is followed
 * in South India and Maharashtra.

 * A variant of the Shalivahana Calendar was reformed and standardized as the
 * Indian National calendar in 1957.
 * <p>
 * Some details of Indian National Calendar (to be implemented) :
 * The Months
 * Month          Length      Start date (Gregorian)
 * =================================================
 * 1 Chaitra      30/31          March 22*
 * 2 Vaisakha     31             April 21
 * 3 Jyaistha     31             May 22
 * 4 Asadha       31             June 22
 * 5 Sravana      31             July 23
 * 6 Bhadra       31             August 23
 * 7 Asvina       30             September 23
 * 8 Kartika      30             October 23
 * 9 Agrahayana   30             November 22
 * 10 Pausa       30             December 22
 * 11 Magha       30             January 21
 * 12 Phalguna    30             February 20

 * In leap years, Chaitra has 31 days and starts on March 21 instead.
 * The leap years of Gregorian calendar and Indian National Calendar are in synchornization.
 * So When its a leap year in Gregorian calendar then Chaitra has 31 days.
 *
 * The Years
 * Years are counted in the Saka Era, which starts its year 0 in 78AD (by gregorian calendar).
 * So for eg. 9th June 2006 by Gregorian Calendar, is same as 19th of Jyaistha in 1928 of Saka
 * era by Indian National Calendar.
 * <p>
 * The Indian Calendar has only one allowable era: <code>Saka Era</code>.  If the
 * calendar is not in lenient mode (see <code>setLenient</code>), dates before
 * 1/1/1 Saka Era are rejected with an <code>IllegalArgumentException</code>.
 * <p>
 * @internal
 */


class U_I18N_API IndianCalendar : public Calendar {
public:
  /**
   * Useful constants for IndianCalendar.
   * @internal
   */
  enum EEras {
    /** 
     * Constant for Chaitra, the 1st month of the Indian year. 
     */
      CHAITRA,

      /**
     * Constant for Vaisakha, the 2nd month of the Indian year. 
     */
      VAISAKHA,

      /**
     * Constant for Jyaistha, the 3rd month of the Indian year. 
     */
      JYAISTHA,

    /**
     * Constant for Asadha, the 4th month of the Indian year. 
     */
      ASADHA,

    /**
     * Constant for Sravana, the 5th month of the Indian year. 
     */
      SRAVANA,

    /**
     * Constant for Bhadra the 6th month of the Indian year
     */
      BHADRA,

    /** 
     * Constant for the Asvina, the 7th month of the Indian year. 
     */
      ASVINA,

    /**
     * Constant for Kartika, the 8th month of the Indian year. 
     */
      KARTIKA,

    /**
     * Constant for Agrahayana, the 9th month of the Indian year. 
     */
      AGRAHAYANA,

    /**
     * Constant for Pausa, the 10th month of the Indian year. 
     */
      PAUSA,

    /**
     * Constant for Magha, the 11th month of the Indian year. 
     */
      MAGHA,

    /**
     * Constant for Phalguna, the 12th month of the Indian year. 
     */
      PHALGUNA
    };

  //-------------------------------------------------------------------------
  // Constructors...
  //-------------------------------------------------------------------------

  /**
   * Constructs an IndianCalendar based on the current time in the default time zone
   * with the given locale.
   *
   * @param aLocale  The given locale.
   * @param success  Indicates the status of IndianCalendar object construction.
   *                 Returns U_ZERO_ERROR if constructed successfully.
   * @param beCivil  Whether the calendar should be civil (default-true) or religious (false)
   * @internal
   */
  IndianCalendar(const Locale& aLocale, UErrorCode &success);

  /**
   * Copy Constructor
   * @internal
   */
  IndianCalendar(const IndianCalendar& other);

  /**
   * Destructor.
   * @internal
   */
  virtual ~IndianCalendar();

  /**
   * Determines whether this object uses the fixed-cycle Indian civil calendar
   * or an approximation of the religious, astronomical calendar.
   *
   * @param beCivil   <code>CIVIL</code> to use the civil calendar,
   *                  <code>ASTRONOMICAL</code> to use the astronomical calendar.
   * @internal
   */
  //void setCivil(ECivil beCivil, UErrorCode &status);
    
  /**
   * Returns <code>true</code> if this object is using the fixed-cycle civil
   * calendar, or <code>false</code> if using the religious, astronomical
   * calendar.
   * @internal
   */
  //UBool isCivil();


  // TODO: copy c'tor, etc

  // clone
  virtual IndianCalendar* clone() const override;

 private:
  /**
   * Determine whether a year is the gregorian year a leap year 
   */
  //static UBool isGregorianLeap(int32_t year);
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
   * @param year  The year in Saka era
   * @param year  The month(0-based) in Indian year
   * @internal
   */
  virtual int32_t handleGetMonthLength(int32_t extendedYear, int32_t month, UErrorCode& status) const override;
  
  /**
   * Return the number of days in the given Indian year
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
   * Override Calendar to compute several fields specific to the Indian
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
  static UClassID U_EXPORT2 getStaticClassID();

  /**
   * return the calendar type, "indian".
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
  IndianCalendar() = delete; // default constructor not implemented

  // Default century.
protected:

  DECLARE_OVERRIDE_SYSTEM_DEFAULT_CENTURY
};

U_NAMESPACE_END

#endif
#endif



