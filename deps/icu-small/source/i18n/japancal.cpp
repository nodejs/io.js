// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2003-2009,2012,2016 International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File JAPANCAL.CPP
*
* Modification History:
*  05/16/2003    srl     copied from buddhcal.cpp
*
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#if U_PLATFORM_HAS_WINUWP_API == 0
#include <stdlib.h> // getenv() is not available in UWP env
#else
#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#   define VC_EXTRALEAN
#   define NOUSER
#   define NOSERVICE
#   define NOIME
#   define NOMCX
#include <windows.h>
#endif
#include "cmemory.h"
#include "erarules.h"
#include "japancal.h"
#include "unicode/gregocal.h"
#include "umutex.h"
#include "uassert.h"
#include "ucln_in.h"
#include "cstring.h"

static icu::EraRules * gJapaneseEraRules = nullptr;
static icu::UInitOnce gJapaneseEraRulesInitOnce {};
static int32_t gCurrentEra = 0;

U_CDECL_BEGIN
static UBool japanese_calendar_cleanup() {
    if (gJapaneseEraRules) {
        delete gJapaneseEraRules;
        gJapaneseEraRules = nullptr;
    }
    gCurrentEra = 0;
    gJapaneseEraRulesInitOnce.reset();
    return true;
}
U_CDECL_END

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(JapaneseCalendar)

static const int32_t kGregorianEpoch = 1970;    // used as the default value of EXTENDED_YEAR
static const char* TENTATIVE_ERA_VAR_NAME = "ICU_ENABLE_TENTATIVE_ERA";


// Export the following for use by test code.
UBool JapaneseCalendar::enableTentativeEra() {
    // Although start date of next Japanese era is planned ahead, a name of
    // new era might not be available. This implementation allows tester to
    // check a new era without era names by settings below (in priority order).
    // By default, such tentative era is disabled.

    // 1. Environment variable ICU_ENABLE_TENTATIVE_ERA=true or false

    UBool includeTentativeEra = false;

#if U_PLATFORM_HAS_WINUWP_API == 1
    // UWP doesn't allow access to getenv(), but we can call GetEnvironmentVariableW to do the same thing.
    char16_t varName[26] = {};
    u_charsToUChars(TENTATIVE_ERA_VAR_NAME, varName, static_cast<int32_t>(uprv_strlen(TENTATIVE_ERA_VAR_NAME)));
    WCHAR varValue[5] = {};
    DWORD ret = GetEnvironmentVariableW(reinterpret_cast<WCHAR*>(varName), varValue, UPRV_LENGTHOF(varValue));
    if ((ret == 4) && (_wcsicmp(varValue, L"true") == 0)) {
        includeTentativeEra = true;
    }
#else
    char *envVarVal = getenv(TENTATIVE_ERA_VAR_NAME);
    if (envVarVal != nullptr && uprv_stricmp(envVarVal, "true") == 0) {
        includeTentativeEra = true;
    }
#endif
    return includeTentativeEra;
}


// Initialize global Japanese era data
static void U_CALLCONV initializeEras(UErrorCode &status) {
    gJapaneseEraRules = EraRules::createInstance("japanese", JapaneseCalendar::enableTentativeEra(), status);
    if (U_FAILURE(status)) {
        return;
    }
    gCurrentEra = gJapaneseEraRules->getCurrentEraIndex();
}

static void init(UErrorCode &status) {
    umtx_initOnce(gJapaneseEraRulesInitOnce, &initializeEras, status);
    ucln_i18n_registerCleanup(UCLN_I18N_JAPANESE_CALENDAR, japanese_calendar_cleanup);
}

/* Some platforms don't like to export constants, like old Palm OS and some z/OS configurations. */
uint32_t JapaneseCalendar::getCurrentEra() {
    return gCurrentEra;
}

JapaneseCalendar::JapaneseCalendar(const Locale& aLocale, UErrorCode& success)
:   GregorianCalendar(aLocale, success)
{
    init(success);
    setTimeInMillis(getNow(), success); // Call this again now that the vtable is set up properly.
}

JapaneseCalendar::~JapaneseCalendar()
{
}

JapaneseCalendar::JapaneseCalendar(const JapaneseCalendar& source)
: GregorianCalendar(source)
{
    UErrorCode status = U_ZERO_ERROR;
    init(status);
    U_ASSERT(U_SUCCESS(status));
}

JapaneseCalendar& JapaneseCalendar::operator= ( const JapaneseCalendar& right)
{
    GregorianCalendar::operator=(right);
    return *this;
}

JapaneseCalendar* JapaneseCalendar::clone() const
{
    return new JapaneseCalendar(*this);
}

const char *JapaneseCalendar::getType() const
{
    return "japanese";
}

int32_t JapaneseCalendar::getDefaultMonthInYear(int32_t eyear, UErrorCode& status) 
{
    if (U_FAILURE(status)) {
      return 0;
    }
    int32_t era = internalGetEra();
    // TODO do we assume we can trust 'era'?  What if it is denormalized?

    int32_t month = 0;

    // Find out if we are at the edge of an era
    int32_t eraStart[3] = { 0,0,0 };
    gJapaneseEraRules->getStartDate(era, eraStart, status);
    if (U_FAILURE(status)) {
        return 0;
    }
    if(eyear == eraStart[0]) {
        // Yes, we're in the first year of this era.
        return eraStart[1]  // month
                -1;         // return 0-based month
    }

    return month;
}

int32_t JapaneseCalendar::getDefaultDayInMonth(int32_t eyear, int32_t month) 
{
    int32_t era = internalGetEra();
    int32_t day = 1;

    int32_t eraStart[3] = { 0,0,0 };
    UErrorCode status = U_ZERO_ERROR;
    gJapaneseEraRules->getStartDate(era, eraStart, status);
    U_ASSERT(U_SUCCESS(status));
    if (eyear == eraStart[0] && (month == eraStart[1] - 1)) {
        return eraStart[2];
    }
    return day;
}


int32_t JapaneseCalendar::internalGetEra() const
{
    return internalGet(UCAL_ERA, gCurrentEra);
}

int32_t JapaneseCalendar::handleGetExtendedYear(UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return 0;
    }
    // EXTENDED_YEAR in JapaneseCalendar is a Gregorian year
    // The default value of EXTENDED_YEAR is 1970 (Showa 45)

    if (newerField(UCAL_EXTENDED_YEAR, UCAL_YEAR) == UCAL_EXTENDED_YEAR &&
        newerField(UCAL_EXTENDED_YEAR, UCAL_ERA) == UCAL_EXTENDED_YEAR) {
        return internalGet(UCAL_EXTENDED_YEAR, kGregorianEpoch);
    }
    int32_t eraStartYear = gJapaneseEraRules->getStartYear(internalGet(UCAL_ERA, gCurrentEra), status);
    if (U_FAILURE(status)) {
        return 0;
    }

    // extended year is a gregorian year, where 1 = 1AD,  0 = 1BC, -1 = 2BC, etc
    int32_t year = internalGet(UCAL_YEAR, 1);   // pin to minimum of year 1 (first year)
    // add gregorian starting year, subtract one because year starts at 1
    if (uprv_add32_overflow(year, eraStartYear - 1,  &year)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    return year;
}


void JapaneseCalendar::handleComputeFields(int32_t julianDay, UErrorCode& status)
{
    //Calendar::timeToFields(theTime, quick, status);
    GregorianCalendar::handleComputeFields(julianDay, status);
    int32_t year = internalGet(UCAL_EXTENDED_YEAR); // Gregorian year
    int32_t eraIdx = gJapaneseEraRules->getEraIndex(year, internalGetMonth(status) + 1, internalGet(UCAL_DAY_OF_MONTH), status);

    internalSet(UCAL_ERA, eraIdx);
    internalSet(UCAL_YEAR, year - gJapaneseEraRules->getStartYear(eraIdx, status) + 1);
}

/*
Disable pivoting 
*/
UBool JapaneseCalendar::haveDefaultCentury() const
{
    return false;
}

UDate JapaneseCalendar::defaultCenturyStart() const
{
    return 0;// WRONG
}

int32_t JapaneseCalendar::defaultCenturyStartYear() const
{
    return 0;
}

int32_t JapaneseCalendar::handleGetLimit(UCalendarDateFields field, ELimitType limitType) const
{
    switch(field) {
    case UCAL_ERA:
        if (limitType == UCAL_LIMIT_MINIMUM || limitType == UCAL_LIMIT_GREATEST_MINIMUM) {
            return 0;
        }
        return gJapaneseEraRules->getNumberOfEras() - 1; // max known era, not gCurrentEra
    case UCAL_YEAR:
        {
            switch (limitType) {
            case UCAL_LIMIT_MINIMUM:
            case UCAL_LIMIT_GREATEST_MINIMUM:
                return 1;
            case UCAL_LIMIT_LEAST_MAXIMUM:
                return 1;
            case  UCAL_LIMIT_COUNT: //added to avoid warning
            case UCAL_LIMIT_MAXIMUM:
            {
                UErrorCode status = U_ZERO_ERROR;
                int32_t eraStartYear = gJapaneseEraRules->getStartYear(gCurrentEra, status);
                U_ASSERT(U_SUCCESS(status));
                return GregorianCalendar::handleGetLimit(UCAL_YEAR, UCAL_LIMIT_MAXIMUM) - eraStartYear;
            }
            default:
                return 1;    // Error condition, invalid limitType
            }
        }
    default:
        return GregorianCalendar::handleGetLimit(field,limitType);
    }
}

int32_t JapaneseCalendar::getActualMaximum(UCalendarDateFields field, UErrorCode& status) const {
    if (field != UCAL_YEAR) {
        return GregorianCalendar::getActualMaximum(field, status);
    }
    int32_t era = get(UCAL_ERA, status);
    if (U_FAILURE(status)) {
        return 0; // error case... any value
    }
    if (era == gJapaneseEraRules->getNumberOfEras() - 1) { // max known era, not gCurrentEra
        // TODO: Investigate what value should be used here - revisit after 4.0.
        return handleGetLimit(UCAL_YEAR, UCAL_LIMIT_MAXIMUM);
    }
    int32_t nextEraStart[3] = { 0,0,0 };
    gJapaneseEraRules->getStartDate(era + 1, nextEraStart, status);
    int32_t nextEraYear = nextEraStart[0];
    int32_t nextEraMonth = nextEraStart[1]; // 1-base
    int32_t nextEraDate = nextEraStart[2];

    int32_t eraStartYear = gJapaneseEraRules->getStartYear(era, status);
    int32_t maxYear = nextEraYear - eraStartYear + 1;   // 1-base
    if (nextEraMonth == 1 && nextEraDate == 1) {
        // Subtract 1, because the next era starts at Jan 1
        maxYear--;
    }
    return maxYear;
}

U_NAMESPACE_END

#endif
