// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 *******************************************************************************
 * Copyright (C) 2003-2013, International Business Machines Corporation and
 * others. All Rights Reserved.
 *******************************************************************************
 *
 * File TAIWNCAL.CPP
 *
 * Modification History:
 *  05/13/2003    srl     copied from gregocal.cpp
 *  06/29/2007    srl     copied from buddhcal.cpp
 *  05/12/2008    jce     modified to use calendar=roc per CLDR
 *
 */

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "taiwncal.h"
#include "gregoimp.h"
#include "unicode/gregocal.h"
#include "umutex.h"
#include <float.h>

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(TaiwanCalendar)

static const int32_t kTaiwanEraStart = 1911;  // 1911 (Gregorian)

static const int32_t kGregorianEpoch = 1970;

TaiwanCalendar::TaiwanCalendar(const Locale& aLocale, UErrorCode& success)
:   GregorianCalendar(aLocale, success)
{
    setTimeInMillis(getNow(), success); // Call this again now that the vtable is set up properly.
}

TaiwanCalendar::~TaiwanCalendar()
{
}

TaiwanCalendar::TaiwanCalendar(const TaiwanCalendar& source)
: GregorianCalendar(source)
{
}

TaiwanCalendar& TaiwanCalendar::operator= ( const TaiwanCalendar& right)
{
    GregorianCalendar::operator=(right);
    return *this;
}

TaiwanCalendar* TaiwanCalendar::clone() const
{
    return new TaiwanCalendar(*this);
}

const char *TaiwanCalendar::getType() const
{
    return "roc";
}

int32_t TaiwanCalendar::handleGetExtendedYear(UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return 0;
    }

    // EXTENDED_YEAR in TaiwanCalendar is a Gregorian year
    // The default value of EXTENDED_YEAR is 1970 (Minguo 59)
    if (newerField(UCAL_EXTENDED_YEAR, UCAL_YEAR) == UCAL_EXTENDED_YEAR
        && newerField(UCAL_EXTENDED_YEAR, UCAL_ERA) == UCAL_EXTENDED_YEAR) {
        return internalGet(UCAL_EXTENDED_YEAR, kGregorianEpoch);
    }
    int32_t era = internalGet(UCAL_ERA, MINGUO);
    int32_t year = internalGet(UCAL_YEAR, 1);
    switch (era) {
        case MINGUO:
            if (uprv_add32_overflow(year, kTaiwanEraStart, &year)) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return 0;
            }
            return year;
        case BEFORE_MINGUO:
            if (uprv_add32_overflow(1 + kTaiwanEraStart, -year, &year)) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return 0;
            }
            return year;
        default:
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return 0;
    }
}

void TaiwanCalendar::handleComputeFields(int32_t julianDay, UErrorCode& status)
{
    GregorianCalendar::handleComputeFields(julianDay, status);
    int32_t y = internalGet(UCAL_EXTENDED_YEAR) - kTaiwanEraStart;
    if(y>0) {
        internalSet(UCAL_ERA, MINGUO);
        internalSet(UCAL_YEAR, y);
    } else {
        internalSet(UCAL_ERA, BEFORE_MINGUO);
        internalSet(UCAL_YEAR, 1-y);
    }
}

int32_t TaiwanCalendar::handleGetLimit(UCalendarDateFields field, ELimitType limitType) const
{
    if(field != UCAL_ERA) {
        return GregorianCalendar::handleGetLimit(field,limitType);
    }
    if (limitType == UCAL_LIMIT_MINIMUM || limitType == UCAL_LIMIT_GREATEST_MINIMUM) {
        return BEFORE_MINGUO;
    }
    return MINGUO;
}

IMPL_SYSTEM_DEFAULT_CENTURY(TaiwanCalendar, "@calendar=roc")

U_NAMESPACE_END

#endif
