// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2003 - 2013, International Business Machines Corporation and  *
* others. All Rights Reserved.                                                *
*******************************************************************************
*/

#ifndef COPTCCAL_H
#define COPTCCAL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"
#include "cecal.h"

U_NAMESPACE_BEGIN

/**
 * Implement the Coptic calendar system.
 * @internal
 */
class CopticCalendar : public CECalendar {
  
public:
    /**
     * Useful constants for CopticCalendar.
     * @internal
     */
    enum EMonths {
        /** 
         * Constant for &#x03c9;&#x03bf;&#x03b3;&#x03c4;/&#x062a;&#xfeee;&#xfe97;,
         * the 1st month of the Coptic year. 
         */
        TOUT,
        
        /** 
         * Constant for &#x03a0;&#x03b1;&#x03bf;&#x03c0;&#x03b9;/&#xfeea;&#xfe91;&#xfe8e;&#xfe91;,
         * the 2nd month of the Coptic year. 
         */
        BABA,

        /** 
         * Constant for &#x0391;&#x03b8;&#x03bf;&#x03c1;/&#x0631;&#xfeee;&#xfe97;&#xfe8e;&#xfeeb;,
         * the 3rd month of the Coptic year. 
         */
        HATOR,

        /** 
         * Constant for &#x03a7;&#x03bf;&#x03b9;&#x03b1;&#x03ba;/&#xfeda;&#xfeec;&#xfef4;&#xfedb;,
         * the 4th month of the Coptic year. 
         */
        KIAHK,

        /** 
         * Constant for &#x03a4;&#x03c9;&#x03b2;&#x03b9;/&#x0637;&#xfeee;&#xfe92;&#xfeeb;,
         * the 5th month of the Coptic year. 
         */
        TOBA,

        /** 
         * Constant for &#x039c;&#x03b5;&#x03e3;&#x03b9;&#x03c1;/&#xfeae;&#xfef4;&#xfeb8;&#xfee3;&#x0623;,
         * the 6th month of the Coptic year. 
         */
        AMSHIR,

        /** 
         * Constant for &#x03a0;&#x03b1;&#x03c1;&#x03b5;&#x03bc;&#x03e9;&#x03b1;&#x03c4;/&#x062a;&#xfe8e;&#xfeec;&#xfee3;&#xfeae;&#xfe91;,
         * the 7th month of the Coptic year. 
         */
        BARAMHAT,

        /** 
         * Constant for &#x03a6;&#x03b1;&#x03c1;&#x03bc;&#x03bf;&#x03b8;&#x03b9;/&#x0647;&#x062f;&#xfeee;&#xfee3;&#xfeae;&#xfe91;, 
         * the 8th month of the Coptic year. 
         */
        BARAMOUDA,

        /** 
         * Constant for &#x03a0;&#x03b1;&#x03e3;&#x03b1;&#x03bd;/&#xfeb2;&#xfee8;&#xfeb8;&#xfe91;,
         * the 9th month of the Coptic year. 
         */
        BASHANS,

        /** 
         * Constant for &#x03a0;&#x03b1;&#x03c9;&#x03bd;&#x03b9;/&#xfeea;&#xfee7;&#x0624;&#xfeee;&#xfe91;,
         * the 10th month of the Coptic year. 
         */
        PAONA,

        /** 
         * Constant for &#x0395;&#x03c0;&#x03b7;&#x03c0;/&#xfe90;&#xfef4;&#xfe91;&#x0623;,
         * the 11th month of the Coptic year. 
         */
        EPEP,

        /** 
         * Constant for &#x039c;&#x03b5;&#x03f2;&#x03c9;&#x03c1;&#x03b7;/&#x0649;&#xfeae;&#xfeb4;&#xfee3;,
         * the 12th month of the Coptic year. 
         */
        MESRA,

        /** 
         * Constant for &#x03a0;&#x03b9;&#x03ba;&#x03bf;&#x03b3;&#x03eb;&#x03b9;
         * &#x03bc;&#x03b1;&#x03b2;&#x03bf;&#x03c4;/&#xfeae;&#xfef4;&#xfed0;&#xfebc;&#xfedf;&#x0627;
         * &#xfeae;&#xfeec;&#xfeb8;&#xfedf;&#x0627;,
         * the 13th month of the Coptic year. 
         */
        NASIE
    };

    enum EEras {
        BCE,    // Before the epoch
        CE      // After the epoch
    };

    /**
     * Constructs a CopticCalendar based on the current time in the default time zone
     * with the given locale.
     *
     * @param aLocale  The given locale.
     * @param success  Indicates the status of CopticCalendar object construction.
     *                 Returns U_ZERO_ERROR if constructed successfully.
     * @internal
     */
    CopticCalendar(const Locale& aLocale, UErrorCode& success);

    /**
     * Copy Constructor
     * @internal
     */
    CopticCalendar (const CopticCalendar& other);

    /**
     * Destructor.
     * @internal
     */
    virtual ~CopticCalendar();

    /**
     * Create and return a polymorphic copy of this calendar.
     * @return    return a polymorphic copy of this calendar.
     * @internal
     */
    virtual CopticCalendar* clone() const override;

    /**
     * return the calendar type, "coptic"
     * @return calendar type
     * @internal
     */
    const char * getType() const override;

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

    virtual bool isEra0CountingBackward() const override { return true; }
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
     * Convert an Coptic year, month, and day to a Julian day.
     *
     * @param year the extended year
     * @param month the month
     * @param day the day
     * @return Julian day
     * @internal
     */
    static int32_t copticToJD(int32_t year, int32_t month, int32_t day);
#endif
};

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
#endif /* COPTCCAL_H */
//eof
