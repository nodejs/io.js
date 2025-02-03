// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2003 - 2013, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "gregoimp.h"
#include "umutex.h"
#include "coptccal.h"
#include "cecal.h"
#include <float.h>

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(CopticCalendar)

static const int32_t COPTIC_JD_EPOCH_OFFSET  = 1824665;

//-------------------------------------------------------------------------
// Constructors...
//-------------------------------------------------------------------------

CopticCalendar::CopticCalendar(const Locale& aLocale, UErrorCode& success)
: CECalendar(aLocale, success)
{
}

CopticCalendar::CopticCalendar (const CopticCalendar& other) 
: CECalendar(other)
{
}

CopticCalendar::~CopticCalendar()
{
}

CopticCalendar*
CopticCalendar::clone() const
{
    return new CopticCalendar(*this);
}

const char*
CopticCalendar::getType() const
{
    return "coptic";
}

//-------------------------------------------------------------------------
// Calendar framework
//-------------------------------------------------------------------------

int32_t
CopticCalendar::handleGetExtendedYear(UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return 0;
    }
    if (newerField(UCAL_EXTENDED_YEAR, UCAL_YEAR) == UCAL_EXTENDED_YEAR) {
        return internalGet(UCAL_EXTENDED_YEAR, 1); // Default to year 1
    }
    // The year defaults to the epoch start, the era to CE
    int32_t era = internalGet(UCAL_ERA, CE);
    if (era == BCE) {
        return 1 - internalGet(UCAL_YEAR, 1); // Convert to extended year
    }
    if (era == CE){
        return internalGet(UCAL_YEAR, 1); // Default to year 1
    }
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return 0;
}

void
CopticCalendar::handleComputeFields(int32_t julianDay, UErrorCode& status)
{
    int32_t eyear, month, day, era, year;
    jdToCE(julianDay, getJDEpochOffset(), eyear, month, day, status);
    if (U_FAILURE(status)) return;

    if (eyear <= 0) {
        era = BCE;
        year = 1 - eyear;
    } else {
        era = CE;
        year = eyear;
    }

    internalSet(UCAL_EXTENDED_YEAR, eyear);
    internalSet(UCAL_ERA, era);
    internalSet(UCAL_YEAR, year);
    internalSet(UCAL_MONTH, month);
    internalSet(UCAL_ORDINAL_MONTH, month);
    internalSet(UCAL_DATE, day);
    internalSet(UCAL_DAY_OF_YEAR, (30 * month) + day);
}

constexpr uint32_t kCopticRelatedYearDiff = 284;

int32_t CopticCalendar::getRelatedYear(UErrorCode &status) const
{
    int32_t year = get(UCAL_EXTENDED_YEAR, status);
    if (U_FAILURE(status)) {
        return 0;
    }
    return year + kCopticRelatedYearDiff;
}

void CopticCalendar::setRelatedYear(int32_t year)
{
    // set extended year
    set(UCAL_EXTENDED_YEAR, year - kCopticRelatedYearDiff);
}

IMPL_SYSTEM_DEFAULT_CENTURY(CopticCalendar, "@calendar=coptic")

int32_t
CopticCalendar::getJDEpochOffset() const
{
    return COPTIC_JD_EPOCH_OFFSET;
}


U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
//eof
