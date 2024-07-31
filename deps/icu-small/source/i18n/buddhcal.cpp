// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2003-2013, International Business Machines Corporation and    *
* others. All Rights Reserved.                                                *
*******************************************************************************
*
* File BUDDHCAL.CPP
*
* Modification History:
*  05/13/2003    srl     copied from gregocal.cpp
*
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "buddhcal.h"
#include "gregoimp.h"
#include "unicode/gregocal.h"
#include "umutex.h"
#include <float.h>

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(BuddhistCalendar)

//static const int32_t kMaxEra = 0; // only 1 era

static const int32_t kBuddhistEraStart = -543;  // 544 BC (Gregorian)

static const int32_t kGregorianEpoch = 1970;    // used as the default value of EXTENDED_YEAR

BuddhistCalendar::BuddhistCalendar(const Locale& aLocale, UErrorCode& success)
:   GregorianCalendar(aLocale, success)
{
    setTimeInMillis(getNow(), success); // Call this again now that the vtable is set up properly.
}

BuddhistCalendar::~BuddhistCalendar()
{
}

BuddhistCalendar::BuddhistCalendar(const BuddhistCalendar& source)
: GregorianCalendar(source)
{
}

BuddhistCalendar& BuddhistCalendar::operator= ( const BuddhistCalendar& right)
{
    GregorianCalendar::operator=(right);
    return *this;
}

BuddhistCalendar* BuddhistCalendar::clone() const
{
    return new BuddhistCalendar(*this);
}

const char *BuddhistCalendar::getType() const
{
    return "buddhist";
}

int32_t BuddhistCalendar::handleGetExtendedYear(UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return 0;
    }
    // EXTENDED_YEAR in BuddhistCalendar is a Gregorian year.
    // The default value of EXTENDED_YEAR is 1970 (Buddhist 2513)
    if (newerField(UCAL_EXTENDED_YEAR, UCAL_YEAR) == UCAL_EXTENDED_YEAR) {
        return internalGet(UCAL_EXTENDED_YEAR, kGregorianEpoch);
    }
    // extended year is a gregorian year, where 1 = 1AD,  0 = 1BC, -1 = 2BC, etc
    int32_t year = internalGet(UCAL_YEAR, kGregorianEpoch - kBuddhistEraStart);
    if (uprv_add32_overflow(year, kBuddhistEraStart, &year)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    return year;
}

void BuddhistCalendar::handleComputeFields(int32_t julianDay, UErrorCode& status)
{
    GregorianCalendar::handleComputeFields(julianDay, status);
    int32_t y = internalGet(UCAL_EXTENDED_YEAR) - kBuddhistEraStart;
    internalSet(UCAL_ERA, 0);
    internalSet(UCAL_YEAR, y);
}

int32_t BuddhistCalendar::handleGetLimit(UCalendarDateFields field, ELimitType limitType) const
{
    if(field == UCAL_ERA) {
        return BE;
    }
    return GregorianCalendar::handleGetLimit(field,limitType);
}

IMPL_SYSTEM_DEFAULT_CENTURY(BuddhistCalendar, "@calendar=buddhist")

U_NAMESPACE_END

#endif
