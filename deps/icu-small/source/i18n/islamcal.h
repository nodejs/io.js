// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 ********************************************************************************
 * Copyright (C) 2003-2013, International Business Machines Corporation
 * and others. All Rights Reserved.
 ******************************************************************************
 *
 * File ISLAMCAL.H
 *
 * Modification History:
 *
 *   Date        Name        Description
 *   10/14/2003  srl         ported from java IslamicCalendar
 *****************************************************************************
 */

#ifndef ISLAMCAL_H
#define ISLAMCAL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"

U_NAMESPACE_BEGIN

/**
 * <code>IslamicCalendar</code> is a subclass of <code>Calendar</code>
 * that implements the Islamic civil and religious calendars.  It
 * is used as the civil calendar in most of the Arab world and the
 * liturgical calendar of the Islamic faith worldwide.  This calendar
 * is also known as the "Hijri" calendar, since it starts at the time
 * of Mohammed's emigration (or "hijra") to Medinah on Thursday, 
 * July 15, 622 AD (Julian).
 * <p>
 * The Islamic calendar is strictly lunar, and thus an Islamic year of twelve
 * lunar months does not correspond to the solar year used by most other
 * calendar systems, including the Gregorian.  An Islamic year is, on average,
 * about 354 days long, so each successive Islamic year starts about 11 days
 * earlier in the corresponding Gregorian year.
 * <p>
 * Each month of the calendar starts when the new moon's crescent is visible
 * at sunset.  However, in order to keep the time fields in this class
 * synchronized with those of the other calendars and with local clock time,
 * we treat days and months as beginning at midnight,
 * roughly 6 hours after the corresponding sunset.
 * <p>
 * There are two main variants of the Islamic calendar in existence.  The first
 * is the <em>civil</em> calendar, which uses a fixed cycle of alternating 29-
 * and 30-day months, with a leap day added to the last month of 11 out of
 * every 30 years.  This calendar is easily calculated and thus predictable in
 * advance, so it is used as the civil calendar in a number of Arab countries.
 * This is the default behavior of a newly-created <code>IslamicCalendar</code>
 * object. This calendar variant is implemented in the IslamicCivilCalendar
 * class.
 * <p>
 * The Islamic <em>religious</em> calendar, however, is based on the <em>observation</em>
 * of the crescent moon.  It is thus affected by the position at which the
 * observations are made, seasonal variations in the time of sunset, the
 * eccentricities of the moon's orbit, and even the weather at the observation
 * site.  This makes it impossible to calculate in advance, and it causes the
 * start of a month in the religious calendar to differ from the civil calendar
 * by up to three days.
 * <p>
 * Using astronomical calculations for the position of the sun and moon, the
 * moon's illumination, and other factors, it is possible to determine the start
 * of a lunar month with a fairly high degree of certainty.  However, these
 * calculations are extremely complicated and thus slow, so most algorithms,
 * including the one used here, are only approximations of the true astronomical
 * calculations.  At present, the approximations used in this class are fairly
 * simplistic; they will be improved in later versions of the code.
 * <p>
 *
 * @see GregorianCalendar
 *
 * @author Laura Werner
 * @author Alan Liu
 * @author Steven R. Loomis
 * @internal
 */
class U_I18N_API IslamicCalendar : public Calendar {
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
     * Constant for Muharram, the 1st month of the Islamic year. 
     * @internal
     */
    MUHARRAM = 0,

    /**
     * Constant for Safar, the 2nd month of the Islamic year. 
     * @internal
     */
    SAFAR = 1,

    /**
     * Constant for Rabi' al-awwal (or Rabi' I), the 3rd month of the Islamic year. 
     * @internal 
     */
    RABI_1 = 2,

    /**
     * Constant for Rabi' al-thani or (Rabi' II), the 4th month of the Islamic year. 
     * @internal 
     */
    RABI_2 = 3,

    /**
     * Constant for Jumada al-awwal or (Jumada I), the 5th month of the Islamic year. 
     * @internal 
     */
    JUMADA_1 = 4,

    /**
     * Constant for Jumada al-thani or (Jumada II), the 6th month of the Islamic year. 
     * @internal 
     */
    JUMADA_2 = 5,

    /**
     * Constant for Rajab, the 7th month of the Islamic year. 
     * @internal 
     */
    RAJAB = 6,

    /**
     * Constant for Sha'ban, the 8th month of the Islamic year. 
     * @internal 
     */
    SHABAN = 7,

    /**
     * Constant for Ramadan, the 9th month of the Islamic year. 
     * @internal 
     */
    RAMADAN = 8,

    /**
     * Constant for Shawwal, the 10th month of the Islamic year. 
     * @internal 
     */
    SHAWWAL = 9,

    /**
     * Constant for Dhu al-Qi'dah, the 11th month of the Islamic year. 
     * @internal 
     */
    DHU_AL_QIDAH = 10,

    /**
     * Constant for Dhu al-Hijjah, the 12th month of the Islamic year. 
     * @internal 
     */
    DHU_AL_HIJJAH = 11,
    
    ISLAMIC_MONTH_MAX
  }; 


  //-------------------------------------------------------------------------
  // Constructors...
  //-------------------------------------------------------------------------

  /**
   * Constructs an IslamicCalendar based on the current time in the default time zone
   * with the given locale.
   *
   * @param aLocale  The given locale.
   * @param success  Indicates the status of IslamicCalendar object construction.
   *                 Returns U_ZERO_ERROR if constructed successfully.
   * @internal
   */
  IslamicCalendar(const Locale& aLocale, UErrorCode &success);

  /**
   * Copy Constructor
   * @internal
   */
  IslamicCalendar(const IslamicCalendar& other) = default;

  /**
   * Destructor.
   * @internal
   */
  virtual ~IslamicCalendar();

  // clone
  virtual IslamicCalendar* clone() const override;

 protected:
  /**
   * Return the day # on which the given year starts.  Days are counted
   * from the Hijri epoch, origin 0.
   */
  virtual int64_t yearStart(int32_t year, UErrorCode& status) const;

  /**
   * Return the day # on which the given month starts.  Days are counted
   * from the Hijri epoch, origin 0.
   *
   * @param year  The hijri year
   * @param year  The hijri month, 0-based
   */
  virtual int64_t monthStart(int32_t year, int32_t month, UErrorCode& status) const;


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
   * @param year  The hijri year
   * @param year  The hijri month, 0-based
   * @internal
   */
  virtual int32_t handleGetMonthLength(int32_t extendedYear, int32_t month, UErrorCode& status) const override;
  
  /**
   * Return the number of days in the given Islamic year
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
   * Override Calendar to compute several fields specific to the Islamic
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

  /**
   * Return the epoc.
   * @internal
   */
  virtual int32_t getEpoc() const;

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
  /*U_I18N_API*/ static UClassID U_EXPORT2 getStaticClassID();

  /**
   * return the calendar type, "islamic".
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

  /**
   * Returns true if the date is in a leap year.
   *
   * @param status        ICU Error Code
   * @return       True if the date in the fields is in a Temporal proposal
   *               defined leap year. False otherwise.
   */
  virtual bool inTemporalLeapYear(UErrorCode &status) const override;

 private:
  IslamicCalendar() = delete; // default constructor not implemented

  // Default century.
 protected:

  DECLARE_OVERRIDE_SYSTEM_DEFAULT_CENTURY
};

/*
 * IslamicCivilCalendar is one of the two main variants of the Islamic calendar.
 * The <em>civil</em> calendar, which uses a fixed cycle of alternating 29-
 * and 30-day months, with a leap day added to the last month of 11 out of
 * every 30 years.  This calendar is easily calculated and thus predictable in
 * advance, so it is used as the civil calendar in a number of Arab countries.
 * This calendar is referring as "Islamic calendar, tabular (intercalary years
 * [2,5,7,10,13,16,18,21,24,26,29]- civil epoch" in CLDR.
 */
class U_I18N_API IslamicCivilCalendar : public IslamicCalendar {
 public:
  /**
   * Constructs an IslamicCivilCalendar based on the current time in the default time zone
   * with the given locale.
   *
   * @param aLocale  The given locale.
   * @param success  Indicates the status of IslamicCivilCalendar object construction.
   *                 Returns U_ZERO_ERROR if constructed successfully.
   * @internal
   */
  IslamicCivilCalendar(const Locale& aLocale, UErrorCode &success);

  /**
   * Copy Constructor
   * @internal
   */
  IslamicCivilCalendar(const IslamicCivilCalendar& other) = default;

  /**
   * Destructor.
   * @internal
   */
  virtual ~IslamicCivilCalendar();

  // clone
  virtual IslamicCivilCalendar* clone() const override;

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
   * return the calendar type, "islamic-civil".
   *
   * @return calendar type
   * @internal
   */
  virtual const char * getType() const override;

 protected:
  /**
   * Return the day # on which the given year starts.  Days are counted
   * from the Hijri epoch, origin 0.
   * @internal
   */
  virtual int64_t yearStart(int32_t year, UErrorCode& status) const override;

  /**
   * Return the day # on which the given month starts.  Days are counted
   * from the Hijri epoch, origin 0.
   *
   * @param year  The hijri year
   * @param year  The hijri month, 0-based
   * @internal
   */
  virtual int64_t monthStart(int32_t year, int32_t month, UErrorCode& status) const override;

  /**
   * Return the length (in days) of the given month.
   *
   * @param year  The hijri year
   * @param year  The hijri month, 0-based
   * @internal
   */
  virtual int32_t handleGetMonthLength(int32_t extendedYear, int32_t month, UErrorCode& status) const override;

  /**
   * Return the number of days in the given Islamic year
   * @internal
   */
  virtual int32_t handleGetYearLength(int32_t extendedYear) const override;

  /**
   * Override Calendar to compute several fields specific to the Islamic
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
};

/*
 * IslamicTBLACalendar calendar.
 * This is a subclass of IslamicCivilCalendar. The only differences in the
 * calendar math is it uses different epoch.
 * This calendar is referring as "Islamic calendar, tabular (intercalary years
 * [2,5,7,10,13,16,18,21,24,26,29] - astronomical epoch" in CLDR.
 */
class U_I18N_API IslamicTBLACalendar : public IslamicCivilCalendar {
 public:
  /**
   * Constructs an IslamicTBLACalendar based on the current time in the default time zone
   * with the given locale.
   *
   * @param aLocale  The given locale.
   * @param success  Indicates the status of IslamicTBLACalendar object construction.
   *                 Returns U_ZERO_ERROR if constructed successfully.
   * @internal
   */
  IslamicTBLACalendar(const Locale& aLocale, UErrorCode &success);

  /**
   * Copy Constructor
   * @internal
   */
  IslamicTBLACalendar(const IslamicTBLACalendar& other) = default;

  /**
   * Destructor.
   * @internal
   */
  virtual ~IslamicTBLACalendar();

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
   * return the calendar type, "islamic-tbla".
   *
   * @return calendar type
   * @internal
   */
  virtual const char * getType() const override;

  // clone
  virtual IslamicTBLACalendar* clone() const override;

 protected:
  /**
   * Return the epoc.
   * @internal
   */
  virtual int32_t getEpoc() const override;
};

/*
 * IslamicUmalquraCalendar
 * This calendar is referred as "Islamic calendar, Umm al-Qura" in CLDR.
 */
class U_I18N_API IslamicUmalquraCalendar : public IslamicCivilCalendar {
 public:
  /**
   * Constructs an IslamicUmalquraCalendar based on the current time in the default time zone
   * with the given locale.
   *
   * @param aLocale  The given locale.
   * @param success  Indicates the status of IslamicUmalquraCalendar object construction.
   *                 Returns U_ZERO_ERROR if constructed successfully.
   * @internal
   */
  IslamicUmalquraCalendar(const Locale& aLocale, UErrorCode &success);

  /**
   * Copy Constructor
   * @internal
   */
  IslamicUmalquraCalendar(const IslamicUmalquraCalendar& other) = default;

  /**
   * Destructor.
   * @internal
   */
  virtual ~IslamicUmalquraCalendar();

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
   * return the calendar type, "islamic-umalqura".
   *
   * @return calendar type
   * @internal
   */
  virtual const char * getType() const override;

  // clone
  virtual IslamicUmalquraCalendar* clone() const override;

 protected:
  /**
   * Return the day # on which the given year starts.  Days are counted
   * from the Hijri epoch, origin 0.
   * @internal
   */
  virtual int64_t yearStart(int32_t year, UErrorCode& status) const override;

  /**
   * Return the day # on which the given month starts.  Days are counted
   * from the Hijri epoch, origin 0.
   *
   * @param year  The hijri year
   * @param year  The hijri month, 0-based
   * @internal
   */
  virtual int64_t monthStart(int32_t year, int32_t month, UErrorCode& status) const override;

  /**
   * Return the length (in days) of the given month.
   *
   * @param year  The hijri year
   * @param year  The hijri month, 0-based
   * @internal
   */
  virtual int32_t handleGetMonthLength(int32_t extendedYear, int32_t month, UErrorCode& status) const override;

  /**
   * Return the number of days in the given Islamic year
   * @internal
   */
  virtual int32_t handleGetYearLength(int32_t extendedYear) const override;

  /**
   * Override Calendar to compute several fields specific to the Islamic
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

 private:
  virtual int32_t yearLength(int32_t extendedYear, UErrorCode& status) const;
};


/*
 * IslamicRGSACalendar
 * Islamic calendar, Saudi Arabia sighting. Since the calendar depends on the
 * sighting, it is impossible to implement by algorithm ahead of time. It is
 * currently identical to IslamicCalendar except the getType will return
 * "islamic-rgsa".
 */
class U_I18N_API IslamicRGSACalendar : public IslamicCalendar {
 public:
  /**
   * Constructs an IslamicRGSACalendar based on the current time in the default time zone
   * with the given locale.
   *
   * @param aLocale  The given locale.
   * @param success  Indicates the status of IslamicRGSACalendar object construction.
   *                 Returns U_ZERO_ERROR if constructed successfully.
   * @internal
   */
  IslamicRGSACalendar(const Locale& aLocale, UErrorCode &success);

  /**
   * Copy Constructor
   * @internal
   */
  IslamicRGSACalendar(const IslamicRGSACalendar& other) = default;

  /**
   * Destructor.
   * @internal
   */
  virtual ~IslamicRGSACalendar();

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
   * return the calendar type, "islamic-rgsa".
   *
   * @return calendar type
   * @internal
   */
  virtual const char * getType() const override;

  // clone
  virtual IslamicRGSACalendar* clone() const override;
};

U_NAMESPACE_END

#endif
#endif
