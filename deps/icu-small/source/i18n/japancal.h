// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 ********************************************************************************
 * Copyright (C) 2003-2008, International Business Machines Corporation
 * and others. All Rights Reserved.
 ********************************************************************************
 *
 * File JAPANCAL.H
 *
 * Modification History:
 *
 *   Date        Name        Description
 *   05/13/2003  srl         copied from gregocal.h
 ********************************************************************************
 */

#ifndef JAPANCAL_H
#define JAPANCAL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"
#include "unicode/gregocal.h"

U_NAMESPACE_BEGIN

/**
 * Concrete class which provides the Japanese calendar.
 * <P>
 * <code>JapaneseCalendar</code> is a subclass of <code>GregorianCalendar</code>
 * that numbers years and eras based on the reigns of the Japanese emperors.
 * The Japanese calendar is identical to the Gregorian calendar in all respects
 * except for the year and era.  The ascension of each  emperor to the throne
 * begins a new era, and the years of that era are numbered starting with the
 * year of ascension as year 1.
 * <p>
 * Note that in the year of an imperial ascension, there are two possible sets
 * of year and era values: that for the old era and for the new.  For example, a
 * new era began on January 7, 1989 AD.  Strictly speaking, the first six days
 * of that year were in the Showa era, e.g. "January 6, 64 Showa", while the rest
 * of the year was in the Heisei era, e.g. "January 7, 1 Heisei".  This class
 * handles this distinction correctly when computing dates.  However, in lenient
 * mode either form of date is acceptable as input. 
 * <p>
 * In modern times, eras have started on January 8, 1868 AD, Gregorian (Meiji),
 * July 30, 1912 (Taisho), December 25, 1926 (Showa), and January 7, 1989 (Heisei).  Constants
 * for these eras, suitable for use in the <code>UCAL_ERA</code> field, are provided
 * in this class.  Note that the <em>number</em> used for each era is more or
 * less arbitrary.  Currently, the era starting in 645 AD is era #0; however this
 * may change in the future.  Use the predefined constants rather than using actual,
 * absolute numbers.
 * <p>
 * Since ICU4C 63, start date of each era is imported from CLDR. CLDR era data
 * may contain tentative era in near future with placeholder names. By default,
 * such era data is not enabled. ICU4C users who want to test the behavior of
 * the future era can enable this one of following settings (in the priority
 * order):
 * <ol>
 * <li>Environment variable <code>ICU_ENABLE_TENTATIVE_ERA=true</code>.</li>
 * </nl>
 * @internal
 */
class JapaneseCalendar : public GregorianCalendar {
public:

    /**
     * Check environment variable. 
     * @internal
     */
    U_I18N_API static UBool U_EXPORT2 enableTentativeEra();

    /**
     * Useful constants for JapaneseCalendar.
     * Exported for use by test code.
     * @internal
     */
    U_I18N_API static uint32_t U_EXPORT2 getCurrentEra(); // the current era

    /**
     * Constructs a JapaneseCalendar based on the current time in the default time zone
     * with the given locale.
     *
     * @param aLocale  The given locale.
     * @param success  Indicates the status of JapaneseCalendar object construction.
     *                 Returns U_ZERO_ERROR if constructed successfully.
     * @stable ICU 2.0
     */
    JapaneseCalendar(const Locale& aLocale, UErrorCode& success);


    /**
     * Destructor
     * @internal
     */
    virtual ~JapaneseCalendar();

    /**
     * Copy constructor
     * @param source    the object to be copied.
     * @internal
     */
    JapaneseCalendar(const JapaneseCalendar& source);

    /**
     * Default assignment operator
     * @param right    the object to be copied.
     * @internal
     */
    JapaneseCalendar& operator=(const JapaneseCalendar& right);

    /**
     * Create and return a polymorphic copy of this calendar.
     * @return    return a polymorphic copy of this calendar.
     * @internal
     */
    virtual JapaneseCalendar* clone() const override;

    /**
     * Return the extended year defined by the current fields.  In the 
     * Japanese calendar case, this is equal to the equivalent extended Gregorian year.
     * @internal
     */
    virtual int32_t handleGetExtendedYear(UErrorCode& status) override;

    /**
     * Return the maximum value that this field could have, given the current date.
     * @internal
     */
    virtual int32_t getActualMaximum(UCalendarDateFields field, UErrorCode& status) const override;


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
    U_I18N_API static UClassID U_EXPORT2 getStaticClassID();

    /**
     * return the calendar type, "japanese".
     *
     * @return calendar type
     * @internal
     */
    virtual const char * getType() const override;

    DECLARE_OVERRIDE_SYSTEM_DEFAULT_CENTURY

private:
    JapaneseCalendar(); // default constructor not implemented

protected:
    /** 
     * Calculate the era for internal computation
     * @internal
     */
    virtual int32_t internalGetEra() const override;

    /**
     * Compute fields from the JD
     * @internal
     */
    virtual void handleComputeFields(int32_t julianDay, UErrorCode& status) override;

    /**
     * Calculate the limit for a specified type of limit and field
     * @internal
     */
    virtual int32_t handleGetLimit(UCalendarDateFields field, ELimitType limitType) const override;

    /***
     * Called by computeJulianDay.  Returns the default month (0-based) for the year,
     * taking year and era into account.  Will return the first month of the given era, if 
     * the current year is an ascension year.
     * @param eyear the extended year
     * @param status Indicates the status.
     * @internal
     */
    virtual int32_t getDefaultMonthInYear(int32_t eyear, UErrorCode& status) override;

    /***
     * Called by computeJulianDay.  Returns the default day (1-based) for the month,
     * taking currently-set year and era into account.  Will return the first day of the given
     * era, if the current month is an ascension year and month.
     * @param eyear the extended year
     * @param mon the month in the year
     * @param status Indicates the status.
     * @internal
     */
    virtual int32_t getDefaultDayInMonth(int32_t eyear, int32_t month, UErrorCode& status) override;

    virtual bool isEra0CountingBackward() const override { return false; }
};

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif
//eof

