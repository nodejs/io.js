// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2003 - 2013, International Business Machines Corporation and  *
* others. All Rights Reserved.                                                *
*******************************************************************************
*/

#ifndef ETHPCCAL_H
#define ETHPCCAL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"
#include "cecal.h"

U_NAMESPACE_BEGIN

/**
 * Implement the Ethiopic calendar system.
 * @internal
 */
class EthiopicCalendar : public CECalendar {

public:
    /**
     * Useful constants for EthiopicCalendar.
     * @internal
     */
    enum EMonths {
        /** 
         * Constant for &#x1218;&#x1235;&#x12a8;&#x1228;&#x121d;, the 1st month of the Ethiopic year.
         */
        MESKEREM,

        /** 
         * Constant for &#x1325;&#x1245;&#x121d;&#x1275;, the 2nd month of the Ethiopic year.
         */
        TEKEMT,

        /** 
         * Constant for &#x1285;&#x12f3;&#x122d;, the 3rd month of the Ethiopic year. 
         */
        HEDAR,

        /** 
         * Constant for &#x1273;&#x1285;&#x1223;&#x1225;, the 4th month of the Ethiopic year. 
         */
        TAHSAS,

        /** 
         * Constant for &#x1325;&#x122d;, the 5th month of the Ethiopic year. 
         */
        TER,

        /** 
         * Constant for &#x12e8;&#x12ab;&#x1272;&#x1275;, the 6th month of the Ethiopic year. 
         */
        YEKATIT,

        /** 
         * Constant for &#x1218;&#x130b;&#x1262;&#x1275;, the 7th month of the Ethiopic year. 
         */
        MEGABIT,

        /** 
         * Constant for &#x121a;&#x12eb;&#x12dd;&#x12eb;, the 8th month of the Ethiopic year. 
         */
        MIAZIA,

        /** 
         * Constant for &#x130d;&#x1295;&#x1266;&#x1275;, the 9th month of the Ethiopic year. 
         */
        GENBOT,

        /** 
         * Constant for &#x1230;&#x1294;, the 10th month of the Ethiopic year. 
         */
        SENE,

        /** 
         * Constant for &#x1210;&#x121d;&#x120c;, the 11th month of the Ethiopic year. 
         */
        HAMLE,

        /** 
         * Constant for &#x1290;&#x1210;&#x1234;, the 12th month of the Ethiopic year. 
         */
        NEHASSE,

        /** 
         * Constant for &#x1333;&#x1309;&#x121c;&#x1295;, the 13th month of the Ethiopic year. 
         */
        PAGUMEN
    };

    enum EEras {
        AMETE_ALEM,     // Before the epoch
        AMETE_MIHRET    // After the epoch
    };

    /**
     * Constructs a EthiopicCalendar based on the current time in the default time zone
     * with the given locale.
     *
     * @param aLocale  The given locale.
     * @param success  Indicates the status of EthiopicCalendar object construction.
     *                 Returns U_ZERO_ERROR if constructed successfully.
     * @param type     Whether this Ethiopic calendar use Amete Mihrret (default) or
     *                 only use Amete Alem for all the time.
     * @internal
     */
    EthiopicCalendar(const Locale& aLocale, UErrorCode& success);

    /**
     * Copy Constructor
     * @internal
     */
    EthiopicCalendar(const EthiopicCalendar& other) = default;

    /**
     * Destructor.
     * @internal
     */
    virtual ~EthiopicCalendar();

    /**
     * Create and return a polymorphic copy of this calendar.
     * @return    return a polymorphic copy of this calendar.
     * @internal
     */
    virtual EthiopicCalendar* clone() const override;

    /**
     * Return the calendar type, "ethiopic"
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

protected:
    //-------------------------------------------------------------------------
    // Calendar framework
    //-------------------------------------------------------------------------

    /**
     * Return the extended year defined by the current fields.
     * This calendar uses both AMETE_ALEM and AMETE_MIHRET.
     *
     * EXTENDED_YEAR       ERA           YEAR
     *             0       AMETE_ALEM    5500
     *             1       AMETE_MIHRET     1
     * @internal
     */
    virtual int32_t handleGetExtendedYear(UErrorCode& status) override;

    /**
     * Compute fields from the JD
     * @internal
     */
    virtual void handleComputeFields(int32_t julianDay, UErrorCode &status) override;

    DECLARE_OVERRIDE_SYSTEM_DEFAULT_CENTURY

    /**
     * Return the date offset from Julian
     * @internal
     */
    virtual int32_t getJDEpochOffset() const override;

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

#if 0
// We do not want to introduce this API in ICU4C.
// It was accidentally introduced in ICU4J as a public API.

public:
    //-------------------------------------------------------------------------
    // Calendar system Conversion methods...
    //-------------------------------------------------------------------------

    /**
     * Convert an Ethiopic year, month, and day to a Julian day.
     *
     * @param year the extended year
     * @param month the month
     * @param day the day
     * @return Julian day
     * @internal
     */
    int32_t ethiopicToJD(int32_t year, int32_t month, int32_t day);
#endif
};

/**
 * Implement the Ethiopic Amete Alem calendar system.
 * @internal
 */
class EthiopicAmeteAlemCalendar : public EthiopicCalendar {

public:
    /**
     * Constructs a EthiopicAmeteAlemCalendar based on the current time in the default time zone
     * with the given locale.
     *
     * @param aLocale  The given locale.
     * @param success  Indicates the status of EthiopicCalendar object construction.
     *                 Returns U_ZERO_ERROR if constructed successfully.
     * @internal
     */
    EthiopicAmeteAlemCalendar(const Locale& aLocale, UErrorCode& success);

    /**
     * Copy Constructor
     * @internal
     */
    EthiopicAmeteAlemCalendar(const EthiopicAmeteAlemCalendar& other) = default;

    /**
     * Destructor.
     * @internal
     */
    virtual ~EthiopicAmeteAlemCalendar();

    /**
     * Create and return a polymorphic copy of this calendar.
     * @return    return a polymorphic copy of this calendar.
     * @internal
     */
    virtual EthiopicAmeteAlemCalendar* clone() const override;

    /**
     * Return the calendar type, "ethiopic-amete-alem"
     * @return calendar type
     * @internal
     */
    virtual const char * getType() const override;

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
    //-------------------------------------------------------------------------
    // Calendar framework
    //-------------------------------------------------------------------------

    /**
     * Return the extended year defined by the current fields.
     * This calendar use only AMETE_ALEM for the era.
     *
     * EXTENDED_YEAR       ERA         YEAR
     *             0       AMETE_ALEM  5500
     *             1       AMETE_ALEM  5501
     * @internal
     */
    virtual int32_t handleGetExtendedYear(UErrorCode& status) override;

    /**
     * Compute fields from the JD
     * @internal
     */
    virtual void handleComputeFields(int32_t julianDay, UErrorCode &status) override;

    /**
     * Calculate the limit for a specified type of limit and field
     * @internal
     */
    virtual int32_t handleGetLimit(UCalendarDateFields field, ELimitType limitType) const override;
    /**
     * Returns the year in which the default century begins
     * @internal
     */
    virtual int32_t defaultCenturyStartYear() const override;
};

U_NAMESPACE_END
#endif /* #if !UCONFIG_NO_FORMATTING */
#endif /* ETHPCCAL_H */
//eof
